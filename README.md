# XSQL Documentation (v1.1.2)

XSQL is a SQL-style query language for static HTML. It treats each HTML element
as a row in a node table and lets you filter by tag and attributes. The project
has now flourished to v1.0.0 as an offline-first C++20 tool.

## Quick Start

Build:
```
./build.sh
```

Run on a file:
```
./build/xsql --query "SELECT a FROM doc WHERE attributes.id = 'login'" --input ./data/index.html
```

Interactive mode:
```
./build/xsql --interactive --input ./data/index.html
```

## Python API (xsql package)

```python
import xsql

doc = xsql.load("data/index.html")
print(xsql.summarize(doc))
rows = xsql.execute("SELECT a FROM document WHERE attributes.href IS NOT NULL")

doc = xsql.load("https://example.com", allow_network=True)
rows = xsql.execute("SELECT title FROM document")
```

Install:
```
pip install pyxsql
```

Security Notes:
- Network access is disabled by default; enable with `allow_network=True`.
- Private/localhost targets are blocked unless `allow_private_network=True`.
- File reads are confined to `base_dir` when provided.
- Downloads are capped by `max_bytes`, and query output by `max_results`.

## Build on Linux/macOS/Windows

Linux (Ubuntu/Debian):
```
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config bison flex
./build.sh
```

macOS (Homebrew):
```
brew install cmake ninja pkg-config bison flex
./build.sh
```

Windows (PowerShell, MSVC):
```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

Optional dependencies via vcpkg:
```
vcpkg install nlohmann-json libxml2 curl arrow[parquet]
```

If you do not want Parquet, configure with `-DXSQL_WITH_ARROW=OFF`.

## Python Build & Tests

Create a virtual environment and install the editable package:
```
python3 -m venv xsql_venv
source ./xsql_venv/bin/activate
pip install -U pip
pip install -e .[test]
```

Run Python tests:
```
pytest -v python/tests
```

Shorthand:
```
./test_python.sh
```

## CLI Usage

```
./build/xsql --query "<query>" --input <path>
./build/xsql --query-file <file> --input <path>
./build/xsql --interactive [--input <path>]
./build/xsql --mode duckbox|json|plain
./build/xsql --highlight on|off
./build/xsql --color=disabled
```

Notes:
- `--input` is required unless reading HTML from stdin.
- Colors are auto-disabled when stdout is not a TTY.
- Default output mode is `duckbox` (table-style).
- `--highlight` only affects duckbox headers (auto-disabled when not a TTY).
- `TO CSV()` / `TO PARQUET()` write files instead of printing results.

## Interactive Mode (REPL)

Commands:
- `.help`: show help
- `.load <path|url>` / `:load <path|url>`: load input (path or URL)
- `.mode duckbox|json|plain`: set output mode
- `.display_mode more|less`: control JSON truncation
- `.max_rows <n|inf>`: set duckbox max rows (`inf` = unlimited)
- `.summarize [doc|path|url]`: list all tags and counts for the active input or target
- `.quit` / `.q` / `:quit` / `:exit`: exit the REPL

Keys:
- Up/Down: history (max 5 entries)
- Left/Right: move cursor
- Ctrl+L: clear screen

## Data Model

Each HTML element becomes a row with fields:
- `node_id` (int64)
- `tag` (string)
- `attributes` (map<string,string>)
- `parent_id` (int64 or null)
- `sibling_pos` (int64, 1-based position among siblings)
- `source_uri` (string)

## Query Language

### Basic Form
```
SELECT <tag_list> FROM <source> [WHERE <expr>] [LIMIT <n>]
  [TO LIST() | TO TABLE() | TO CSV('file.csv') | TO PARQUET('file.parquet')]
