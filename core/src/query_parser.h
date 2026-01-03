#pragma once

#include <optional>
#include <string>

#include "ast.h"

namespace xsql {

/// Describes a parse failure with a message and byte position.
/// MUST report positions relative to the original input string.
/// Inputs are parser diagnostics; outputs are error details only.
struct ParseError {
  std::string message;
  size_t position = 0;
};

/// Wraps either a parsed Query or a ParseError.
/// MUST contain exactly one of query or error.
/// Inputs are parser outputs; side effects are none.
struct ParseResult {
  std::optional<Query> query;
  std::optional<ParseError> error;
};

/// Parses a query string into an AST for execution.
/// MUST return errors without throwing on invalid syntax.
/// Inputs are query text; outputs are ParseResult with optional error.
ParseResult parse_query(const std::string& input);

}  // namespace xsql
