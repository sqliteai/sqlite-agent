//
//  sqlite-agent.c
//  sqlite-agent
//
//  Created by Gioele Cantoni on 05/11/25.
//

#define DEFAULT_AGENT_MAX_ITERATIONS 5

//#define AGENT_DEBUG 1
#ifdef AGENT_DEBUG
  #define D(x) fprintf(stderr, "[DEBUG] " x "\n")
  #define DF(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", __VA_ARGS__)
#else
  #define D(x)
  #define DF(fmt, ...)
#endif

#include "sqlite-agent.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

SQLITE_EXTENSION_INIT1

static void agent_version(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  sqlite3_result_text(context, SQLITE_AGENT_VERSION, -1, NULL);
}

static char* agent_call_mcp_tool(sqlite3 *db, const char *tool_name, const char *tool_args) {
  sqlite3_stmt *stmt;
  char sql[1024];
  snprintf(sql, sizeof(sql), "SELECT mcp_call_tool_json(?, ?)");

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    DF("Failed to prepare mcp_call_tool_json(): %s", sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, tool_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, tool_args, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    DF("Failed to execute mcp_call_tool_json(): %s", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }

  const char *result = (const char*)sqlite3_column_text(stmt, 0);
  if (!result) {
    sqlite3_finalize(stmt);
    return NULL;
  }

  char *result_copy = strdup(result);
  sqlite3_finalize(stmt);
  return result_copy;
}

static char* agent_get_tools_list(sqlite3 *db) {
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, "SELECT mcp_list_tools_json()", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    DF("Failed to prepare mcp_list_tools_json(): %s", sqlite3_errmsg(db));
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    DF("Failed to execute mcp_list_tools_json(): %s", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }

  const char *tools_result = (const char*)sqlite3_column_text(stmt, 0);
  if (!tools_result) {
    sqlite3_finalize(stmt);
    return NULL;
  }

  char *formatted = malloc(32768);
  if (!formatted) {
    sqlite3_finalize(stmt);
    return NULL;
  }

  snprintf(formatted, 32768, "Available tools (JSON):\n%s", tools_result);
  sqlite3_finalize(stmt);
  return formatted;
}

static int agent_create_chat_context(sqlite3 *db, const char *tools_list) {
  int rc;
  sqlite3_stmt *ctx_check_stmt;
  int existing_ctx_size = 0;

  rc = sqlite3_prepare_v2(db, "SELECT llm_context_size()", -1, &ctx_check_stmt, 0);
  if (rc == SQLITE_OK) {
    if (sqlite3_step(ctx_check_stmt) == SQLITE_ROW) {
      existing_ctx_size = sqlite3_column_int(ctx_check_stmt, 0);
      DF("Existing context size: %d", existing_ctx_size);
    }
    sqlite3_finalize(ctx_check_stmt);
  }

  int tools_list_len = (int)strlen(tools_list);
  int calculated_ctx_size = tools_list_len * 2;

  if (calculated_ctx_size < 4096) {
    calculated_ctx_size = 4096;
  }

  DF("Calculated context size: %d (tools list: %d bytes)", calculated_ctx_size, tools_list_len);

  int ctx_size = (existing_ctx_size > calculated_ctx_size) ? existing_ctx_size : calculated_ctx_size;

  char create_cmd[256];
  snprintf(create_cmd, sizeof(create_cmd),
           "SELECT llm_context_create_chat('context_size=%d')", ctx_size);
  rc = sqlite3_exec(db, create_cmd, 0, 0, 0);

  DF("Created chat context with size: %d", ctx_size);

  return rc;
}

static void agent_run_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if (argc < 1 || argc > 4) {
    sqlite3_result_error(context, "agent_run requires 1-4 arguments: (goal, [table_name], [max_iterations], [system_prompt])", -1);
    return;
  }

  const char *goal = (const char*)sqlite3_value_text(argv[0]);
  const char *table_name = NULL;
  int max_iterations = DEFAULT_AGENT_MAX_ITERATIONS;
  const char *custom_system_prompt = NULL;

  if (argc >= 2) {
    if (sqlite3_value_type(argv[1]) == SQLITE_INTEGER) {
      max_iterations = sqlite3_value_int(argv[1]);
      table_name = NULL;
    } else {
      table_name = (const char*)sqlite3_value_text(argv[1]);
      if (table_name && strlen(table_name) == 0) {
        table_name = NULL;
      }
    }
  }

  if (argc >= 3) {
    max_iterations = sqlite3_value_int(argv[2]);
  }

  if (argc == 4) {
    custom_system_prompt = (const char*)sqlite3_value_text(argv[3]);
  }

  if (!goal) {
    sqlite3_result_error(context, "goal must be non-null", -1);
    return;
  }

  sqlite3 *db = sqlite3_context_db_handle(context);

  if (!table_name) {
    D("MODE 1: Text-Only Response");
    sqlite3_stmt *stmt = NULL;
    int rc;
    char final_result[8192] = {0};

    char *tools_list = agent_get_tools_list(db);
    if (!tools_list) {
      D("ERROR: Failed to list MCP tools");
      sqlite3_result_error(context, "Not connected. Call mcp_connect() first", -1);
      return;
    }
    DF("Received tools list (length=%zu)", strlen(tools_list));

    rc = agent_create_chat_context(db, tools_list);
    if (rc != SQLITE_OK) {
      D("ERROR: Failed to create LLM chat context");
      free(tools_list);
      sqlite3_result_error(context, "Failed to create LLM chat context", -1);
      return;
    }

    for (int i = 0; i < max_iterations; i++) {
      DF("Iteration %d/%d", i+1, max_iterations);

      char system_prompt[32768];
      if (custom_system_prompt && strlen(custom_system_prompt) > 0) {
        strncpy(system_prompt, custom_system_prompt, sizeof(system_prompt) - 1);
        system_prompt[sizeof(system_prompt) - 1] = '\0';
      } else {
        snprintf(system_prompt, sizeof(system_prompt),
          "You are an AI agent that can use tools to accomplish tasks.\n\n"
          "%s\n"
          "User goal: %s\n\n"
          "To use a tool, respond with EXACTLY this format:\n"
          "TOOL_CALL: tool_name\n"
          "ARGS: {\"param1\": \"value1\", \"param2\": \"value2\"}\n\n"
          "After the tool executes, you'll see the result and can call another tool or provide a final answer.\n"
          "Type DONE only when you have completed the task.",
          tools_list, goal);
      }

      DF("System prompt (length=%zu):\n%s", strlen(system_prompt), system_prompt);

      rc = sqlite3_prepare_v2(db, "SELECT llm_chat_respond(?)", -1, &stmt, 0);
      if (rc != SQLITE_OK) {
        D("ERROR: Failed to prepare LLM query");
        free(tools_list);
        sqlite3_result_error(context, "Failed to prepare LLM query", -1);
        return;
      }

      sqlite3_bind_text(stmt, 1, system_prompt, -1, SQLITE_TRANSIENT);

      if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        D("ERROR: LLM did not respond");
        free(tools_list);
        sqlite3_result_error(context, "LLM did not respond", -1);
        return;
      }

      const char *llm_response = (const char*)sqlite3_column_text(stmt, 0);
      if (!llm_response) {
        D("WARNING: LLM returned NULL response, ending loop");
        sqlite3_finalize(stmt);
        break;
      }

      DF("LLM Response (length=%zu):\n%s", strlen(llm_response), llm_response);

      if (strstr(llm_response, "DONE") != NULL) {
        D("Agent said DONE - ending loop");
        strncpy(final_result, llm_response, sizeof(final_result) - 1);
        sqlite3_finalize(stmt);
        break;
      }

      const char *tool_call_marker = strstr(llm_response, "TOOL_CALL:");
      const char *args_marker = strstr(llm_response, "ARGS:");

      if (!tool_call_marker) {
        D("No TOOL_CALL marker - treating as final answer");
        strncpy(final_result, llm_response, sizeof(final_result) - 1);
        sqlite3_finalize(stmt);
        break;
      }

      if (!args_marker) {
        D("WARNING: Found TOOL_CALL but missing ARGS - defaulting to empty args");
      }

      tool_call_marker += 10;
      while (*tool_call_marker == ' ' || *tool_call_marker == '\n') tool_call_marker++;
      const char *tool_end = strchr(tool_call_marker, '\n');
      if (!tool_end) tool_end = tool_call_marker + strlen(tool_call_marker);

      char tool_name_buf[256] = {0};
      int tool_name_len = tool_end - tool_call_marker;
      if (tool_name_len > 255) tool_name_len = 255;
      strncpy(tool_name_buf, tool_call_marker, tool_name_len);

      char *p = tool_name_buf + strlen(tool_name_buf) - 1;
      while (p > tool_name_buf && (*p == ' ' || *p == '\n' || *p == '\r')) {
        *p = '\0';
        p--;
      }

      char tool_args[2048] = {0};
      if (args_marker) {
        args_marker += 5;
        while (*args_marker == ' ' || *args_marker == '\n') args_marker++;

        const char *args_start = strchr(args_marker, '{');
        if (args_start) {
          int brace_count = 1;
          const char *p = args_start + 1;
          while (*p && brace_count > 0) {
            if (*p == '{') brace_count++;
            else if (*p == '}') brace_count--;
            if (brace_count > 0) p++;
          }
          if (brace_count == 0) {
            size_t len = p - args_start + 1;
            if (len < sizeof(tool_args)) {
              strncpy(tool_args, args_start, len);
              tool_args[len] = '\0';
            }
          }
        } else {
          const char *newline = strchr(args_marker, '\n');
          size_t len = newline ? (newline - args_marker) : strlen(args_marker);
          if (len >= sizeof(tool_args)) len = sizeof(tool_args) - 1;
          strncpy(tool_args, args_marker, len);
          tool_args[len] = '\0';
        }
      } else {
        strcpy(tool_args, "{}");
      }

      DF("Extracted tool: '%s' args: '%s'", tool_name_buf, tool_args);

      sqlite3_finalize(stmt);

      char *tool_result = agent_call_mcp_tool(db, tool_name_buf, tool_args);
      if (!tool_result) {
        DF("ERROR: Failed to execute tool '%s'", tool_name_buf);
        snprintf(final_result, sizeof(final_result),
                "{\"error\": \"Failed to execute tool %s\"}", tool_name_buf);
        break;
      }

      DF("Tool result (length=%zu): %.500s%s",
         strlen(tool_result),
         tool_result,
         strlen(tool_result) > 500 ? "..." : "");

      strncpy(final_result, tool_result, sizeof(final_result) - 1);
      free(tool_result);

      if (strstr(final_result, "\"error\"")) {
        D("Tool returned error, continuing to next iteration");
        continue;
      }
    }

    free(tools_list);

    sqlite3_result_text(context, final_result, -1, SQLITE_TRANSIENT);
    return;
  }

  D("MODE 2: Table Extraction Mode");
  char schema_query[512];
  snprintf(schema_query, sizeof(schema_query), "PRAGMA table_info(%s)", table_name);

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, schema_query, -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, "Failed to query table schema", -1);
    return;
  }

  char schema_desc[4096] = "Table columns:\n";
  char column_names[256][64];
  char column_types[256][16];
  int column_count = 0;
  int embedding_col_indices[32];
  int embedding_col_count = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW && column_count < 256) {
    const char *col_name = (const char*)sqlite3_column_text(stmt, 1);
    const char *col_type = (const char*)sqlite3_column_text(stmt, 2);

    if (!col_name || !col_type) continue;

    strncpy(column_names[column_count], col_name, 63);
    column_names[column_count][63] = '\0';
    strncpy(column_types[column_count], col_type, 15);
    column_types[column_count][15] = '\0';

    if (strcmp(col_type, "BLOB") == 0 &&
        (strcmp(col_name, "embedding") == 0 || strstr(col_name, "_embedding"))) {
      if (embedding_col_count < 32) {
        embedding_col_indices[embedding_col_count++] = column_count;
      }
    }

    int is_embedding = 0;
    for (int i = 0; i < embedding_col_count; i++) {
      if (embedding_col_indices[i] == column_count) {
        is_embedding = 1;
        break;
      }
    }
    if (!is_embedding) {
      char col_desc[128];
      snprintf(col_desc, sizeof(col_desc), "  - %s (%s)\n", col_name, col_type);
      strncat(schema_desc, col_desc, sizeof(schema_desc) - strlen(schema_desc) - 1);
    }

    column_count++;
  }
  sqlite3_finalize(stmt);

  if (column_count == 0) {
    sqlite3_result_error(context, "Table does not exist or has no columns", -1);
    return;
  }

  DF("Goal: %s", goal);
  DF("Table: %s", table_name);
  DF("Max iterations: %d", max_iterations);

  char *tools_list = agent_get_tools_list(db);
  if (!tools_list) {
    D("ERROR: Failed to list MCP tools");
    sqlite3_result_error(context, "Not connected. Call mcp_connect() first", -1);
    return;
  }
  DF("Received tools list (length=%zu)", strlen(tools_list));

  char system_prompt[20480];

  if (custom_system_prompt && strlen(custom_system_prompt) > 0) {
    strncpy(system_prompt, custom_system_prompt, sizeof(system_prompt) - 1);
    system_prompt[sizeof(system_prompt) - 1] = '\0';
  } else {
    snprintf(system_prompt, sizeof(system_prompt),
      "You are a tool-calling agent. You MUST respond with ONLY a tool call, nothing else.\n\n"
      "%s\n\n"
      "TARGET DATA SCHEMA:\n"
      "You need to collect data that will populate a table with these columns:\n"
      "%s\n"
      "Make sure to search for properties/items that have information matching these columns.\n\n"
      "IMPORTANT RULES:\n"
      "1. Your response must be ONLY in this EXACT JSON format:\n"
      "   {\"tool\": \"tool_name\", \"args\": {\"param1\": \"value1\", \"param2\": 123}}\n"
      "2. Do NOT include explanations, reasoning, or any other text\n"
      "3. Do NOT use markdown code blocks or backticks\n"
      "4. ONLY use the exact parameter names shown in the tool signatures above\n"
      "5. Use proper JSON: keys in \"quotes\", boolean as true/false (lowercase), strings in \"quotes\"\n"
      "6. You can make MULTIPLE tool calls across iterations to gather detailed data\n"
      "7. Type DONE only when you have retrieved sufficient detailed information\n\n"
      "CRITICAL: Extract actual values from previous tool responses\n"
      "✓ CORRECT: {\"args\": {\"name\": \"sqlite-agent\"}}   (literal value from response)\n"
      "✗ WRONG:   {\"args\": {\"name\": \"{{items[0].name}}\"}}  (template syntax - will fail!)\n"
      "✗ WRONG:   {\"args\": {\"name\": \"<name-from-search>\"}} (placeholder - will fail!)\n"
      "When you receive tool responses, read the actual values and use them directly.\n\n"
      "Task: %s\n\n"
      "Respond with ONLY the JSON tool call:",
      tools_list, schema_desc, goal);
  }

  DF("System prompt (length=%zu):\n%s", strlen(system_prompt), system_prompt);

  rc = agent_create_chat_context(db, tools_list);
  if (rc != SQLITE_OK) {
    D("ERROR: Failed to create LLM chat context");
    free(tools_list);
    sqlite3_result_error(context, "Failed to create LLM chat context", -1);
    return;
  }

  // Calculate dynamic truncation based on available context space
  int tools_list_len = (int)strlen(tools_list);
  int system_prompt_len = (int)strlen(system_prompt);
  int extraction_prompt_overhead = 2000; // Overhead for extraction prompt template
  int safety_margin = 1024; // Additional buffer for JSON overhead

  // Context size (from agent_create_chat_context logic: tools_list_len * 2, min 4096)
  int ctx_size = tools_list_len * 2;
  if (ctx_size < 4096) ctx_size = 4096;

  // Calculate available space for conversation history
  // ctx_size must accommodate: tools_list + system_prompt + conversation_history + extraction_prompt
  int available_for_conversation = ctx_size - tools_list_len - system_prompt_len
                                    - extraction_prompt_overhead - safety_margin;

  // Ensure minimum reasonable space
  if (available_for_conversation < 8192) available_for_conversation = 8192;

  // Assume average of max_iterations/2 tool calls, allocate space for each
  size_t dynamic_truncate_at = available_for_conversation / ((max_iterations + 1) / 2);

  // Apply reasonable bounds
  if (dynamic_truncate_at < 4096) dynamic_truncate_at = 4096;     // Minimum per tool
  if (dynamic_truncate_at > 50000) dynamic_truncate_at = 50000;   // Maximum per tool

  DF("Dynamic truncation: ctx_size=%d, tools=%d, system=%d, available=%d, truncate_at=%zu",
     ctx_size, tools_list_len, system_prompt_len, available_for_conversation, dynamic_truncate_at);

  DF("Starting agent loop with max_iterations=%d", max_iterations);
  char conversation_history[32768] = {0};
  int consecutive_errors = 0;
  char last_error[512] = {0};
  for (int loop = 0; loop < max_iterations; loop++) {
    DF("Table loop %d/%d", loop+1, max_iterations);

    char user_message[24576];
    if (loop == 0) {
      snprintf(user_message, sizeof(user_message), "%s", system_prompt);
    } else {
      snprintf(user_message, sizeof(user_message), "Continue");
    }

    rc = sqlite3_prepare_v2(db, "SELECT llm_chat_respond(?)", -1, &stmt, 0);
    if (rc != SQLITE_OK) {
      D("ERROR: Failed to prepare LLM query");
      continue;
    }

    sqlite3_bind_text(stmt, 1, user_message, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
      DF("ERROR: Failed to get LLM response (rc=%d): %s", rc, sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      continue;
    }

    const char *agent_response = (const char*)sqlite3_column_text(stmt, 0);
    if (!agent_response) {
      D("WARNING: LLM returned NULL response");
      sqlite3_finalize(stmt);
      break;
    }

    char response_copy[8192];
    strncpy(response_copy, agent_response, sizeof(response_copy) - 1);
    response_copy[sizeof(response_copy) - 1] = '\0';
    sqlite3_finalize(stmt);

    DF("Agent Response:\n%s", response_copy);

    if (strstr(response_copy, "DONE") != NULL) {
      D("Agent said DONE - ending loop");
      break;
    }

    char tool_name[256] = {0};
    char tool_args[4096] = {0};

    const char *tool_marker = strstr(response_copy, "\"tool\"");
    const char *args_marker = strstr(response_copy, "\"args\"");

    if (tool_marker && args_marker) {
      const char *tool_value = strchr(tool_marker + 6, '"');
      if (tool_value) {
        tool_value++;
        const char *tool_end = strchr(tool_value, '"');
        if (tool_end) {
          size_t len = tool_end - tool_value;
          if (len < sizeof(tool_name)) {
            strncpy(tool_name, tool_value, len);
            tool_name[len] = '\0';
          }
        }
      }

      const char *args_start = strchr(args_marker + 6, '{');
      if (args_start) {
        int brace_count = 1;
        const char *p = args_start + 1;
        while (*p && brace_count > 0) {
          if (*p == '{') brace_count++;
          else if (*p == '}') brace_count--;
          if (brace_count > 0) p++;
        }
        if (brace_count == 0) {
          size_t len = p - args_start + 1;
          if (len < sizeof(tool_args)) {
            strncpy(tool_args, args_start, len);
            tool_args[len] = '\0';
          }
        }
      }
    }

    if (tool_name[0]) {
      DF("Parsed tool: '%s' args: '%s'", tool_name, tool_args);

      if (strstr(tool_args, "{{") != NULL || strstr(tool_args, "}}") != NULL) {
        D("ERROR: Tool args contain template syntax {{...}}");
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg),
                 "ERROR: Tool args contain invalid template syntax: %.200s", tool_args);
        strncat(conversation_history, error_msg,
                sizeof(conversation_history) - strlen(conversation_history) - 1);
        strncat(conversation_history, "\n",
                sizeof(conversation_history) - strlen(conversation_history) - 1);
        continue;
      }

      char *tool_result = agent_call_mcp_tool(db, tool_name, tool_args);
      if (tool_result) {
        DF("Tool result (length=%zu): %.500s%s",
           strlen(tool_result),
           tool_result,
           strlen(tool_result) > 500 ? "..." : "");

        int is_error = (strstr(tool_result, "\"isError\":true") != NULL ||
                        strstr(tool_result, "404 Not Found") != NULL ||
                        strstr(tool_result, "failed to") != NULL);

        if (is_error) {
          char current_error[256];
          snprintf(current_error, sizeof(current_error), "%.200s", tool_result);
          if (strcmp(current_error, last_error) == 0) {
            consecutive_errors++;
            DF("WARNING: Same error repeated %d times", consecutive_errors);
            if (consecutive_errors >= 3) {
              D("ERROR: Stopping due to 3 consecutive identical errors");
              free(tool_result);
              break;
            }
          } else {
            strncpy(last_error, current_error, sizeof(last_error) - 1);
            consecutive_errors = 1;
          }
        } else {
          consecutive_errors = 0;
          last_error[0] = '\0';
        }

        char result_msg[16384];
        // Use dynamic truncation calculated based on available context space
        if (strlen(tool_result) > dynamic_truncate_at) {
          // Dynamically create format string for truncation
          char format_str[128];
          snprintf(format_str, sizeof(format_str),
                   "Tool %%s returned (truncated to %%zu chars): %%.%zus...\n",
                   dynamic_truncate_at);
          snprintf(result_msg, sizeof(result_msg), format_str,
                   tool_name, dynamic_truncate_at, tool_result);
        } else {
          snprintf(result_msg, sizeof(result_msg),
                   "Tool %s returned: %s\n", tool_name, tool_result);
        }
        strncat(conversation_history, result_msg,
                sizeof(conversation_history) - strlen(conversation_history) - 1);
        free(tool_result);
      } else {
        DF("ERROR: Tool '%s' returned NULL", tool_name);
      }
    } else {
      D("WARNING: Could not parse tool call from agent response");
    }
  }

  DF("Conversation history (length=%zu):", strlen(conversation_history));
  DF("=== FULL CONVERSATION HISTORY ===\n%s\n=== END CONVERSATION HISTORY ===", conversation_history);

  char extraction_prompt[32768];
  snprintf(extraction_prompt, sizeof(extraction_prompt),
    "Extract structured data from the following information and format it as a JSON array.\n\n"
    "%s\n\n"
    "IMPORTANT:\n"
    "- Return ONLY a JSON array of objects\n"
    "- Each object must have these EXACT keys (matching column names):\n"
    "%s\n"
    "- Extract ALL available data that matches the schema\n"
    "- Use null for missing values\n"
    "- Do NOT include the 'embedding' column if present\n\n"
    "CRITICAL ID EXTRACTION RULE:\n"
    "If the schema has an 'id' column, look in the JSON data for fields like:\n"
    "- \"id\", \"listing_id\", \"property_id\", \"item_id\", etc.\n"
    "Extract the ACTUAL numeric/string ID value from the source data.\n"
    "Example: if you see {\"id\": 123456789, \"title\": \"Rome Apartment\"}, use 123456789\n"
    "NEVER use 0, 1, 2, 3 as IDs - use the real IDs from the data!\n\n"
    "Data to extract:\n%.6000s\n\n"
    "Return ONLY the JSON array:",
    schema_desc, schema_desc, conversation_history);

  DF("=== FULL EXTRACTION PROMPT ===\n%s\n=== END EXTRACTION PROMPT ===", extraction_prompt);

  sqlite3_stmt *ctx_size_stmt;
  int ctx_size_for_extraction = 0;
  rc = sqlite3_prepare_v2(db, "SELECT llm_context_size()", -1, &ctx_size_stmt, 0);
  if (rc == SQLITE_OK) {
    if (sqlite3_step(ctx_size_stmt) == SQLITE_ROW) {
      ctx_size_for_extraction = sqlite3_column_int(ctx_size_stmt, 0);
      DF("Context size for extraction: %d", ctx_size_for_extraction);
    }
    sqlite3_finalize(ctx_size_stmt);
  }

  if (ctx_size_for_extraction > 0) {
    char create_extraction_cmd[256];
    snprintf(create_extraction_cmd, sizeof(create_extraction_cmd),
             "SELECT llm_context_create_chat('context_size=%d')", ctx_size_for_extraction);
    sqlite3_exec(db, create_extraction_cmd, 0, 0, 0);
  } else {
    sqlite3_exec(db, "SELECT llm_context_create_chat()", 0, 0, 0);
  }

  rc = sqlite3_prepare_v2(db, "SELECT llm_chat_respond(?)", -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    D("ERROR: Failed to prepare extraction query");
    sqlite3_result_error(context, "Failed to prepare extraction", -1);
    return;
  }

  sqlite3_bind_text(stmt, 1, extraction_prompt, -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    D("ERROR: LLM extraction failed");
    sqlite3_result_error(context, "Failed to extract structured data", -1);
    sqlite3_finalize(stmt);
    return;
  }

  const char *json_data = (const char*)sqlite3_column_text(stmt, 0);
  char json_copy[32768];
  strncpy(json_copy, json_data ? json_data : "[]", sizeof(json_copy) - 1);
  json_copy[sizeof(json_copy) - 1] = '\0';
  sqlite3_finalize(stmt);

  DF("=== FULL EXTRACTED JSON ===\n%s\n=== END EXTRACTED JSON ===", json_copy);

  sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

  int rows_inserted = 0;

  const char *pos = json_copy;
  while ((pos = strchr(pos, '{')) != NULL) {
    const char *obj_start = pos;
    const char *obj_end = strchr(obj_start, '}');
    if (!obj_end) {
      D("WARNING: Could not find closing brace for object");
      break;
    }

    DF("Found JSON object (length=%ld): %.200s...", (long)(obj_end - obj_start), obj_start);

    char insert_sql[2048];
    char columns_part[512] = "";
    char values_part[512] = "";

    int first_col = 1;
    for (int i = 0; i < column_count; i++) {
      int is_embedding = 0;
      for (int j = 0; j < embedding_col_count; j++) {
        if (embedding_col_indices[j] == i) {
          is_embedding = 1;
          break;
        }
      }
      if (is_embedding) continue;

      if (!first_col) {
        strcat(columns_part, ", ");
        strcat(values_part, ", ");
      }
      strcat(columns_part, column_names[i]);
      strcat(values_part, "?");
      first_col = 0;
    }

    snprintf(insert_sql, sizeof(insert_sql),
             "INSERT INTO %s (%s) VALUES (%s)",
             table_name, columns_part, values_part);

    DF("Preparing INSERT: %s", insert_sql);

    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
      DF("ERROR: Failed to prepare insert statement: %s", sqlite3_errmsg(db));
      sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
      sqlite3_result_error(context, "Failed to prepare insert statement", -1);
      return;
    }

    int bind_idx = 1;
    for (int i = 0; i < column_count; i++) {
      int is_embedding = 0;
      for (int j = 0; j < embedding_col_count; j++) {
        if (embedding_col_indices[j] == i) {
          is_embedding = 1;
          break;
        }
      }
      if (is_embedding) continue;

      char search_key[128];
      snprintf(search_key, sizeof(search_key), "\"%s\"", column_names[i]);
      const char *key_pos = strstr(obj_start, search_key);

      if (key_pos && key_pos < obj_end) {
        const char *value_start = strchr(key_pos, ':');
        if (value_start) {
          value_start++;
          while (*value_start == ' ' || *value_start == '\t') value_start++;

          if (strcmp(column_types[i], "INTEGER") == 0) {
            if (strncmp(value_start, "null", 4) == 0) {
              sqlite3_bind_null(stmt, bind_idx);
            } else if (*value_start == '"') {
              value_start++;
              const char *value_end = strchr(value_start, '"');
              if (value_end) {
                char num_str[64];
                size_t len = value_end - value_start;
                if (len >= sizeof(num_str)) len = sizeof(num_str) - 1;
                strncpy(num_str, value_start, len);
                num_str[len] = '\0';
                sqlite3_bind_int64(stmt, bind_idx, atoll(num_str));
              } else {
                sqlite3_bind_null(stmt, bind_idx);
              }
            } else {
              sqlite3_bind_int64(stmt, bind_idx, atoll(value_start));
            }
          } else if (strcmp(column_types[i], "REAL") == 0) {
            if (strncmp(value_start, "null", 4) == 0) {
              sqlite3_bind_null(stmt, bind_idx);
            } else if (*value_start == '"') {
              value_start++;
              const char *value_end = strchr(value_start, '"');
              if (value_end) {
                char num_str[64];
                size_t len = value_end - value_start;
                if (len >= sizeof(num_str)) len = sizeof(num_str) - 1;
                strncpy(num_str, value_start, len);
                num_str[len] = '\0';
                sqlite3_bind_double(stmt, bind_idx, atof(num_str));
              } else {
                sqlite3_bind_null(stmt, bind_idx);
              }
            } else {
              sqlite3_bind_double(stmt, bind_idx, atof(value_start));
            }
          } else {
            if (*value_start == '"') {
              value_start++;
              const char *value_end = strchr(value_start, '"');
              if (value_end) {
                char str_value[512];
                size_t len = value_end - value_start;
                if (len >= sizeof(str_value)) len = sizeof(str_value) - 1;
                strncpy(str_value, value_start, len);
                str_value[len] = '\0';
                sqlite3_bind_text(stmt, bind_idx, str_value, -1, SQLITE_TRANSIENT);
              } else {
                sqlite3_bind_null(stmt, bind_idx);
              }
            } else if (strncmp(value_start, "null", 4) == 0) {
              sqlite3_bind_null(stmt, bind_idx);
            }
          }
        } else {
          sqlite3_bind_null(stmt, bind_idx);
        }
      } else {
        sqlite3_bind_null(stmt, bind_idx);
      }

      bind_idx++;
    }

    #ifdef AGENT_DEBUG
    char *expanded_sql = sqlite3_expanded_sql(stmt);
    if (expanded_sql) {
      DF("Full INSERT:\n%s", expanded_sql);
      sqlite3_free(expanded_sql);
    }
    #endif

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
      DF("ERROR: Insert failed (rc=%d): %s", rc, sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      sqlite3_exec(db, "ROLLBACK", 0, 0, 0);

      char error_msg[512];
      snprintf(error_msg, sizeof(error_msg), "Failed to insert row: %s", sqlite3_errmsg(db));
      sqlite3_result_error(context, error_msg, -1);
      return;
    }

    sqlite3_finalize(stmt);
    rows_inserted++;
    DF("Row %d inserted", rows_inserted);
    pos = obj_end + 1;
  }

  DF("Total rows inserted: %d", rows_inserted);

  sqlite3_exec(db, "COMMIT", 0, 0, 0);

  if (embedding_col_count > 0 && rows_inserted > 0) {
      rc = sqlite3_prepare_v2(db, "SELECT llm_context_create_embedding('embedding_type=FLOAT32')", -1, &stmt, 0);
      if (rc == SQLITE_OK) {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
      }

      for (int emb_idx = 0; emb_idx < embedding_col_count; emb_idx++) {
        int emb_col = embedding_col_indices[emb_idx];
        const char *emb_col_name = column_names[emb_col];

        char available_cols[1024] = "";
        int first = 1;
        for (int i = 0; i < column_count; i++) {
          int is_embedding = 0;
          for (int j = 0; j < embedding_col_count; j++) {
            if (embedding_col_indices[j] == i) {
              is_embedding = 1;
              break;
            }
          }
          if (is_embedding) continue;
          if (strcmp(column_types[i], "TEXT") != 0) continue;

          if (!first) strcat(available_cols, ", ");
          first = 0;
          strcat(available_cols, column_names[i]);
        }

        if (strlen(available_cols) == 0) continue;

        char mapping_prompt[2048];
        snprintf(mapping_prompt, sizeof(mapping_prompt),
          "Table has columns: %s\n\n"
          "For the '%s' embedding column, which source columns should be embedded together?\n"
          "Return ONLY comma-separated column names, no explanation.\n"
          "Example: title, description\n\n"
          "Relevant columns: ",
          available_cols, emb_col_name);

        rc = sqlite3_prepare_v2(db, "SELECT llm_chat_respond(?)", -1, &stmt, 0);
        if (rc != SQLITE_OK) continue;

        sqlite3_bind_text(stmt, 1, mapping_prompt, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_ROW) {
          sqlite3_finalize(stmt);
          continue;
        }

        const char *llm_response = (const char*)sqlite3_column_text(stmt, 0);
        char selected_cols[1024];
        strncpy(selected_cols, llm_response ? llm_response : "", sizeof(selected_cols) - 1);
        selected_cols[sizeof(selected_cols) - 1] = '\0';
        sqlite3_finalize(stmt);

        char embed_sql[2048];
        snprintf(embed_sql, sizeof(embed_sql), "UPDATE %s SET %s = llm_embed_generate(",
                table_name, emb_col_name);

        char *token = strtok(selected_cols, ",");
        first = 1;
        while (token != NULL) {
          while (*token == ' ') token++;
          char *end = token + strlen(token) - 1;
          while (end > token && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
          }

          int found = 0;
          for (int i = 0; i < column_count; i++) {
            if (strcmp(column_names[i], token) == 0) {
              found = 1;
              break;
            }
          }

          if (found) {
            if (!first) strcat(embed_sql, " || ' | ' || ");
            first = 0;
            strcat(embed_sql, "COALESCE(");
            strcat(embed_sql, token);
            strcat(embed_sql, ", '')");
          }

          token = strtok(NULL, ",");
        }

        strcat(embed_sql, ", '') WHERE ");
        strcat(embed_sql, emb_col_name);
        strcat(embed_sql, " IS NULL");

        sqlite3_exec(db, embed_sql, 0, 0, 0);
      }

    if (embedding_col_count > 0) {
      DF("Initializing vector indices for %d embedding columns", embedding_col_count);

      rc = sqlite3_prepare_v2(db, "SELECT llm_model_n_embd()", -1, &stmt, 0);
      if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        int n_embd = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        DF("Embedding dimension: %d", n_embd);

        if (n_embd > 0) {
          for (int emb_idx = 0; emb_idx < embedding_col_count; emb_idx++) {
            int emb_col = embedding_col_indices[emb_idx];
            const char *emb_col_name = column_names[emb_col];

            char vector_init_sql[512];
            snprintf(vector_init_sql, sizeof(vector_init_sql),
              "SELECT vector_init('%s', '%s', 'dimension=%d,type=FLOAT32,distance=cosine')",
              table_name, emb_col_name, n_embd);

            DF("Initializing vector index for %s.%s", table_name, emb_col_name);

            char *vec_err = NULL;
            rc = sqlite3_exec(db, vector_init_sql, 0, 0, &vec_err);
            if (rc != SQLITE_OK) {
              DF("ERROR: vector_init failed for %s: %s", emb_col_name, vec_err ? vec_err : "(unknown)");
              sqlite3_free(vec_err);
            } else {
              DF("Vector index initialized for %s", emb_col_name);
            }
          }
        } else {
          D("WARNING: Embedding dimension is 0");
        }
      } else {
        DF("WARNING: Failed to get embedding dimension (rc=%d): %s", rc, sqlite3_errmsg(db));
        if (stmt) sqlite3_finalize(stmt);
      }
    }
  }

  free(tools_list);
  sqlite3_result_int(context, rows_inserted);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_agent_init(
  sqlite3 *db,
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  rc = sqlite3_create_function(db, "agent_version", 0,
                               SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                               0, agent_version, 0, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_function(db, "agent_run", -1,
                               SQLITE_UTF8,
                               0, agent_run_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  return rc;
}
