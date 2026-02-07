#include "lexer.h"

#include <cctype>

namespace xsql {

Lexer::Lexer(const std::string& input) : input_(input) {}

Token Lexer::next() {
  skip_ws();
  if (pos_ >= input_.size()) {
    return Token{TokenType::End, "", pos_};
  }

  char c = input_[pos_];
  if (c == ',') {
    ++pos_;
    return Token{TokenType::Comma, ",", pos_ - 1};
  }
  if (c == '.') {
    ++pos_;
    return Token{TokenType::Dot, ".", pos_ - 1};
  }
  if (c == '(') {
    ++pos_;
    return Token{TokenType::LParen, "(", pos_ - 1};
  }
  if (c == ')') {
    ++pos_;
    return Token{TokenType::RParen, ")", pos_ - 1};
  }
  if (c == ';') {
    ++pos_;
    return Token{TokenType::Semicolon, ";", pos_ - 1};
  }
  if (c == '*') {
    ++pos_;
    return Token{TokenType::Star, "*", pos_ - 1};
  }
  if (c == '=') {
    ++pos_;
    return Token{TokenType::Equal, "=", pos_ - 1};
  }
  if (c == '!') {
    if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
      pos_ += 2;
      return Token{TokenType::NotEqual, "!=", pos_ - 2};
    }
  }
  if (c == '<') {
    if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '>') {
      pos_ += 2;
      return Token{TokenType::NotEqual, "<>", pos_ - 2};
    }
  }
  if (c == '~') {
    ++pos_;
    return Token{TokenType::RegexMatch, "~", pos_ - 1};
  }
  if (c == '\'' || c == '\"') {
    return lex_string();
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return lex_number();
  }
  if (is_ident_start(c)) {
    return lex_identifier_or_keyword();
  }

  // WHY: advance on unknown input to avoid infinite loops on malformed queries.
  ++pos_;
  return Token{TokenType::End, "", pos_ - 1};
}

Token Lexer::lex_string() {
  size_t start = pos_;
  char quote = input_[pos_++];
  std::string out;
  while (pos_ < input_.size()) {
    char c = input_[pos_++];
    if (c == quote) {
      return Token{TokenType::String, out, start};
    }
    out.push_back(c);
  }
  return Token{TokenType::String, out, start};
}

Token Lexer::lex_identifier_or_keyword() {
  size_t start = pos_;
  std::string out;
  while (pos_ < input_.size() && is_ident_char(input_[pos_])) {
    out.push_back(input_[pos_++]);
  }
  std::string upper = to_upper(out);
  if (upper == "SELECT") return Token{TokenType::KeywordSelect, out, start};
  if (upper == "FROM") return Token{TokenType::KeywordFrom, out, start};
  if (upper == "WHERE") return Token{TokenType::KeywordWhere, out, start};
  if (upper == "AND") return Token{TokenType::KeywordAnd, out, start};
  if (upper == "OR") return Token{TokenType::KeywordOr, out, start};
  if (upper == "IN") return Token{TokenType::KeywordIn, out, start};
  if (upper == "EXISTS") return Token{TokenType::KeywordExists, out, start};
  if (upper == "DOCUMENT") return Token{TokenType::KeywordDocument, out, start};
  if (upper == "LIMIT") return Token{TokenType::KeywordLimit, out, start};
  if (upper == "EXCLUDE") return Token{TokenType::KeywordExclude, out, start};
  if (upper == "ORDER") return Token{TokenType::KeywordOrder, out, start};
  if (upper == "BY") return Token{TokenType::KeywordBy, out, start};
  if (upper == "ASC") return Token{TokenType::KeywordAsc, out, start};
  if (upper == "DESC") return Token{TokenType::KeywordDesc, out, start};
  if (upper == "AS") return Token{TokenType::KeywordAs, out, start};
  if (upper == "TO") return Token{TokenType::KeywordTo, out, start};
  if (upper == "LIST") return Token{TokenType::KeywordList, out, start};
  if (upper == "COUNT") return Token{TokenType::KeywordCount, out, start};
  if (upper == "TABLE") return Token{TokenType::KeywordTable, out, start};
  if (upper == "CSV") return Token{TokenType::KeywordCsv, out, start};
  if (upper == "PARQUET") return Token{TokenType::KeywordParquet, out, start};
  if (upper == "RAW") return Token{TokenType::KeywordRaw, out, start};
  if (upper == "FRAGMENTS") return Token{TokenType::KeywordFragments, out, start};
  if (upper == "CONTAINS") return Token{TokenType::KeywordContains, out, start};
  if (upper == "HAS_DIRECT_TEXT") return Token{TokenType::KeywordHasDirectText, out, start};
  if (upper == "ALL") return Token{TokenType::KeywordAll, out, start};
  if (upper == "ANY") return Token{TokenType::KeywordAny, out, start};
  if (upper == "IS") return Token{TokenType::KeywordIs, out, start};
  if (upper == "NOT") return Token{TokenType::KeywordNot, out, start};
  if (upper == "NULL") return Token{TokenType::KeywordNull, out, start};
  if (upper == "SHOW") return Token{TokenType::KeywordShow, out, start};
  if (upper == "DESCRIBE") return Token{TokenType::KeywordDescribe, out, start};
  if (upper == "INPUT") return Token{TokenType::KeywordInput, out, start};
  if (upper == "INPUTS") return Token{TokenType::KeywordInputs, out, start};
  if (upper == "FUNCTIONS") return Token{TokenType::KeywordFunctions, out, start};
  if (upper == "AXES") return Token{TokenType::KeywordAxes, out, start};
  if (upper == "OPERATORS") return Token{TokenType::KeywordOperators, out, start};
  return Token{TokenType::Identifier, out, start};
}

Token Lexer::lex_number() {
  size_t start = pos_;
  std::string out;
  while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
    out.push_back(input_[pos_++]);
  }
  return Token{TokenType::Number, out, start};
}

void Lexer::skip_ws() {
  while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
    ++pos_;
  }
}

bool Lexer::is_ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

std::string Lexer::to_upper(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

}  // namespace xsql
