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

## Security model

`agent_run()` hands control of what happens next to a language model, so it is
worth being explicit about what is trusted and what is not.

**What is trusted:** the SQL you issue yourself — the goal, the target table and
any custom system prompt — and the MCP server you chose in `mcp_connect()`.

**What is not trusted:** everything that comes back from that server. Tool
names, descriptions and input schemas, and above all tool *results*, which may
contain whatever the tool fetched: a web page, a file, an API response. All of
it is quoted into the model's prompt.

Two controls apply:

- **Untrusted text is fenced.** Each `agent_run()` call mints a random
  delimiter and wraps every block of server-supplied text in it, instructing the
  model to treat the contents as data. Because the delimiter is unpredictable,
  injected text cannot close the fence and escape into the instruction region.
- **Tools are restricted to those advertised.** A tool the model asks for is
  invoked only if it appeared in `mcp_list_tools_respond`. Text telling the
  model to "call tool X" cannot reach a tool the server never offered.

**What these controls do not do.** They do not protect you from a malicious MCP
server: a server you connect to can advertise any tool it likes, and the agent
will use it. Choosing which server to connect to is your trust decision. They
also do not make the model's judgement reliable — a sufficiently persuasive
injected instruction may still get the agent to call an advertised tool with
arguments you would not have chosen. Treat an agent connected to an untrusted
server, or to tools that read untrusted content, as semi-trusted: give the MCP
server the narrowest credentials that still do the job, and prefer read-only
tools where the server supports it.

**`agent_run()` is marked `SQLITE_DIRECTONLY`.** It can only be called from SQL
your application issues directly — never from a VIEW, TRIGGER, CHECK
constraint, generated column or index expression. This matters because the
function makes network calls and writes rows: without the restriction, merely
querying a table in a database file somebody else prepared could trigger those
side effects on your behalf. If you need the result in a view, materialise it
into a table first.

Finally, `max_iterations` bounds how many LLM and tool round trips a single call
can make. It is your cost and runtime ceiling, so set it deliberately. Values
below 1 or above 100 are rejected; the hard cap keeps one SQL statement from
spending an unbounded amount of time and money against the LLM and the MCP
server.

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

-- max_iterations must be between 1 and 100
SELECT agent_run('Find apartments', 'listings', 0);
-- Error: max_iterations must be >= 1
SELECT agent_run('Find apartments', 'listings', 500);
-- Error: max_iterations must be <= 100

-- Called from a VIEW or TRIGGER rather than directly
CREATE VIEW v AS SELECT agent_run('Find apartments', 5);
SELECT * FROM v;
-- Error: unsafe use of agent_run()

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
