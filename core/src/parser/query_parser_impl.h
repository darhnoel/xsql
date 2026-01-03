#pragma once

#include <string>

#include "../query_parser.h"

namespace xsql {

/// Parses a query string into a Query AST with error reporting.
/// MUST be deterministic and MUST not throw on parse errors.
/// Inputs are query text; outputs are ParseResult with optional error.
ParseResult parse_query_impl(const std::string& input);

}  // namespace xsql
