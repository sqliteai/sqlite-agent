# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project uses [Semantic Versioning](https://semver.org/); while the major
version is 0, breaking changes are released in the minor version.

Releases before 0.2.0 predate this file; see the
[GitHub releases](https://github.com/sqliteai/sqlite-agent/releases) for those.

## [0.2.0]

A correctness and hardening release. Three of the changes alter existing
behaviour — see **Breaking changes** before upgrading.

### Breaking changes

- `agent_run()` is now registered with `SQLITE_DIRECTONLY`, so it can only be
  called from SQL the application issues directly. Calls from a VIEW, TRIGGER,
  CHECK constraint, generated column or index expression now fail with
  `unsafe use of agent_run()`. The function makes network calls and writes rows;
  without this restriction, querying a table in a database file prepared by
  somebody else could trigger those side effects on the caller's behalf. If you
  need the result in a view, materialise it into a table first.
- `max_iterations` must be between 1 and 100. Values outside that range are
  rejected instead of being accepted. `0` and negative values previously caused
  a division by zero; large values allowed a single statement to spend an
  unbounded amount of time and money against the LLM and the MCP server.
- A tool the model asks for is invoked only if the MCP server advertised it in
  `mcp_list_tools_respond`. Previously any name the model produced was passed
  straight through.

### Security

- Text coming from the MCP server — tool names, descriptions, input schemas and
  tool results — is now fenced in the prompt with a delimiter randomised per
  `agent_run()` call, and the model is told to treat the contents as data. An
  instruction injected into content a tool fetched can no longer escape into the
  instruction region. See "Security model" in [API.md](API.md).
- Identifiers interpolated into SQL are quoted with `%w` (`PRAGMA table_info`,
  `INSERT`, the embedding `UPDATE`) and string literals with `%q`
  (`vector_init`). A table or column name containing a quote or a space can
  neither break the statement nor inject SQL.
- Apple signing secrets are no longer exposed to workflow runs on every branch:
  the keychain, codesign and notarisation steps are restricted to `main`.
  Secrets are passed through `env` instead of being interpolated into shell
  script bodies, and the keychain cleanup now runs even when a prior step fails.
- GitHub Actions are pinned to commit SHAs rather than mutable tags, workflow
  permissions default to `contents: read` with the release job widening them for
  itself, and the SQLite amalgamation downloaded during the Android test is
  verified against a pinned SHA-256 before being compiled.
- Builds enable `-fstack-protector-strong` and `_FORTIFY_SOURCE=2`, plus full
  RELRO and a non-executable stack on Linux and Android and NX/ASLR on Windows.
  (macOS already defaulted to the first two.)

### Fixed

- Fixed a stack buffer overflow when extracting into a table with roughly 22 or
  more columns: the `INSERT` statement was assembled with unbounded `strcat()`
  into two fixed 512-byte buffers. The statement is now built once per call with
  `sqlite3_str`, which grows on demand. The same unbounded concatenation is
  fixed in the embedding column mapping and the embedding `UPDATE`.
- `agent_run()` no longer destroys the caller's transaction. It used
  `BEGIN`/`COMMIT`/`ROLLBACK` with the return codes ignored; because the function
  runs while one of the caller's statements is already executing, the inner
  `BEGIN` failed whenever the caller had a transaction open and the matching
  `ROLLBACK` then discarded the caller's uncommitted work. It now uses a
  `SAVEPOINT`, which nests safely and leaves the caller in control.
- Reduced the stack frame of `agent_run()` from about 155 KB to about 6 KB by
  moving its scratch buffers to the heap. The old frame exceeded the 128 KB
  default thread stack on musl, so the extension could overflow the stack on any
  non-main thread.
- Fixed four leaks of the 64 KB tool list on error paths in table mode.
- JSON objects in the extracted payload are now delimited by matching braces
  while honouring nesting and string literals. Previously the first `}` ended
  the object, so every field after a nested object was lost and subsequent rows
  were mis-split; a nested key could also shadow the row's own, and a value
  containing an escaped quote was truncated. Escape sequences, including
  `\uXXXX` and surrogate pairs, are now decoded.
- Column values are bound according to SQLite's type affinity rules rather than
  an exact match on `INTEGER` or `REAL`. Columns declared `INT`, `BIGINT`,
  `FLOAT`, `DOUBLE`, `NUMERIC` or `VARCHAR(n)` previously took the string path,
  where an unquoted JSON number was never bound at all and the column ended up
  `NULL`. JSON booleans now bind as 1 and 0 in numeric columns.
- Tool results are fed back to the model in text mode. The system prompt was
  rebuilt and re-sent unchanged on every iteration, so the model never saw what
  a tool returned and could not chain calls or use the data in its answer.
- Tool arguments are delimited with a string-aware scan, so a `}` inside a JSON
  string value no longer truncates the arguments sent to the tool.
- The per-tool truncation budget is now capped by the size of the buffer that
  holds the message, and the limit is applied through `%.*s` rather than a
  format string built at runtime. The computed budget previously had no effect
  above 16 KB.
- The Node package works when imported as ESM. `getExtensionPath()` always threw
  `ExtensionNotFoundError` from the ESM build, with a message claiming the
  platform package was not installed, because the bundler rewrites a bare
  `require()` into a stub that throws. It now derives a `require` from
  `__filename` or `import.meta.url`, whichever the build provides.
- `generate-platform-packages.js` fails instead of reporting success when a
  platform binary is missing, so a package without its binary can no longer be
  published and burn the version number on npm.
- `package-lock.json` is tracked, pinning the toolchain that builds the
  published bundle.

### Added

- A "Security model" section in [API.md](API.md) describing what the agent
  trusts, what it does not, and the limits of the controls above.
