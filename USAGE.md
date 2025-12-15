# SQLite Agent Extension Usage Guide

## Overview

The SQLite Agent extension enables autonomous AI agents within SQLite databases. It requires both [sqlite-mcp](https://github.com/sqliteai/sqlite-mcp) for external tool access and [sqlite-ai](https://github.com/sqliteai/sqlite-ai) for LLM inference and embeddings.

Optionally, [sqlite-vector](https://github.com/sqliteai/sqlite-vector) can be loaded to enable automatic vector embeddings and semantic search capabilities when using table extraction mode.

## Quick Start

### 1. Load Extensions

The agent extension requires both sqlite-mcp and sqlite-ai:

```sql
-- Load required extensions (in order)
.load ../sqlite-mcp/dist/mcp
.load ../sqlite-ai/dist/ai
.load ./dist/agent

-- Optional: Load sqlite-vector for automatic embeddings and semantic search
.load ../sqlite-vector/dist/vector

-- Verify versions
SELECT mcp_version();    -- 0.1.4
SELECT agent_version();  -- 0.1.0
```

### 2. Initialize

Load an AI model and connect to an MCP server:

```sql
-- Load AI model (required)
SELECT llm_model_load('/path/to/model.gguf', 'gpu_layers=99');

-- Connect to MCP server (required)
SELECT mcp_connect('http://localhost:8000/mcp');
```

### 3. Run Agent

```sql
-- Simple text response
SELECT agent_run('Find affordable apartments in Rome', 5);

-- Table extraction with auto-features
CREATE TABLE listings (id INTEGER, title TEXT, price REAL, embedding BLOB);
SELECT agent_run('Find apartments in Rome', 'listings', 8);
```

## Two Modes of Operation

### MODE 1: Text Response

Returns agent's text answer after using tools.

**Use when:**
- You want a conversational response
- You need quick answers without data storage
- You're prototyping or exploring

**Example:**
```sql
SELECT agent_run(
  'Find 3 affordable apartments in Rome with AC under 100 EUR',
  8
);
```

**Response:**
```
I found several options:
1. Bright Studio - €85/night, AC, WiFi
2. Cozy Room in Trastevere - €72/night, AC, WiFi
3. Modern Apartment - €95/night, AC, WiFi, Parking
```

### MODE 2: Table Extraction

Fetches data and populates a database table.

**Use when:**
- You want to store structured data
- You need semantic search capabilities
- You're building a data-driven application

**Example:**
```sql
CREATE TABLE listings (
  id INTEGER PRIMARY KEY,
  title TEXT,
  location TEXT,
  price REAL,
  amenities TEXT,
  embedding BLOB
);

SELECT agent_run(
  'Find affordable apartments in Rome under 100 EUR',
  'listings',
  8
);
-- Returns: "Inserted 5 rows into listings"
```

## Auto-Features in Table Mode

When using table extraction mode, the agent automatically:

### 1. Schema Inspection
Inspects table columns to understand what data to fetch:
```sql
CREATE TABLE listings (
  id INTEGER,
  title TEXT,
  price REAL,
  location TEXT
);

-- Agent knows to fetch: id, title, price, location
```

### 2. Structured Extraction
Uses LLM to extract data matching your schema:
```sql
-- Agent extracts structured JSON matching columns
-- Handles type conversion automatically
```

### 3. Auto-Embeddings (Requires sqlite-vector)
Generates embeddings for BLOB columns named `*_embedding`:
```sql
CREATE TABLE listings (
  title TEXT,
  description TEXT,
  title_embedding BLOB,      -- Auto-embedded from 'title'
  description_embedding BLOB  -- Auto-embedded from 'description'
);

SELECT agent_run('Find apartments', 'listings', 8);
-- Automatically generates embeddings if sqlite-vector is loaded!
```

### 4. Auto-Vector Index (Requires sqlite-vector)
Initializes vector search indices:
```sql
-- After generating embeddings, automatically runs (if sqlite-vector is loaded):
-- SELECT vector_init('listings', 'title_embedding', ...)
-- SELECT vector_init('listings', 'description_embedding', ...)
```

### 5. Transaction Safety
All insertions wrapped in a transaction:
```sql
-- BEGIN TRANSACTION
-- INSERT INTO listings ...
-- INSERT INTO listings ...
-- COMMIT
-- (or ROLLBACK on error)
```

## Complete Workflow Example

### Airbnb RAG Workflow

```sql
-- 1. Load extensions
.load ../sqlite-mcp/dist/mcp
.load ./dist/agent
.load ../sqlite-ai/dist/ai
.load ../sqlite-vector/dist/vector

-- 2. Initialize (one-time)
SELECT llm_model_load('/path/to/model.gguf', 'gpu_layers=99');
SELECT mcp_connect('http://localhost:8000/mcp');

-- 3. Create table
CREATE TABLE listings (
  id INTEGER PRIMARY KEY,
  title TEXT,
  description TEXT,
  location TEXT,
  property_type TEXT,
  amenities TEXT,
  price REAL,
  rating REAL,
  embedding BLOB
);

-- 4. Run agent to populate table
-- Auto-generates embeddings and vector index (requires sqlite-vector)!
SELECT agent_run(
  'Find affordable apartments in Rome under 100 EUR per night',
  'listings',
  8
);
-- Returns: "Inserted 5 rows into listings"

-- 5. Semantic search (works immediately!)
SELECT title, location, price, v.distance
FROM vector_full_scan('listings', 'embedding',
  llm_embed_generate('cozy modern apartment', ''), 5) v
JOIN listings l ON l.rowid = v.rowid
ORDER BY v.distance ASC;

-- 6. RAG: Answer questions
SELECT llm_context_create_chat();
SELECT llm_chat_respond(
  'Based on these listings: ' || (
    SELECT group_concat(title || ' - €' || price, '; ')
    FROM listings
  ) || '. Which is best for families?'
);
```

## Custom System Prompts

Override the default agent behavior:

```sql
SELECT agent_run(
  'Find vegan restaurants',
  'restaurants',
  10,
  'You are a helpful restaurant finder.
   Focus on highly-rated establishments with good reviews.
   Extract: name, cuisine, rating, price_range, address'
);
```

## Error Handling

### Common Errors

**1. Not Connected to MCP**
```sql
SELECT agent_run('Find apartments', 5);
-- Error: Not connected. Call mcp_connect() first
```

**Solution:**
```sql
SELECT mcp_connect('http://localhost:8000/mcp');
```

**2. LLM Not Loaded**
```sql
SELECT agent_run('Find apartments', 'listings', 5);
-- Error: Failed to create LLM chat context
```

**Solution:**
```sql
SELECT llm_model_load('/path/to/model.gguf', 'gpu_layers=99');
```

**3. Table Does Not Exist**
```sql
SELECT agent_run('Find apartments', 'nonexistent', 5);
-- Error: Table does not exist or has no columns
```

**Solution:**
```sql
CREATE TABLE nonexistent (id INTEGER, title TEXT);
```

### Checking for Errors

```sql
SELECT
  CASE
    WHEN result LIKE '%error%' OR result LIKE '%ERROR%'
    THEN 'Error: ' || result
    ELSE 'Success'
  END
FROM (SELECT agent_run('Find apartments', 5) as result);
```

## Performance Tips

### 1. Use Appropriate Iterations

```sql
-- Simple tasks: 3-5 iterations
SELECT agent_run('Find apartments in Rome', 3);

-- Complex tasks: 8-10 iterations
SELECT agent_run('Find apartments, get details, filter by amenities', 10);
```

### 2. Reuse Connections

```sql
-- Connect once
SELECT mcp_connect('http://localhost:8000/mcp');

-- Run multiple agents (connection persists)
SELECT agent_run('Find apartments in Rome', 'rome_listings', 5);
SELECT agent_run('Find apartments in Paris', 'paris_listings', 5);
```

### 3. Cache Embeddings (Requires sqlite-vector)

```sql
-- For static data, embeddings are cached in the BLOB column
-- No need to regenerate unless data changes
UPDATE listings SET title = 'New Title' WHERE id = 1;
-- Run agent again to refresh embeddings (if sqlite-vector is loaded)
```

## Debugging

Enable debug output by setting `AGENT_DEBUG=1` in the source:

```c
// In src/sqlite-agent.c
#define AGENT_DEBUG 1
```

Then rebuild:
```bash
make clean && make
```

Debug output will show:
- Agent iterations
- Tool calls and arguments
- LLM responses
- Embedding generation
- Vector index initialization

## Related Documentation

- [API.md](API.md) – Complete API reference
- [README.md](README.md) – Project overview
- [sqlite-mcp API](https://github.com/sqliteai/sqlite-mcp/blob/main/API.md) – MCP extension API
- [sqlite-ai API](https://github.com/sqliteai/sqlite-ai/blob/main/API.md) – AI extension API
- [sqlite-vector API](https://github.com/sqliteai/sqlite-vector/blob/main/API.md) – Vector extension API

## Support

- [GitHub Issues](https://github.com/sqliteai/sqlite-agent/issues)
- [SQLite Extension Load Guide](https://github.com/sqliteai/sqlite-extensions-guide)