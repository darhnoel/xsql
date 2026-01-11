from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional


@dataclass(frozen=True)
class Document:
    """Represents a parsed HTML document snapshot for deterministic queries."""

    html: str
    source: str


@dataclass(frozen=True)
class ExportSink:
    """Captures export intent attached to a query result."""

    kind: str
    path: str


@dataclass(frozen=True)
class TableResult:
    """Holds rows extracted from an HTML <table> node."""

    node_id: int
    rows: List[List[Optional[str]]]


@dataclass(frozen=True)
class QueryResult:
    """Carries rows/tables plus metadata from executing an XSQL query."""

    columns: List[str]
    rows: List[Dict[str, Any]]
    tables: List[TableResult]
    to_list: bool
    to_table: bool
    table_has_header: bool
    export_sink: ExportSink
