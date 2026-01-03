#include "query_parser.h"

#include "parser/query_parser_impl.h"

namespace xsql {

/// Delegates public parse_query calls to the internal parser implementation.
/// MUST preserve error reporting behavior and MUST not throw on syntax errors.
/// Inputs are query text; outputs are ParseResult with optional error.
ParseResult parse_query(const std::string& input) {
  return parse_query_impl(input);
}

}  // namespace xsql
