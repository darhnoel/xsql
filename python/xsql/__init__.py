"""Python interface for XSQL query execution with safety-first defaults."""

from __future__ import annotations

from typing import Any, Dict, Optional

from ._loader import load_html_source
from ._security import FetchPolicy
from ._summary import summarize_document
from ._types import Document, ExportSink, QueryResult, TableResult

try:
    from . import _core
except Exception as exc:  # pragma: no cover - surfaced in import-time errors.
    _core = None
    _core_import_error = exc
else:
    _core_import_error = None

doc: Optional[Document] = None


def _require_core() -> None:
    if _core is None:
        raise RuntimeError("xsql native module is unavailable") from _core_import_error


def load(
    source: Any,
    *,
    base_dir: Optional[str] = None,
    allow_network: bool = False,
    allow_private_network: bool = False,
    timeout: int = 10,
    max_bytes: int = 5_000_000,
) -> Document:
    """Loads HTML into a module-level Document for reuse across queries.

    This function MUST be treated as not thread-safe because it mutates xsql.doc.
    It performs IO for file/URL sources and enforces size/network limits.
    """

    policy = FetchPolicy(
        allow_network=allow_network,
        allow_private_network=allow_private_network,
        timeout=timeout,
        max_bytes=max_bytes,
    )
    html, origin = load_html_source(source, base_dir=base_dir, policy=policy)
    document = Document(html=html, source=origin)
    globals()["doc"] = document
    return document


def summarize(doc: Document, *, max_nodes_preview: int = 50) -> Dict[str, object]:
    """Summarizes a document by tags and attribute keys without executing scripts.

    The summary MUST avoid disclosing paths beyond the provided document source.
    It performs parsing but does not execute or fetch external resources.
    """

    return summarize_document(doc, max_nodes_preview)


def execute(
    query: str,
    *,
    source: Any = None,
    doc: Optional[Document] = None,
    params: Optional[Dict[str, Any]] = None,
    allow_network: bool = False,
    base_dir: Optional[str] = None,
    timeout: int = 10,
    max_bytes: int = 5_000_000,
    max_results: int = 10_000,
) -> QueryResult:
    """Executes an XSQL query against a document or source with safety limits.

    The query MUST be executed without eval/exec and respects max_results limits.
    It may read files or URLs when a source is provided and allow_network permits it.
    """

    _require_core()
    if params:
        raise ValueError("params are not supported by XSQL execution")
    active = doc or globals().get("doc")
    if active is None:
        if source is None:
            raise ValueError("No document loaded; provide doc or source")
        policy = FetchPolicy(
            allow_network=allow_network,
            allow_private_network=False,
            timeout=timeout,
            max_bytes=max_bytes,
        )
        html, origin = load_html_source(source, base_dir=base_dir, policy=policy)
        active = Document(html=html, source=origin)
    raw = _core.execute_from_document(active.html, query)
    rows = raw.get("rows", [])
    tables = []
    for table in raw.get("tables", []):
        tables.append(TableResult(node_id=table["node_id"], rows=table["rows"]))
    if len(rows) > max_results:
        raise ValueError("Query result exceeds max_results")
    total_table_rows = sum(len(table.rows) for table in tables)
    if total_table_rows > max_results:
        raise ValueError("Table result exceeds max_results")
    export = raw.get("export_sink", {})
    export_sink = ExportSink(kind=export.get("kind", "none"), path=export.get("path", ""))
    return QueryResult(
        columns=raw.get("columns", []),
        rows=rows,
        tables=tables,
        to_list=raw.get("to_list", False),
        to_table=raw.get("to_table", False),
        table_has_header=raw.get("table_has_header", True),
        export_sink=export_sink,
    )


__all__ = ["Document", "QueryResult", "TableResult", "ExportSink", "doc", "load", "summarize", "execute"]
