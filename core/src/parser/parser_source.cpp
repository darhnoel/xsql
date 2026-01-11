#include "parser_internal.h"

namespace xsql {

/// Parses the FROM source, supporting document, path, or URL forms.
/// MUST normalize source kinds and capture spans for errors.
/// Inputs are tokens; outputs are Source or errors.
bool Parser::parse_source(Source& src) {
  if (current_.type == TokenType::KeywordDocument) {
    src.kind = Source::Kind::Document;
    src.value = "document";
    src.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return parse_source_alias(src);
  }
  if (current_.type == TokenType::KeywordRaw) {
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after RAW")) return false;
    if (current_.type != TokenType::String) {
      return set_error("Expected string literal inside RAW()");
    }
    src.kind = Source::Kind::RawHtml;
    src.value = current_.text;
    src.span = Span{start, current_.pos + current_.text.size()};
    advance();
    if (!consume(TokenType::RParen, "Expected ) after RAW argument")) return false;
    return parse_source_alias(src);
  }
  if (current_.type == TokenType::KeywordFragments) {
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after FRAGMENTS")) return false;
    src.kind = Source::Kind::Fragments;
    if (current_.type == TokenType::KeywordRaw) {
      size_t raw_start = current_.pos;
      advance();
      if (!consume(TokenType::LParen, "Expected ( after RAW")) return false;
      if (current_.type != TokenType::String) {
        return set_error("Expected string literal inside RAW()");
      }
      src.fragments_raw = current_.text;
      src.span = Span{raw_start, current_.pos + current_.text.size()};
      advance();
      if (!consume(TokenType::RParen, "Expected ) after RAW argument")) return false;
    } else {
      std::shared_ptr<Query> subquery;
      if (!parse_subquery(subquery)) return false;
      src.fragments_query = std::move(subquery);
    }
    if (!consume(TokenType::RParen, "Expected ) after FRAGMENTS argument")) return false;
    src.span = Span{start, current_.pos};
    return parse_source_alias(src);
  }
  if (current_.type == TokenType::String) {
    src.value = current_.text;
    src.span = Span{current_.pos, current_.pos + current_.text.size()};
    if (starts_with(src.value, "http://") || starts_with(src.value, "https://")) {
      src.kind = Source::Kind::Url;
    } else {
      src.kind = Source::Kind::Path;
    }
    advance();
    return parse_source_alias(src);
  }
  if (current_.type == TokenType::Identifier) {
    src.kind = Source::Kind::Document;
    src.value = "document";
    src.alias = current_.text;
    src.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  return set_error("Expected document, alias, string literal, RAW(), or FRAGMENTS() source");
}

/// Parses a full query inside FRAGMENTS(), stopping before a closing ).
/// MUST return with current_ positioned at the closing parenthesis.
/// Inputs are token streams; outputs are Query pointers or errors.
bool Parser::parse_subquery(std::shared_ptr<Query>& out) {
  auto q = std::make_shared<Query>();
  if (!parse_query_body(*q)) return false;
  if (current_.type == TokenType::Semicolon) {
    advance();
  }
  if (current_.type != TokenType::RParen) {
    return set_error("Expected ) after subquery");
  }
  out = std::move(q);
  return true;
}

/// Parses an optional alias after a source.
/// MUST accept AS or bare identifiers for aliasing.
/// Inputs are tokens; outputs are updated Source or errors.
bool Parser::parse_source_alias(Source& src) {
  if (current_.type == TokenType::KeywordAs) {
    advance();
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected alias identifier after AS");
    }
    src.alias = current_.text;
    advance();
    return true;
  }
  if (current_.type == TokenType::Identifier) {
    src.alias = current_.text;
    advance();
    return true;
  }
  return true;
}

}  // namespace xsql
