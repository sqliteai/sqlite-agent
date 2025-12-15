//
//  airbnb.c
//  sqlite-agent
//
//  Created by Gioele Cantoni on 05/11/25.
//
//  Demonstrates MCP + AI + Vector + Agent extensions working together
//  to fetch, embed, and semantically search real Airbnb listings.
//
//  This example uses threading to demonstrate concurrent database access:
//  - Worker thread: Runs the agent to fetch and store data
//  - Main thread: Monitors progress by polling the database
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <pthread.h>
#include <unistd.h>

#define AGENT_EXT "./dist/agent"
#define MCP_EXT "../sqlite-mcp/dist/mcp"
#define VEC_EXT "../sqlite-vector/dist/vector"
#define AI_EXT "../sqlite-ai/dist/ai"
#define GGUF_PATH "./models/qwen2.5-coder-7b-instruct-q4_k_m.gguf"
#define DB_PATH "airbnb.db"
#define MAX_ITERATIONS 15

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

// Worker thread: Performs agent operations
void* worker_thread(void* arg) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    printf("\n");
    printf("Airbnb Demo: MCP + AI + Vector + Agent Extensions\n");
    print_separator();
    printf("\n");
    printf("This demo showcases:\n");
    printf("  1. MCP Extension    - Fetch real Airbnb listings via AI agent\n");
    printf("  2. AI Extension     - Generate embeddings for each listing\n");
    printf("  3. Vector Extension - Semantic search and ranking\n");
    printf("  4. LLM Analysis     - Answer questions about the data\n");
    printf("  5. Threading        - Concurrent database access\n");
    printf("\n");

    // Open database connection in worker thread
    rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Cannot open database: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    sqlite3_enable_load_extension(db, 1);

    // STEP 1: Load extensions
    print_separator();
    printf("STEP 1: Loading SQLite Extensions\n");
    print_separator();

    rc = sqlite3_load_extension(db, AGENT_EXT, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to load agent extension: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    printf("  * Agent extension loaded\n");

    rc = sqlite3_load_extension(db, MCP_EXT, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to load MCP extension: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    printf("  * MCP extension loaded\n");

    rc = sqlite3_load_extension(db, VEC_EXT, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to load Vector extension: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    printf("  * Vector extension loaded\n");

    rc = sqlite3_load_extension(db, AI_EXT, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to load AI extension: %s\n", sqlite3_errmsg(db));
        return NULL;
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
        return NULL;
    }
    sqlite3_finalize(stmt);
    printf("  * Model loaded: "GGUF_PATH"\n\n");

    // STEP 3: Connect to MCP server
    print_separator();
    printf("STEP 3: Connect to Airbnb MCP Server\n");
    print_separator();

    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_connect('http://localhost:8500/mcp')",
        -1, &stmt, 0);
    if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        fprintf(stderr, "Error: Failed to connect to MCP: %s\n", sqlite3_errmsg(db));
        fprintf(stderr, "Make sure Airbnb MCP server is running:\n");
        fprintf(stderr, "  npx -y supergateway --stdio \"npx @openbnb/mcp-server-airbnb --ignore-robots-txt\" \\\n");
        fprintf(stderr, "    --outputTransport streamableHttp --port 8500\n");
        return NULL;
    }

    const char *connect_result = (const char *)sqlite3_column_text(stmt, 0);
    if (connect_result && strstr(connect_result, "error") != NULL) {
        fprintf(stderr, "Error: MCP connection failed: %s\n", connect_result);
        sqlite3_finalize(stmt);
        return NULL;
    }
    sqlite3_finalize(stmt);
    printf("  * Connected to Airbnb MCP server\n");

    // STEP 4: Create table and run agent
    print_separator();
    printf("STEP 4: Create Table and Run Agent\n");
    print_separator();

    rc = sqlite3_exec(db,
        "CREATE TABLE listings ("
        "  id INTEGER PRIMARY KEY,"
        "  title TEXT,"
        "  description TEXT,"
        "  price REAL,"
        "  rating REAL,"
        "  location TEXT,"
        "  property_type TEXT,"
        "  guests INTEGER,"
        "  bedrooms INTEGER,"
        "  bathrooms INTEGER,"
        "  amenities TEXT,"
        "  url TEXT,"
        "  content_embedding BLOB,"
        "  location_embedding BLOB,"
        "  features_embedding BLOB"
        ")",
        0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to create table: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    printf("  * Created listings table\n\n");

    printf("  Running AI agent to fetch and store listings...\n");
    printf("  (This may take 30-60 seconds as the agent queries Airbnb)\n");
    printf("  (Main thread is monitoring progress concurrently)\n\n");

    const char *search_goal =
        "Search for affordable apartments in Rome under 100 EUR per night. "
        "Make MULTIPLE search calls with different parameters to find at least 5 different listings. "
        "Try different locations within Rome (Trastevere, Centro Storico, Monti, etc.) and different dates. "
        "For each listing found, extract: title, description, price, rating, location, property_type, "
        "guests, bedrooms, bathrooms, amenities (comma-separated), and url.";

    rc = sqlite3_prepare_v2(db, "SELECT agent_run(?, 'listings', ?)", -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare agent: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, search_goal, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, MAX_ITERATIONS);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        fprintf(stderr, "Error: Agent execution failed: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    const char *result = (const char *)sqlite3_column_text(stmt, 0);
    printf("\n  * %s\n", result ? result : "Agent completed");
    sqlite3_finalize(stmt);

    // Get listing count
    int listing_count = 0;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM listings", -1, &stmt, 0);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        listing_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (listing_count == 0) {
        fprintf(stderr, "Error: No listings were extracted\n");
        exit(1);
        return NULL;
    }
    printf("  * Total listings stored: %d\n\n", listing_count);

    // STEP 5: View stored data
    print_separator();
    printf("STEP 5: View Stored Data\n");
    print_separator();

    printf("\nAgent automatically handled:\n");
    printf("  1. Data fetching from MCP server\n");
    printf("  2. LLM-based data extraction into table schema\n");
    printf("  3. Multi-embedding generation (content, location, features)\n");
    printf("  4. Vector index initialization for all embeddings\n\n");

    printf("Listings:\n\n");

    rc = sqlite3_prepare_v2(db,
        "SELECT id, title, location, property_type, price, rating, "
        "guests, bedrooms, bathrooms, url "
        "FROM listings",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        int row_num = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const char *title = (const char *)sqlite3_column_text(stmt, 1);
            const char *location = (const char *)sqlite3_column_text(stmt, 2);
            const char *property_type = (const char *)sqlite3_column_text(stmt, 3);
            double price = sqlite3_column_double(stmt, 4);
            double rating = sqlite3_column_double(stmt, 5);
            int guests = sqlite3_column_int(stmt, 6);
            int bedrooms = sqlite3_column_int(stmt, 7);
            int bathrooms = sqlite3_column_int(stmt, 8);
            const char *url = (const char *)sqlite3_column_text(stmt, 9);

            printf("  %d. %s\n", row_num++, title ? title : "(no title)");
            printf("     Location: %s | Type: %s\n",
                location ? location : "(unknown)",
                property_type ? property_type : "(unknown)");
            printf("     Price: EUR %.0f/night | Rating: %.1f/5.0\n", price, rating);
            printf("     Capacity: %d guests | %d bedrooms | %d bathrooms\n",
                guests, bedrooms, bathrooms);
            if (url && *url) {
                printf("     URL: %s\n", url);
            }
            printf("\n");
        }
    }
    sqlite3_finalize(stmt);

    // STEP 6: Semantic search
    print_separator();
    printf("STEP 6: Semantic Search with Vector Similarity\n");
    print_separator();

    printf("\nQuery 1: Content search - \"cozy modern apartment\"\n");
    printf("         Using content_embedding (title + description)\n\n");

    exec_simple(db, "SELECT llm_context_create_embedding('embedding_type=FLOAT32')");

    rc = sqlite3_prepare_v2(db,
        "SELECT l.title, l.location, l.price, l.rating, v.distance "
        "FROM vector_full_scan('listings', 'content_embedding', "
        "  llm_embed_generate('cozy modern apartment comfortable stylish', ''), 3) AS v "
        "JOIN listings l ON l.rowid = v.rowid "
        "ORDER BY v.distance ASC",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        int rank = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *title = (const char *)sqlite3_column_text(stmt, 0);
            const char *location = (const char *)sqlite3_column_text(stmt, 1);
            double price = sqlite3_column_double(stmt, 2);
            double rating = sqlite3_column_double(stmt, 3);
            double distance = sqlite3_column_double(stmt, 4);

            printf("  %d. %s\n", rank++, title ? title : "(no title)");
            printf("     %s | EUR %.0f/night | Rating: %.1f\n",
                location ? location : "(unknown)", price, rating);
            printf("     Similarity: %.3f\n\n", 1.0 - distance);
        }
    } else {
        fprintf(stderr, "Error: Content search failed: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    printf("Query 2: Location search - \"central Rome near attractions\"\n");
    printf("         Using location_embedding (location + property_type)\n\n");

    rc = sqlite3_prepare_v2(db,
        "SELECT l.title, l.location, l.price, l.rating, v.distance "
        "FROM vector_full_scan('listings', 'location_embedding', "
        "  llm_embed_generate('central Rome near attractions tourist area', ''), 3) AS v "
        "JOIN listings l ON l.rowid = v.rowid "
        "ORDER BY v.distance ASC",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        int rank = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *title = (const char *)sqlite3_column_text(stmt, 0);
            const char *location = (const char *)sqlite3_column_text(stmt, 1);
            double price = sqlite3_column_double(stmt, 2);
            double rating = sqlite3_column_double(stmt, 3);
            double distance = sqlite3_column_double(stmt, 4);

            printf("  %d. %s\n", rank++, title ? title : "(no title)");
            printf("     %s | EUR %.0f/night | Rating: %.1f\n",
                location ? location : "(unknown)", price, rating);
            printf("     Similarity: %.3f\n\n", 1.0 - distance);
        }
    } else {
        fprintf(stderr, "Error: Location search failed: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    // STEP 7: LLM analysis with retrieved context
    print_separator();
    printf("STEP 7: LLM Analysis with Retrieved Context\n");
    print_separator();

    printf("\nQuestion: \"Which apartment is best for families with children?\"\n\n");

    // Retrieve context with listing details
    char context[4096] = "Available apartments:\n\n";
    rc = sqlite3_prepare_v2(db,
        "SELECT l.title, l.location, l.property_type, l.price, l.rating, "
        "l.guests, l.bedrooms, l.bathrooms, l.amenities "
        "FROM vector_full_scan('listings', 'content_embedding',"
        "  llm_embed_generate('family friendly children kids spacious', ''), 3) AS v "
        "JOIN listings l ON l.rowid = v.rowid "
        "ORDER BY v.distance ASC",
        -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *title = (const char *)sqlite3_column_text(stmt, 0);
            const char *location = (const char *)sqlite3_column_text(stmt, 1);
            const char *property_type = (const char *)sqlite3_column_text(stmt, 2);
            double price = sqlite3_column_double(stmt, 3);
            double rating = sqlite3_column_double(stmt, 4);
            int guests = sqlite3_column_int(stmt, 5);
            int bedrooms = sqlite3_column_int(stmt, 6);
            int bathrooms = sqlite3_column_int(stmt, 7);
            const char *amenities = (const char *)sqlite3_column_text(stmt, 8);

            char listing_text[1024];
            snprintf(listing_text, sizeof(listing_text),
                "- %s\n"
                "  Location: %s | Type: %s\n"
                "  Price: EUR %.0f/night | Rating: %.1f\n"
                "  Capacity: %d guests | %d bedrooms | %d bathrooms\n"
                "  Amenities: %s\n\n",
                title ? title : "(no title)",
                location ? location : "(unknown)",
                property_type ? property_type : "(unknown)",
                price, rating, guests, bedrooms, bathrooms,
                amenities ? amenities : "(none)");
            strncat(context, listing_text, sizeof(context) - strlen(context) - 1);
        }
    }
    sqlite3_finalize(stmt);

    // Ask LLM with context
    exec_simple(db, "SELECT llm_context_create_chat()");

    char prompt[4096];
    snprintf(prompt, sizeof(prompt),
        "%s\n"
        "Based on the above apartments, which one would you recommend "
        "for a family with children? Explain why in 2-3 sentences.",
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
    printf("  * Listings fetched via MCP: %d\n", listing_count);
    printf("  * Vector searches performed: 3\n");
    printf("  * LLM queries answered: 1\n\n");
    printf("This demonstrates:\n");
    printf("  1. MCP for data acquisition\n");
    printf("  2. AI for embeddings & analysis\n");
    printf("  3. Vector for semantic search\n");
    printf("  4. Agent for autonomous data collection\n");
    printf("  5. Threading for concurrent database access\n");
    printf("\n");

    // Cleanup
    sqlite3_exec(db, "SELECT llm_context_free()", 0, 0, 0);
    sqlite3_exec(db, "SELECT llm_model_free()", 0, 0, 0);
    sqlite3_close(db);

    return NULL;
}

// Main thread: Monitors progress by polling database
int main(void) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;
    pthread_t thread;
    int listing_count = 0;

    // Clean up old database
    remove(DB_PATH);

    // Open database connection in main thread
    printf("[Main] Opening database connection for monitoring...\n");
    rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[Main] Error: Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Launch worker thread
    printf("[Main] Launching worker thread...\n\n");
    pthread_create(&thread, NULL, worker_thread, NULL);

    // Poll database for progress (demonstrates concurrent access)
    printf("[Main] Monitoring progress from main thread...\n");
    while (listing_count == 0) {
        rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM listings", -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                int current_count = sqlite3_column_int(stmt, 0);
                if (current_count > 0 && current_count != listing_count) {
                    printf("[Main] Progress: %d listing(s) detected\n", current_count);
                }
                listing_count = current_count;
            }
            sqlite3_finalize(stmt);
        }
        sleep(1);
    }

    printf("[Main] Worker thread completed, finalizing...\n\n");
    pthread_join(thread, NULL);

    // Close main thread's database connection
    sqlite3_close(db);
    printf("[Main] Database connection closed.\n");

    return 0;
}
