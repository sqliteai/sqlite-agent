# SQLite Agent

**SQLite Agent** is a cross-platform SQLite extension that brings autonomous AI agents to your database. It enables databases to run multi-step, tool-using agents that fetch data from external sources (web, APIs, etc.) through [sqlite-mcp](https://github.com/sqliteai/sqlite-mcp) and populate database tables. The extracted data can then be analyzed by LLMs, displayed in UIs, or used in applications.

## Highlights

* **Autonomous Agents** – Run AI agents that can plan and execute multi-step tasks
* **MCP Integration** – Works with [sqlite-mcp](https://github.com/sqliteai/sqlite-mcp) for external tool access
* **AI Powered** – Uses [sqlite-ai](https://github.com/sqliteai/sqlite-ai) for LLM inference and embeddings
* **Table Extraction Mode** – Automatically fetch and populate database tables
* **Auto-Embeddings** – Optionally uses [sqlite-vector](https://github.com/sqliteai/sqlite-vector) to generate vector embeddings automatically when inserting data for BLOB columns named `*_embedding`
* **Auto-Vector Indexing** – Initialize vector search indices automatically

## 🚀 Quick Start

### Installation

#### Pre-built Binaries

Download the appropriate pre-built binary for your platform from the [Releases](https://github.com/sqliteai/sqlite-agent/releases) page: we do support **Linux**, **macOS**, **Windows**, **Android** and **iOS**.

### Basic Usage

```sql
-- Load required extensions
SELECT load_extension('./dist/mcp');
SELECT load_extension('./dist/ai');
SELECT load_extension('./dist/agent');

-- Initialize AI model
SELECT llm_model_load('/path/to/model.gguf', 'gpu_layers=99');

-- Connect to MCP server
SELECT mcp_connect('http://localhost:8000/mcp');

-- Run an autonomous agent (text-only mode)
SELECT agent_run('Find affordable apartments in Rome with AC', 8);
-- Agent uses MCP tools to accomplish the task and returns result
```

## 📖 API Reference

### Available Functions

| Function | Description |
|----------|-------------|
| `agent_version()` | Returns extension version |
| `agent_run(goal, [table_name], [max_iterations], [system_prompt])` | Run autonomous AI agent |

See [API.md](API.md) for complete API documentation with examples.

## Example: AirBnb MCP Server

This example demonstrates combining agent, MCP, AI, and vector extensions:

```sql
-- Load all extensions
.load ../sqlite-mcp/dist/mcp
.load ./dist/agent
.load ../sqlite-ai/dist/ai
.load ../sqlite-vector/dist/vector

-- Initialize AI model (one-time)
SELECT llm_model_load('/path/to/model.gguf', 'gpu_layers=99');

-- Connect to MCP server (one-time)
SELECT mcp_connect('http://localhost:8000/mcp');

-- Create table with embedding column
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

-- Run agent to fetch and populate data
-- Embeddings and vector index are created automatically!
SELECT agent_run(
  'Find affordable apartments in Rome under 100 EUR per night',
  'listings',
  8
);
-- Returns: "Inserted 5 rows into listings"

-- Semantic search (works immediately!)
SELECT title, location, price, v.distance
FROM vector_full_scan('listings', 'embedding',
  llm_embed_generate('cozy modern apartment', ''), 5) v
JOIN listings l ON l.rowid = v.rowid
ORDER BY v.distance ASC;
```

Run the full demo:
```bash
make airbnb
```
See [USAGE.md](USAGE.md) for complete usage examples.

## How It Works

The agent operates through an iterative loop:

1. Receives goal and available tools from MCP
2. Decides which tool to call
3. Executes tool via sqlite-mcp extension
4. Receives tool result
5. Continues until goal achieved or max iterations reached
6. Returns final result

## Related Projects

- [sqlite-mcp](https://github.com/sqliteai/sqlite-mcp) – Model Context Protocol client for SQLite
- [sqlite-ai](https://github.com/sqliteai/sqlite-ai) – AI/ML inference and embeddings for SQLite
- [sqlite-vector](https://github.com/sqliteai/sqlite-vector) – Vector search capabilities for SQLite
- [sqlite-sync](https://github.com/sqliteai/sqlite-sync) – Cloud synchronization for SQLite
- [sqlite-js](https://github.com/sqliteai/sqlite-js) – JavaScript engine integration for SQLite

## Support

- [GitHub Issues](https://github.com/sqliteai/sqlite-agent/issues)
- [SQLite Extension Load Guide](https://github.com/sqliteai/sqlite-extensions-guide)

## License

This project is licensed under the [Elastic License 2.0](./LICENSE). You can use, copy, modify, and distribute it under the terms of the license for non-production use. For production or managed service use, please [contact SQLite Cloud, Inc](mailto:info@sqlitecloud.io) for a commercial license.