```

### Source
```
FROM document
FROM 'path.html'
FROM 'https://example.com'   (URL fetching requires libcurl)
FROM RAW('<div class="card"></div>')
FROM FRAGMENTS(RAW('<ul><li>1</li><li>2</li></ul>')) AS frag
FROM FRAGMENTS(SELECT inner_html(div) FROM doc WHERE attributes.class = 'pagination') AS frag
FROM doc                     (alias for document)
FROM document AS doc
```

Notes:
- `RAW('<html>')` parses an inline HTML string as the document source.
- `FRAGMENTS(...)` builds a temporary document by concatenating HTML fragments.
- `FRAGMENTS` accepts either `RAW('<html>')` or a subquery returning a single HTML string column (use `inner_html(...)`).
- `FRAGMENTS` subqueries cannot use file or URL sources.

### Tags
```
SELECT div
SELECT div,span
SELECT *
```

Exclude columns:
```
SELECT * EXCLUDE source_uri FROM doc
SELECT * EXCLUDE (source_uri, tag) FROM doc
```

### WHERE Expressions
Supported operators:
- `=`
- `IN`
- `<>` / `!=`
- `IS NULL` / `IS NOT NULL`
- `~` (regex, ECMAScript)
- `CONTAINS` (attributes only, case-insensitive)
- `HAS_DIRECT_TEXT` (case-insensitive substring match on direct text)
- `AND`, `OR`

Attribute references:
```
attributes.id = 'main'
parent.attributes.class = 'menu'
child.attributes.href <> ''
ancestor.attributes.id = 'root'
descendant.attributes.class IN ('nav','top')
attributes.href CONTAINS 'example'
```

Field references:
```
text <> ''
tag = 'div'
parent.tag = 'section'
child.tag = 'a'
ancestor.text ~ 'error|warning'
div HAS_DIRECT_TEXT 'login'
sibling_pos = 2
```

Shorthand attribute filters:
```
title = "Menu"
doc.title = "Menu"
```

### Aliases
Alias the source and qualify attribute filters:
```
SELECT a FROM document AS d WHERE d.attributes.id = 'login'
```

### Projections
Project a field from a tag:
```
SELECT a.parent_id FROM doc
SELECT link.href FROM doc
SELECT a.attributes FROM doc
SELECT div(node_id, tag, parent_id) FROM doc
```

Supported base fields:
- `node_id`, `tag`, `parent_id`, `sibling_pos`, `source_uri`, `attributes`

Attribute value projection:
- `SELECT link.href FROM doc` returns the `href` value

Function projection:
- `SELECT inner_html(div) FROM doc` returns the raw inner HTML for each `div`
- `SELECT inner_html(div, 1) FROM doc` keeps only tags up to depth 1 (drops deeper tags)
- `SELECT trim(inner_html(div)) FROM doc` trims leading/trailing whitespace
- `SELECT TEXT(div) FROM doc WHERE tag = 'div'` returns descendant text for each `div`

Notes:
- `TEXT()` and `INNER_HTML()` require a `WHERE` clause with a non-tag filter (e.g., attributes or parent).
- `attributes IS NULL` matches elements with no attributes.

### TO LIST()
Output a JSON list for a single projected column:
```
SELECT link.href FROM doc WHERE attributes.rel = "preload" TO LIST()
```

### TO TABLE()
Extract an HTML `<table>` into rows (array of arrays):
```
SELECT table FROM doc TO TABLE()
```

If multiple tables match, the output is a list of objects:
```
[{ "node_id": 123, "rows": [[...], ...] }, ...]
```

Note: `TO LIST()` always returns JSON output. `TO TABLE()` uses duckbox by default and JSON in `--mode json|plain`.

### TO CSV()
Write any rectangular result to a CSV file:
```
SELECT a.href, a.text FROM doc WHERE attributes.href IS NOT NULL TO CSV('links.csv')
```

### TO PARQUET()
Write any rectangular result to a Parquet file (requires Apache Arrow feature):
```
SELECT * FROM doc TO PARQUET('nodes.parquet')
```

Note: `TO CSV()` and `TO PARQUET()` write files and do not print the result set.
If you `SELECT table ... TO CSV(...)`, XSQL exports the HTML table rows directly.

### LIMIT
```
SELECT a FROM doc LIMIT 5
```

### COUNT()
Minimal aggregate:
```
SELECT COUNT(a) FROM doc
SELECT COUNT(*) FROM doc
SELECT COUNT(link) FROM doc WHERE attributes.rel = "preload"
```

### Regex
Use `~` with ECMAScript regex:
```
SELECT a FROM doc WHERE attributes.href ~ '.*\\.pdf$'
```

### Contains (attributes)
Case-insensitive substring match for attribute values:
```
SELECT a FROM doc WHERE attributes.href CONTAINS 'techkhmer'
SELECT a FROM doc WHERE attributes.href CONTAINS ALL ('https', '.html')
SELECT a FROM doc WHERE attributes.href CONTAINS ANY ('https', 'mailto')
```

### Direct Text
Case-insensitive substring match on direct text only (excluding nested tags):
```
SELECT div FROM doc WHERE div HAS_DIRECT_TEXT 'computer science'
```

## Examples

```
-- Filters
SELECT ul FROM doc WHERE attributes.id = 'countries';
SELECT table FROM doc WHERE parent.attributes.id = 'table-01';
SELECT div FROM doc WHERE descendant.attributes.class = 'card';
SELECT span FROM doc WHERE parent_id = 1;
SELECT span FROM doc WHERE node_id = 1;
SELECT div FROM doc WHERE attributes IS NULL;

-- Lists and exports
SELECT link.href FROM doc WHERE attributes.rel = "preload" TO LIST();
SELECT a.href, a.text FROM doc WHERE attributes.href IS NOT NULL TO CSV('links.csv');
SELECT * FROM doc TO PARQUET('nodes.parquet');

-- Fragments
SELECT li FROM FRAGMENTS(SELECT inner_html(ul) FROM doc WHERE attributes.id = 'menu') AS frag;

-- Ordering
SELECT div FROM doc ORDER BY node_id DESC;
SELECT * FROM doc ORDER BY tag, parent_id LIMIT 10;

-- Summaries
SELECT summarize(*) FROM doc;
SELECT summarize(*) FROM doc ORDER BY count DESC LIMIT 5;
```

## Known Limitations (v0.1)

- No XPath or positional predicates.
- `ORDER BY` is limited to `node_id`, `tag`, `text`, or `parent_id`.
- No `GROUP BY` or joins.
- No XML mode (HTML only).
- URL fetching requires libcurl.
- Default output is duckbox tables; JSON output is available via `--mode json`.
- `TO PARQUET()` requires Apache Arrow support at build time.

## Build Dependencies

Optional:
- `nlohmann/json` for pretty JSON output (vcpkg recommended).
- `libxml2` for robust HTML parsing (fallback to naive parser if missing).
- `libcurl` for URL fetching.
- `apache-arrow` (Arrow/Parquet) for `TO PARQUET()` export.

## Troubleshooting

- If you see `No input loaded` in REPL, run `:load <path|url>`.
- If a query fails with `Expected FROM`, include a `FROM` clause.
- If output is compact JSON, ensure `nlohmann/json` is linked via vcpkg.
