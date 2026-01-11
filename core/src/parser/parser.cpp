#include "query_parser_impl.h"

#include "parser_internal.h"

namespace xsql {

/// Constructs a parser for a given query input.
/// MUST immediately read the first token to initialize state.
/// Inputs are query strings; side effects include token consumption.
Parser::Parser(const std::string& input) : lexer_(input) { advance(); }

/// Parses a full query and returns either a Query or a ParseError.
/// MUST consume all tokens or report an unexpected trailing token.
/// Inputs are internal state; outputs are ParseResult.
ParseResult Parser::parse() {
  Query q;
  if (!parse_query_body(q)) return error_result();
  if (current_.type == TokenType::Semicolon) {
    advance();
  }
  if (current_.type != TokenType::End) {
    set_error("Unexpected token after query");
    return error_result();
  }
  q.span = Span{0, current_.pos};
  ParseResult res;
  res.query = q;
  return res;
}

/// Entry point for parsing that constructs a parser instance.
/// MUST not throw on parse errors and MUST return ParseResult consistently.
/// Inputs are query text; outputs are ParseResult with optional error.
ParseResult parse_query_impl(const std::string& input) {
  Parser parser(input);
  return parser.parse();
}

}  // namespace xsql
