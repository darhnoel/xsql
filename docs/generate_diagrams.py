#!/usr/bin/env python3
from __future__ import annotations

import io
import os
import sys

try:
    from railroad import (
        Diagram,
        Sequence,
        Choice,
        Optional,
        OneOrMore,
        ZeroOrMore,
        Terminal,
        NonTerminal,
    )
except ImportError as exc:
    print("Error: railroad-diagrams is not installed.", file=sys.stderr)
    print("Run: python3 -m pip install -r docs/requirements.txt", file=sys.stderr)
    raise SystemExit(1) from exc


def t(value: str) -> Terminal:
    return Terminal(value)


def nt(value: str) -> NonTerminal:
    return NonTerminal(value)


STYLE = """<style>
.railroad-diagram path { stroke: #333; stroke-width: 2; fill: none; }
.railroad-diagram text { fill: #111; font: 14px monospace; text-anchor: middle; }
.railroad-diagram rect { fill: #fdfdfd; stroke: #333; }
.railroad-diagram .terminal rect { fill: #e8f2ea; }
.railroad-diagram .non-terminal rect { fill: #f2f2f2; }
</style>"""


def render_svg(diagram: Diagram) -> str:
    buf = io.StringIO()
    try:
        diagram.writeSvg(buf.write)
    except TypeError:
        diagram.writeSvg(buf)
    svg = buf.getvalue()
    svg = ensure_svg_namespace(svg)
    insert = "\n" + STYLE + "\n<rect width=\"100%\" height=\"100%\" fill=\"white\" />\n"
    idx = svg.find(">")
    if idx == -1:
        return svg
    return svg[: idx + 1] + insert + svg[idx + 1 :]


