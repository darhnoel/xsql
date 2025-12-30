# XSQL Documentation (v0.1 Prototype)

XSQL is a SQL-style query language for static HTML. It treats each HTML element
as a row in a node table and lets you filter by tag and attributes. This is a
minimal, offline-first prototype built in C++20.

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

## Interactive Mode (REPL)

Commands:
- `.help`: show help
- `.load <path|url>` / `:load <path|url>`: load input (path or URL)
- `.mode duckbox|json|plain`: set output mode
- `.display_mode more|less`: control JSON truncation
- `.max_rows <n>`: set duckbox max rows (0 = unlimited)
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
- `source_uri` (string)

## Query Language

### Basic Form
```
SELECT <tag_list> FROM <source> [WHERE <expr>] [LIMIT <n>] [TO LIST() | TO TABLE()]
```

### Source
```
FROM document
FROM 'path.html'
FROM 'https://example.com'   (URL fetching requires libcurl)
FROM doc                     (alias for document)
FROM document AS doc
```

### Tags
```
SELECT div
SELECT div,span
SELECT *
```

### WHERE Expressions
Supported operators:
- `=`
- `IN`
- `<>` / `!=`
- `IS NULL` / `IS NOT NULL`
- `~` (regex, ECMAScript)
- `AND`, `OR`

Attribute references:
```
attributes.id = 'main'
parent.attributes.class = 'menu'
child.attributes.href <> ''
ancestor.attributes.id = 'root'
descendant.attributes.class IN ('nav','top')
```

Field references:
```
text <> ''
tag = 'div'
parent.tag = 'section'
child.tag = 'a'
ancestor.text ~ 'error|warning'
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
- `node_id`, `tag`, `parent_id`, `source_uri`, `attributes`

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

## Examples

Select by id:
```
SELECT ul FROM doc WHERE attributes.id = 'countries';
```

Parent attribute filter:
```
SELECT table FROM doc WHERE parent.attributes.id = 'table-01';
```

Descendant attribute filter:
```
SELECT div FROM doc WHERE descendant.attributes.class = 'card';
```

Extract href list:
```
SELECT link.href FROM doc WHERE attributes.rel = "preload" TO LIST();
```

Order results:
```
SELECT div FROM doc ORDER BY node_id DESC;
```
```
SELECT * FROM doc ORDER BY tag, parent_id LIMIT 10;
```

Summarize tags:
```
SELECT summarize(*) FROM doc;
```

Top tags:
```
SELECT summarize(*) FROM doc ORDER BY count DESC LIMIT 5;
```

Filter by parent node id:
```
SELECT span FROM doc WHERE parent_id = 1;
```

Filter by node id:
```
SELECT span FROM doc WHERE node_id = 1;
```

Match elements with no attributes:
```
SELECT div FROM doc WHERE attributes IS NULL;
```

## Known Limitations (v0.1)

- No XPath or positional predicates.
- `ORDER BY` is limited to `node_id`, `tag`, `text`, or `parent_id`.
- No `GROUP BY` or joins.
- No XML mode (HTML only).
- URL fetching requires libcurl.
- Default output is duckbox tables; JSON output is available via `--mode json`.

## Build Dependencies

Optional:
- `nlohmann/json` for pretty JSON output (vcpkg recommended).
- `libxml2` for robust HTML parsing (fallback to naive parser if missing).
- `libcurl` for URL fetching.

## Troubleshooting

- If you see `No input loaded` in REPL, run `:load <path|url>`.
- If a query fails with `Expected FROM`, include a `FROM` clause.
- If output is compact JSON, ensure `nlohmann/json` is linked via vcpkg.
