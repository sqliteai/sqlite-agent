//
//  github.c
//  sqlite-agent
//
//  Created by Gioele Cantoni on 05/11/25.
//
//  Demonstrates GitHub team activity analysis using Agent + MCP + AI + Vector extensions.
//  Fetches real GitHub repository data, generates embeddings, and performs
//  semantic analysis of development patterns.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define AGENT_EXT "./dist/agent"
#define MCP_EXT "../sqlite-mcp/dist/mcp"
#define VEC_EXT "../sqlite-vector/dist/vector"
#define AI_EXT "../sqlite-ai/dist/ai"
#define GITHUB_MCP_URL "http://localhost:8000/mcp"
#define GGUF_PATH "./models/qwen2.5-coder-7b-instruct-q4_k_m.gguf"
#define DB_PATH "github.db"
#define MAX_ITERATIONS 20

static void print_separator(void) {
    printf("--------------------------------------------------------------------\n");
}

static int exec_simple(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return rc;
}

int main(void) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    printf("\n");
    printf("GitHub Demo: AI-Driven Development Team Analysis\n");
    print_separator();
    printf("\n");
    printf("This demo showcases:\n");
    printf("  1. MCP Extension    - GitHub API data acquisition\n");
    printf("  2. AI Extension     - Development pattern analysis\n");
    printf("  3. Vector Extension - Semantic search on commit history\n");
    printf("  4. LLM Analysis     - Team productivity insights\n");
    printf("  5. Agent System     - Autonomous repository analysis\n");
    printf("\n");

    // Check GitHub token
    const char *github_token = getenv("GITHUB_TOKEN");
    if (!github_token || strlen(github_token) == 0) {
        fprintf(stderr, "Error: GITHUB_TOKEN environment variable not set\n\n");
        fprintf(stderr, "Please set your GitHub Personal Access Token:\n");
        fprintf(stderr, "  export GITHUB_TOKEN=\"ghp_your_token_here\"\n\n");
        fprintf(stderr, "You can create a token at:\n");
        fprintf(stderr, "  https://github.com/settings/tokens\n\n");
        fprintf(stderr, "Required scopes: repo, read:user, read:org\n\n");
        return 1;
    }

    printf("  * GitHub token loaded (prefix: %.20s...)\n\n", github_token);

    // Clean up old database
    remove(DB_PATH);

    // Open database
    rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_enable_load_extension(db, 1);

    // STEP 1: Load extensions
    print_separator();
    printf("STEP 1: Loading SQLite Extensions\n");
    print_separator();

    rc = sqlite3_load_extension(db, AGENT_EXT, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to load agent extension: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    printf("  * Agent extension loaded\n");

    rc = sqlite3_load_extension(db, MCP_EXT, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to load MCP extension: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    printf("  * MCP extension loaded\n");

    rc = sqlite3_load_extension(db, VEC_EXT, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to load Vector extension: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    printf("  * Vector extension loaded\n");

    rc = sqlite3_load_extension(db, AI_EXT, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to load AI extension: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    printf("  * AI extension loaded\n\n");

    // STEP 2: Load LLM model
    print_separator();
    printf("STEP 2: Loading LLM Model\n");
    print_separator();

    rc = sqlite3_prepare_v2(db,
        "SELECT llm_model_load('"GGUF_PATH"', 'gpu_layers=99')",
        -1, &stmt, 0);
    if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        fprintf(stderr, "Error: Failed to load model: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_finalize(stmt);
    printf("  * Model loaded: "GGUF_PATH"\n\n");

    // STEP 3: Connect to GitHub MCP server
    print_separator();
    printf("STEP 3: Connect to GitHub MCP Server\n");
    print_separator();

    // Create headers JSON with authorization
    char headers_json[512];
    snprintf(headers_json, sizeof(headers_json),
             "{\"Authorization\": \"Bearer %s\", \"X-MCP-Readonly\": \"true\"}",
             github_token);

    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_connect(?, ?)",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare connect statement: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    sqlite3_bind_text(stmt, 1, GITHUB_MCP_URL, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, headers_json, -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "Error: Failed to connect to MCP: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }

    const char *connect_result = (const char *)sqlite3_column_text(stmt, 0);
    if (connect_result && strstr(connect_result, "error") != NULL) {
        fprintf(stderr, "Error: MCP connection failed: %s\n", connect_result);
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
    printf("  * Connected to GitHub MCP server\n");

    // STEP 4: Create table and run agent
    print_separator();
    printf("STEP 4: Create Table and Run Agent\n");
    print_separator();

    rc = sqlite3_exec(db,
        "CREATE TABLE team_activity ("
        "  id INTEGER PRIMARY KEY,"
        "  username TEXT,"
        "  repository TEXT,"
        "  activity_type TEXT,"
        "  title TEXT,"
        "  description TEXT,"
        "  timestamp TEXT,"
        "  url TEXT,"
        "  user_embedding BLOB,"
        "  repo_embedding BLOB,"
        "  activity_embedding BLOB"
        ")",
        0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to create table: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    printf("  * Created team_activity table\n\n");

    printf("  Running AI agent to analyze SQLiteAI team activity...\n");
    printf("  (This may take 60-90 seconds as the agent queries GitHub API)\n\n");

    const char *analysis_goal =
        "Use search_repositories tool with query 'user:sqliteai' and sort 'updated' to find the 2 most recently updated repositories. "
        "Then for each repository, use list_commits tool with owner 'sqliteai' and the repo name to get 5 recent commits. "
        "Insert into team_activity table with these exact fields: username (author.name or author.login), repository (repo name), "
        "activity_type (always 'commit'), title (commit.message), description (commit.message), "
        "timestamp (commit.author.date), url (commit.html_url). "
        "Parse all JSON responses carefully and extract real values, never insert template strings like {{variable}}.";

    rc = sqlite3_prepare_v2(db, "SELECT agent_run(?, 'team_activity', ?)", -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare agent: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    sqlite3_bind_text(stmt, 1, analysis_goal, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, MAX_ITERATIONS);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        fprintf(stderr, "Error: Agent execution failed: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    int rows_inserted = sqlite3_column_int(stmt, 0);
    printf("  * Inserted %d rows into team_activity\n", rows_inserted);
    sqlite3_finalize(stmt);

    // Get activity count
    int activity_count = 0;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM team_activity", -1, &stmt, 0);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        activity_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (activity_count == 0) {
        fprintf(stderr, "Error: No activities were extracted\n");
        return 1;
    }
    printf("  * Total activities stored: %d\n\n", activity_count);

    // STEP 5: View collected data
    print_separator();
    printf("STEP 5: View Team Activity Summary\n");
    print_separator();

    printf("\nAgent automatically handled:\n");
    printf("  1. GitHub API data fetching via MCP\n");
    printf("  2. LLM-based commit message parsing\n");
    printf("  3. Multi-embedding generation (user, repo, activity)\n");
    printf("  4. Vector index initialization for semantic search\n\n");

    // Show activity by repository
    printf("Most Active Repositories:\n\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT repository, COUNT(*) as activity_count "
        "FROM team_activity "
        "WHERE repository IS NOT NULL "
        "GROUP BY repository "
        "ORDER BY activity_count DESC "
        "LIMIT 5",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        int rank = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *repo = (const char *)sqlite3_column_text(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            printf("  %d. %s - %d activities\n", rank++, repo ? repo : "(unknown)", count);
        }
        printf("\n");
    }
    sqlite3_finalize(stmt);

    // Show activity by user
    printf("Most Active Users:\n\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT username, COUNT(*) as activity_count "
        "FROM team_activity "
        "WHERE username IS NOT NULL "
        "GROUP BY username "
        "ORDER BY activity_count DESC "
        "LIMIT 10",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        int rank = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *username = (const char *)sqlite3_column_text(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            printf("  %d. %s - %d activities\n", rank++, username ? username : "(unknown)", count);
        }
        printf("\n");
    }
    sqlite3_finalize(stmt);

    // STEP 6: Semantic search
    print_separator();
    printf("STEP 6: Semantic Search on Development Activity\n");
    print_separator();

    printf("\nQuery 1: Activity search - \"bug fixes and improvements\"\n");
    printf("         Using activity_embedding (activity_type + title + description)\n\n");

    exec_simple(db, "SELECT llm_context_create_embedding('embedding_type=FLOAT32')");

    rc = sqlite3_prepare_v2(db,
        "SELECT t.username, t.repository, t.title, t.description, t.timestamp, v.distance "
        "FROM vector_full_scan('team_activity', 'activity_embedding', "
        "  llm_embed_generate('bug fixes improvements enhancements features', ''), 3) AS v "
        "JOIN team_activity t ON t.rowid = v.rowid "
        "ORDER BY v.distance ASC",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        int rank = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *username = (const char *)sqlite3_column_text(stmt, 0);
            const char *repository = (const char *)sqlite3_column_text(stmt, 1);
            const char *title = (const char *)sqlite3_column_text(stmt, 2);
            const char *description = (const char *)sqlite3_column_text(stmt, 3);
            const char *timestamp = (const char *)sqlite3_column_text(stmt, 4);
            double distance = sqlite3_column_double(stmt, 5);

            printf("  %d. [%s] %s\n", rank++,
                repository ? repository : "(unknown)",
                title ? title : "(no title)");
            printf("     Author: %s | Date: %s\n",
                username ? username : "(unknown)",
                timestamp ? timestamp : "(no date)");
            if (description && strlen(description) > 0 && strcmp(description, title ? title : "") != 0) {
                printf("     Details: %.80s%s\n", description,
                    strlen(description) > 80 ? "..." : "");
            }
            printf("     Similarity: %.3f\n\n", 1.0 - distance);
        }
    } else {
        fprintf(stderr, "Error: Activity search failed: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    printf("Query 2: Repository search - \"extension development\"\n");
    printf("         Using repo_embedding (repository + activity_type)\n\n");

    rc = sqlite3_prepare_v2(db,
        "SELECT t.username, t.repository, t.title, t.description, t.timestamp, v.distance "
        "FROM vector_full_scan('team_activity', 'repo_embedding', "
        "  llm_embed_generate('extension development sqlite database', ''), 3) AS v "
        "JOIN team_activity t ON t.rowid = v.rowid "
        "ORDER BY v.distance ASC",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        int rank = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *username = (const char *)sqlite3_column_text(stmt, 0);
            const char *repository = (const char *)sqlite3_column_text(stmt, 1);
            const char *title = (const char *)sqlite3_column_text(stmt, 2);
            const char *description = (const char *)sqlite3_column_text(stmt, 3);
            const char *timestamp = (const char *)sqlite3_column_text(stmt, 4);
            double distance = sqlite3_column_double(stmt, 5);

            printf("  %d. [%s] %s\n", rank++,
                repository ? repository : "(unknown)",
                title ? title : "(no title)");
            printf("     Author: %s | Date: %s\n",
                username ? username : "(unknown)",
                timestamp ? timestamp : "(no date)");
            if (description && strlen(description) > 0 && strcmp(description, title ? title : "") != 0) {
                printf("     Details: %.80s%s\n", description,
                    strlen(description) > 80 ? "..." : "");
            }
            printf("     Similarity: %.3f\n\n", 1.0 - distance);
        }
    } else {
        fprintf(stderr, "Error: Repository search failed: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    // STEP 7: LLM analysis with retrieved context
    print_separator();
    printf("STEP 7: LLM Analysis with Retrieved Context\n");
    print_separator();

    printf("\nQuestion: \"What are the main development focuses of the SQLiteAI team?\"\n\n");

    // Retrieve context with recent activity details
    char context[4096] = "Recent SQLiteAI team activity:\n\n";
    rc = sqlite3_prepare_v2(db,
        "SELECT t.username, t.repository, t.title, t.description, t.timestamp "
        "FROM vector_full_scan('team_activity', 'activity_embedding',"
        "  llm_embed_generate('development focus priorities main themes', ''), 10) AS v "
        "JOIN team_activity t ON t.rowid = v.rowid "
        "ORDER BY v.distance ASC",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *username = (const char *)sqlite3_column_text(stmt, 0);
            const char *repository = (const char *)sqlite3_column_text(stmt, 1);
            const char *title = (const char *)sqlite3_column_text(stmt, 2);
            const char *description = (const char *)sqlite3_column_text(stmt, 3);
            const char *timestamp = (const char *)sqlite3_column_text(stmt, 4);

            char activity_text[1024];
            snprintf(activity_text, sizeof(activity_text),
                "- Repository: %s\n"
                "  Author: %s | Date: %s\n"
                "  Commit: %s\n"
                "  Description: %s\n\n",
                repository ? repository : "(unknown)",
                username ? username : "(unknown)",
                timestamp ? timestamp : "(no date)",
                title ? title : "(no title)",
                description ? description : "(no description)");
            strncat(context, activity_text, sizeof(context) - strlen(context) - 1);
        }
    }
    sqlite3_finalize(stmt);

    // Ask LLM with context
    exec_simple(db, "SELECT llm_context_create_chat()");

    char prompt[5120];
    snprintf(prompt, sizeof(prompt),
        "%s\n"
        "Based on the above commit activity, what are the main development focuses "
        "of the SQLiteAI team? Summarize the key themes and priorities in 2-3 sentences.",
        context);

    rc = sqlite3_prepare_v2(db, "SELECT llm_chat_respond(?)", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_STATIC);

    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *answer = (const char *)sqlite3_column_text(stmt, 0);
        printf("LLM Response:\n%s\n\n", answer ? answer : "(no response)");
    }
    sqlite3_finalize(stmt);

    // Summary
    print_separator();
    printf("Demo Complete\n");
    print_separator();
    printf("\nSummary:\n");
    printf("  * Team activities analyzed via GitHub API: %d\n", activity_count);
    printf("  * Vector searches performed: 2\n");
    printf("  * LLM queries answered: 1\n\n");
    printf("This demonstrates:\n");
    printf("  1. MCP for GitHub data acquisition\n");
    printf("  2. AI for development pattern analysis & embeddings\n");
    printf("  3. Vector for semantic search on commit history\n");
    printf("  4. Agent for autonomous repository analysis\n");
    printf("  5. LLM for team productivity insights\n");
    printf("\n");

    // Cleanup
    sqlite3_exec(db, "SELECT llm_context_free()", 0, 0, 0);
    sqlite3_exec(db, "SELECT llm_model_free()", 0, 0, 0);
    sqlite3_close(db);

    return 0;
}