def write_svg(diagram: Diagram, path: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    svg = render_svg(diagram)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(svg)


def ensure_svg_namespace(svg: str) -> str:
    start = svg.find("<svg")
    if start == -1:
        return svg
    end = svg.find(">", start)
    if end == -1:
        return svg
    tag = svg[start : end + 1]
    if "xmlns=" in tag and "xmlns:xlink" in tag:
        return svg
    attrs = ""
    if "xmlns=" not in tag:
        attrs += " xmlns=\"http://www.w3.org/2000/svg\""
    if "xmlns:xlink" not in tag:
        attrs += " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
    new_tag = tag[:-1] + attrs + ">"
    return svg[:start] + new_tag + svg[end + 1 :]


def build_diagrams() -> dict[str, Diagram]:
    diagrams: dict[str, Diagram] = {}

    diagrams["query"] = Diagram(
        Sequence(
            Choice(0, nt("select_query"), nt("show_query"), nt("describe_query")),
            Optional(t(";")),
        )
    )

    diagrams["select_query"] = Diagram(
        Sequence(
            t("SELECT"),
            nt("select_list"),
            Optional(nt("exclude_clause")),
            t("FROM"),
            nt("source"),
            Optional(nt("where_clause")),
            Optional(nt("order_by_clause")),
            Optional(nt("limit_clause")),
            Optional(nt("to_clause")),
        )
    )

    diagrams["show_query"] = Diagram(
        Sequence(
            t("SHOW"),
            Choice(
                0,
                t("INPUT"),
                t("INPUTS"),
                t("FUNCTIONS"),
                t("AXES"),
                t("OPERATORS"),
            ),
        )
    )

    diagrams["describe_query"] = Diagram(
        Sequence(t("DESCRIBE"), Choice(0, t("DOC"), t("DOCUMENT"), t("LANGUAGE")))
    )

    diagrams["select_list"] = Diagram(OneOrMore(nt("select_item"), t(",")))

    diagrams["select_item"] = Diagram(
        Choice(
            0,
            nt("tag_only"),
            nt("projected_field"),
            Sequence(t("COUNT"), t("("), Choice(0, t("*"), nt("tag_name")), t(")")),
            Sequence(t("SUMMARIZE"), t("("), t("*"), t(")")),
            Sequence(t("TFIDF"), t("("), nt("tfidf_args"), t(")")),
            Sequence(
                t("TRIM"),
                t("("),
                Choice(
                    0,
                    Sequence(t("TEXT"), t("("), nt("tag_name"), t(")")),
                    Sequence(
                        t("INNER_HTML"),
                        t("("),
                        nt("tag_name"),
                        Optional(Sequence(t(","), nt("number"))),
                        t(")"),
                    ),
                ),
                t(")"),
            ),
            Sequence(t("TEXT"), t("("), nt("tag_name"), t(")")),
            Sequence(
                t("INNER_HTML"),
                t("("),
                nt("tag_name"),
                Optional(Sequence(t(","), nt("number"))),
                t(")"),
            ),
        )
    )

    diagrams["projected_field"] = Diagram(
        Choice(
            0,
            Sequence(nt("tag_name"), t("."), nt("field_name")),
            Sequence(
                nt("tag_name"),
                t("("),
                nt("field_name"),
                ZeroOrMore(Sequence(t(","), nt("field_name"))),
                t(")"),
            ),
        )
    )

    diagrams["exclude_clause"] = Diagram(
        Choice(
            0,
            Sequence(t("EXCLUDE"), nt("field_name")),
            Sequence(
                t("EXCLUDE"),
                t("("),
                nt("field_name"),
                ZeroOrMore(Sequence(t(","), nt("field_name"))),
                t(")"),
            ),
        )
    )

    diagrams["source"] = Diagram(
        Choice(
            0,
            t("document"),
            t("doc"),
            nt("string_literal"),
            Sequence(t("RAW"), t("("), nt("string_literal"), t(")")),
            Sequence(
                t("FRAGMENTS"),
                t("("),
                Choice(
                    0,
                    Sequence(t("RAW"), t("("), nt("string_literal"), t(")")),
                    nt("select_query"),
                ),
                t(")"),
                Optional(Sequence(t("AS"), nt("alias"))),
            ),
            Sequence(t("document"), t("AS"), nt("alias")),
            nt("alias"),
        )
    )

    diagrams["where_clause"] = Diagram(Sequence(t("WHERE"), nt("expr")))

    diagrams["expr"] = Diagram(
        Sequence(nt("and_expr"), ZeroOrMore(Sequence(t("OR"), nt("and_expr"))))
    )

    diagrams["and_expr"] = Diagram(
        Sequence(nt("cmp_expr"), ZeroOrMore(Sequence(t("AND"), nt("cmp_expr"))))
    )

    diagrams["cmp_expr"] = Diagram(
        Choice(
            0,
            Sequence(t("("), nt("expr"), t(")")),
            Sequence(nt("operand"), nt("cmp_op"), nt("value_list")),
        )
    )

    diagrams["cmp_op"] = Diagram(
        Choice(
            0,
            t("="),
            t("<>"),
            t("IN"),
            t("~"),
            t("CONTAINS"),
            Sequence(t("CONTAINS"), t("ALL")),
            Sequence(t("CONTAINS"), t("ANY")),
            Sequence(t("IS"), t("NULL")),
            Sequence(t("IS"), t("NOT"), t("NULL")),
            t("HAS_DIRECT_TEXT"),
        )
    )

    diagrams["operand"] = Diagram(
        Sequence(Optional(Sequence(nt("axis"), t("."))), nt("field_ref"))
    )

    diagrams["axis"] = Diagram(
        Choice(0, t("parent"), t("child"), t("ancestor"), t("descendant"))
    )

    diagrams["field_ref"] = Diagram(
        Choice(
            0,
            nt("field_name"),
            t("attributes"),
            Sequence(t("attributes"), t("."), nt("attr_name")),
        )
    )

    diagrams["order_by_clause"] = Diagram(
        Sequence(
            t("ORDER"),
            t("BY"),
            nt("order_item"),
            ZeroOrMore(Sequence(t(","), nt("order_item"))),
        )
    )

    diagrams["order_item"] = Diagram(
        Sequence(nt("order_field"), Optional(Choice(0, t("ASC"), t("DESC"))))
    )

    diagrams["limit_clause"] = Diagram(Sequence(t("LIMIT"), nt("number")))

    diagrams["to_clause"] = Diagram(
        Choice(
            0,
            Sequence(t("TO"), t("LIST"), t("("), t(")")),
            Sequence(t("TO"), t("TABLE"), t("("), nt("table_opts"), t(")")),
            Sequence(t("TO"), t("CSV"), t("("), nt("string_literal"), t(")")),
            Sequence(t("TO"), t("PARQUET"), t("("), nt("string_literal"), t(")")),
        )
    )

    diagrams["table_opts"] = Diagram(
        Optional(OneOrMore(nt("table_opt"), t(",")))
    )

    diagrams["table_opt"] = Diagram(
        Choice(
            0,
            Sequence(t("HEADER"), Optional(t("=")), Choice(0, t("ON"), t("OFF"))),
            t("NOHEADER"),
            t("NO_HEADER"),
            Sequence(t("EXPORT"), Optional(t("=")), nt("string_literal")),
        )
    )

    return diagrams


def main() -> int:
    out_dir = os.path.join("docs", "diagrams")
    diagrams = build_diagrams()
    for name, diagram in diagrams.items():
        write_svg(diagram, os.path.join(out_dir, f"{name}.svg"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
