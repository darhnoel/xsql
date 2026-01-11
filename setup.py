from __future__ import annotations

from pathlib import Path

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup


def core_sources() -> list[str]:
    sources = [
        "python/xsql/_core.cpp",
        "core/src/ast.cpp",
        "core/src/query_parser.cpp",
        "core/src/parser/parser.cpp",
        "core/src/parser/parser_expr.cpp",
        "core/src/parser/parser_query.cpp",
        "core/src/parser/parser_select.cpp",
        "core/src/parser/parser_source.cpp",
        "core/src/parser/parser_util.cpp",
        "core/src/parser/lexer.cpp",
        "core/src/html_parser.cpp",
        "core/src/html/parser_naive.cpp",
        "core/src/executor/executor.cpp",
        "core/src/executor/filter.cpp",
        "core/src/executor/order.cpp",
        "core/src/util/string_util.cpp",
        "core/src/xsql/execute.cpp",
        "core/src/xsql/io.cpp",
        "core/src/xsql/validation.cpp",
        "core/src/xsql/result_builder.cpp",
        "core/src/xsql/table_extract.cpp",
    ]
    return sources


ext_modules = [
    Pybind11Extension(
        "xsql._core",
        core_sources(),
        include_dirs=[
            "core/include",
            "core/src",
            "core/src/parser",
            "core/src/executor",
            "core/src/html",
            "core/src/util",
            "core/src/xsql",
        ],
        cxx_std=20,
    )
]


setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
