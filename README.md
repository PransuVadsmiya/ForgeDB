# ForgeDB

A small SQL database engine in C++17. It parses SQL, plans queries against
B-tree and hash indexes, executes them (joins, aggregates, transactions), and
persists tables to disk — a scaled-down take on how an engine like SQLite works
internally.

## Features

- **SQL**: `CREATE`/`DROP TABLE`, `CREATE INDEX`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`, `DESCRIBE`, `SHOW TABLES`, `VACUUM`
- **Indexes**: B-tree (`std::map`) for range scans, hash (`std::unordered_map`) for equality; the planner tries every applicable index and keeps the one returning the fewest rows, falling back to a sequential scan
- **`WHERE` expressions**: a hand-written lexer and Pratt parser build an AST, giving `AND`/`OR`/`NOT`, parentheses, arithmetic, and functions (`UPPER`, `LOWER`, `LENGTH`, `ABS`, `COALESCE`), evaluated with three-valued (NULL-aware) logic
- **Joins**: `INNER`, `LEFT`, `RIGHT`, `FULL OUTER`, and `CROSS`, using a hash join
- **Aggregates**: `COUNT`, `AVG`, `MIN`, `MAX`
- **Transactions**: `BEGIN` / `COMMIT` / `ROLLBACK` (snapshot-based)
- **`EXPLAIN` / `EXPLAIN ANALYZE`**: prints the chosen plan and rows examined
- **Storage**: each table is a `.schema` + `.rows` (TSV) pair, reloaded on startup

## How it works

```
SQL text -> lex -> parse (AST) -> validate -> plan -> execute -> storage
```

The planner extracts the sargable comparisons from a `WHERE` expression to drive
index selection, then the evaluator applies the full expression as the exact
filter. So `WHERE cgpa >= 9 AND (dept = 'ICT' OR sem = 6)` still uses the `cgpa`
index and evaluates the `OR` only on the rows the index returns.

Index keys are strings, so one structure serves both column types. Integers are
encoded order-preservingly — the sign bit is flipped and the result zero-padded
to a fixed width — so lexicographic key order matches numeric order, negatives
included.

Rows live in a `vector<optional<Row>>`: `DELETE` tombstones a slot rather than
erasing it, which keeps every row ID (and therefore every index entry) stable.
`VACUUM` compacts the vector and rebuilds the indexes.

## Build

```
cmake -S . -B build
cmake --build build
```

Or directly:

```
g++ -std=c++17 -Iinclude src/*.cpp -o minisql
```

## Run

```
# run a script
./minisql --data demo-data --file examples/advanced_demo.sql

# interactive shell (end each statement with ';', .exit to quit)
./minisql
```

## Example

```sql
CREATE TABLE students (id INT PRIMARY KEY, name TEXT NOT NULL, dept TEXT, cgpa INT);
CREATE INDEX idx_cgpa ON students(cgpa) USING BTREE;
INSERT INTO students VALUES (1, 'Pransu', 'ICT', 9);

EXPLAIN ANALYZE
SELECT id, name FROM students WHERE cgpa >= 8 AND UPPER(dept) = 'ICT';
```

See `examples/advanced_demo.sql` for a fuller walkthrough, and
`pgsql/reference.sql` for the equivalent PostgreSQL.

## Limitations

Single-threaded, no concurrency or crash recovery (WAL). "B-tree" here is an
in-memory `std::map`, not a paged on-disk B-tree, and `BEGIN` snapshots the whole
database in memory. Full `WHERE` expressions apply to single-table queries; joins
and `UPDATE`/`DELETE` use a simpler AND-only filter that also rejects qualified
column names. Join queries need table-qualified projections (`SELECT s.name`, not
`SELECT name`) and ignore `ORDER BY` and aggregates. Only one join per query.
