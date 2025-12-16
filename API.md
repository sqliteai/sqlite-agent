# SQLite Agent API Reference

## Dependencies

**Required:**
- [sqlite-mcp](https://github.com/sqliteai/sqlite-mcp) – MCP client for tool access
- [sqlite-ai](https://github.com/sqliteai/sqlite-ai) – LLM inference and embeddings

**Optional:**
- [sqlite-vector](https://github.com/sqliteai/sqlite-vector) – For vector search (auto-enabled in table mode)

---

## Functions

### `agent_version()`

Returns the extension version.

**Syntax:**
```sql
SELECT agent_version();
```

**Returns:** `TEXT` – Version string (e.g., "0.1.0")

**Example:**
```sql
SELECT agent_version();
-- 0.1.0
```

---

### `agent_run()`

Runs an autonomous AI agent that uses MCP tools to accomplish a goal.

**Syntax:**
```sql
-- MODE 1: Text response
SELECT agent_run(goal);
SELECT agent_run(goal, max_iterations);

-- MODE 2: Table extraction
SELECT agent_run(goal, table_name);
SELECT agent_run(goal, table_name, max_iterations);
SELECT agent_run(goal, table_name, max_iterations, system_prompt);
```

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `goal` | TEXT | Yes | - | Task or goal for the agent |
| `table_name` | TEXT | No | NULL | Target table (NULL for text mode) |
| `max_iterations` | INTEGER | No | 5 | Maximum iterations |
| `system_prompt` | TEXT | No | NULL | Custom system prompt |

**Returns:**
- **MODE 1 (text):** TEXT – Agent's final response  
- **MODE 2 (table):** INTEGER – Number of rows inserted into the table

**MODE 1: Text Response**

Agent returns text after using tools.

```sql
SELECT agent_run('Find affordable apartments in Rome', 8);
```

Returns:
```
I found several affordable apartments in Rome:
1. Bright Studio - €85/night
2. Cozy Room - €72/night
3. Modern Apartment - €95/night
```

**MODE 2: Table Extraction**

Agent fetches data and populates a table.

```sql
CREATE TABLE listings (
  id INTEGER PRIMARY KEY,
  title TEXT,
  price REAL,
  embedding BLOB
);

SELECT agent_run('Find apartments in Rome under 100 EUR', 'listings', 8);
-- Returns: "Inserted 5 rows into listings"
```

**Auto-Features (MODE 2):**

1. **Schema Inspection** – Reads table schema to understand target data structure
2. **Structured Extraction** – Extracts data matching column names and types
3. **Transaction Safety** – Wraps all insertions in BEGIN/COMMIT
4. **Auto-Embeddings** – Generates embeddings for BLOB columns named `*_embedding`
5. **Auto-Vector Index** – Initializes vector indices when embeddings are created

**Custom System Prompt:**

```sql
SELECT agent_run(
  'Find vegan restaurants',
  'restaurants',
  10,
  'You are a restaurant finder. Focus on highly-rated establishments.'
);
```

---

## Error Handling

**Common Errors:**

```sql
-- Not connected to MCP
SELECT agent_run('Find apartments', 5);
-- Error: Not connected. Call mcp_connect() first

-- Table doesn't exist
SELECT agent_run('Find apartments', 'nonexistent', 5);
-- Error: Table does not exist or has no columns

-- LLM not loaded (table mode)
SELECT agent_run('Find apartments', 'listings', 5);
-- Error: Failed to create LLM chat context
```

**Checking for Errors:**

```sql
SELECT
  CASE
    WHEN result LIKE '%error%' OR result LIKE '%ERROR%'
    THEN 'Error occurred'
    ELSE 'Success'
  END
FROM (SELECT agent_run('Find apartments', 5) as result);
```

---

## Performance

**Timing:**
- **Agent Iterations**: 100ms-10s per iteration (LLM inference)
- **Tool Calls**: 10-1000ms (network latency)
- **Embedding Generation**: 10-100ms per text (model-dependent)
- **Vector Index**: <100ms (initialization)

**Tips:**
- Use appropriate `max_iterations` (default: 5)
- Reuse MCP connections (global client persists)
- Use the Agent in a separated thread

---

## See Also

- [USAGE.md](USAGE.md) – Usage guide and examples
- [README.md](README.md) – Project overview
- [sqlite-mcp API](https://github.com/sqliteai/sqlite-mcp/blob/main/API.md) – MCP extension API
- [sqlite-ai API](https://github.com/sqliteai/sqlite-ai/blob/main/API.md) – AI extension API
- [sqlite-vector API](https://github.com/sqliteai/sqlite-vector/blob/main/API.md) – Vector extension API
