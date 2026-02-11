# sqlite_agent

SQLite Agent is a cross-platform SQLite extension that brings autonomous AI agents to your database. It enables databases to run multi-step, tool-using agents that fetch data from external sources (web, APIs, etc.) through sqlite-mcp and populate database tables.

## Installation

```
dart pub add sqlite_agent
```

Requires Dart 3.10+ / Flutter 3.38+.

## Usage

### With `sqlite3`

```dart
import 'package:sqlite3/sqlite3.dart';
import 'package:sqlite_agent/sqlite_agent.dart';

void main() {
  // Load once at startup.
  sqlite3.loadSqliteAgentExtension();

  final db = sqlite3.openInMemory();

  // Check version.
  final result = db.select('SELECT agent_version() AS version');
  print(result.first['version']);

  db.dispose();
}
```

### With `drift`

```dart
import 'package:sqlite3/sqlite3.dart';
import 'package:sqlite_agent/sqlite_agent.dart';
import 'package:drift/native.dart';

Sqlite3 loadExtensions() {
  sqlite3.loadSqliteAgentExtension();
  return sqlite3;
}

// Use when creating the database:
NativeDatabase.createInBackground(
  File(path),
  sqlite3: loadExtensions,
);
```

## Supported platforms

| Platform | Architectures |
|----------|---------------|
| Android  | arm64, arm, x64 |
| iOS      | arm64 (device + simulator) |
| macOS    | arm64, x64 |
| Linux    | arm64, x64 |
| Windows  | x64 |

## API

See the full [sqlite-agent API documentation](https://github.com/sqliteai/sqlite-agent/blob/main/API.md).

## License

See [LICENSE](LICENSE).
