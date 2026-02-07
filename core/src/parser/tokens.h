#pragma once

#include <cstddef>
#include <string>

namespace xsql {

/// Enumerates lexical tokens produced by the query lexer.
/// MUST remain consistent with parser expectations and keyword mapping.
/// Inputs are characters; outputs are token kinds with no side effects.
enum class TokenType {
  Identifier,
  String,
  Number,
  Comma,
  Dot,
  LParen,
  RParen,
  Semicolon,
  Star,
  End,
  KeywordSelect,
  KeywordFrom,
  KeywordWhere,
  KeywordAnd,
  KeywordOr,
  KeywordIn,
  KeywordExists,
  KeywordDocument,
  KeywordLimit,
  KeywordExclude,
  KeywordOrder,
  KeywordBy,
  KeywordAsc,
  KeywordDesc,
  KeywordAs,
  KeywordTo,
  KeywordList,
  KeywordCount,
  KeywordTable,
  KeywordCsv,
  KeywordParquet,
  KeywordRaw,
  KeywordFragments,
  KeywordContains,
  KeywordHasDirectText,
  KeywordAll,
  KeywordAny,
  KeywordIs,
  KeywordNot,
  KeywordNull,
  KeywordShow,
  KeywordDescribe,
  KeywordInput,
  KeywordInputs,
  KeywordFunctions,
  KeywordAxes,
  KeywordOperators,
  Equal,
  NotEqual,
  RegexMatch
};

/// Represents a single token with source text and position metadata.
/// MUST track byte positions to support precise error reporting.
/// Inputs are lexer output; outputs are consumed by the parser.
struct Token {
  TokenType type;
  std::string text;
  size_t pos = 0;
};

}  // namespace xsql
