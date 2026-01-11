#include "parser_internal.h"

#include <cctype>

namespace xsql {

/// Consumes an identifier matching the expected string.
/// MUST enforce exact match and report errors on mismatch.
/// Inputs are tokens/expected; outputs are success or error.
bool Parser::consume_identifier(const std::string& expected) {
  if (current_.type != TokenType::Identifier) {
    return set_error("Expected identifier");
  }
  if (to_upper(current_.text) != to_upper(expected)) {
    return set_error("Expected identifier: " + expected);
  }
  advance();
  return true;
}

/// Consumes a token of the expected type or sets a parse error.
/// MUST advance the token stream on success.
/// Inputs are token type/message; outputs are success or error.
bool Parser::consume(TokenType type, const std::string& message) {
  if (current_.type != type) {
    return set_error(message);
  }
  advance();
  return true;
}

/// Records the first parse error for reporting.
/// MUST preserve the earliest error position for clarity.
/// Inputs are error message; outputs are false with stored error.
bool Parser::set_error(const std::string& message) {
  error_ = ParseError{message, current_.pos};
  return false;
}

/// Produces a ParseResult using the recorded error.
/// MUST return an empty query with the stored error.
/// Inputs are internal state; outputs are ParseResult.
ParseResult Parser::error_result() {
  ParseResult res;
  res.error = error_;
  return res;
}

/// Advances to the next token in the input stream.
/// MUST be called after consuming tokens to keep state in sync.
/// Inputs are internal state; outputs are updated current_.
void Parser::advance() {
  if (has_peek_) {
    current_ = peek_;
    has_peek_ = false;
    return;
  }
  current_ = lexer_.next();
}

/// Peeks one token ahead without consuming it.
/// MUST preserve current_ and return a cached lookahead.
/// Inputs are internal state; outputs are peek token.
Token Parser::peek() {
  if (!has_peek_) {
    peek_ = lexer_.next();
    has_peek_ = true;
  }
  return peek_;
}

/// Tests whether a string starts with a given prefix.
/// MUST be exact and ASCII-safe for parser purposes.
/// Inputs are strings; outputs are boolean with no side effects.
bool Parser::starts_with(const std::string& s, const std::string& prefix) {
  return s.rfind(prefix, 0) == 0;
}

/// Normalizes strings to uppercase for keyword checks.
/// MUST avoid locale-sensitive behavior for determinism.
/// Inputs are strings; outputs are uppercase strings.
std::string Parser::to_upper(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

/// Normalizes strings to lowercase for matching fields and tags.
/// MUST avoid locale-sensitive behavior for determinism.
/// Inputs are strings; outputs are lowercase strings.
std::string Parser::to_lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

}  // namespace xsql
