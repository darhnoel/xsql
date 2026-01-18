#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../query_parser.h"
#include "lexer.h"

namespace xsql {

class Parser {
 public:
  explicit Parser(const std::string& input);
  ParseResult parse();

 private:
  bool parse_select_list(std::vector<Query::SelectItem>& items);
  bool parse_exclude_list(std::vector<std::string>& fields);
  bool parse_select_item(std::vector<Query::SelectItem>& items, bool& saw_field, bool& saw_tag_only);

  bool parse_source(Source& src);
  bool parse_subquery(std::shared_ptr<Query>& out);
  bool parse_query_body(Query& q);
  bool parse_show(Query& q);
  bool parse_describe(Query& q);
  bool parse_source_alias(Source& src);

  bool parse_expr(Expr& out);
  bool parse_and_expr(Expr& out);
  bool parse_cmp_expr(Expr& out);
  bool parse_operand(Operand& operand);
  bool parse_value_list(ValueList& values);
  bool parse_string_list(ValueList& values);

  bool parse_limit(size_t& limit);

  bool consume_identifier(const std::string& expected);
  bool consume(TokenType type, const std::string& message);
  bool set_error(const std::string& message);
  ParseResult error_result();

  void advance();
  Token peek();

  static bool starts_with(const std::string& s, const std::string& prefix);
  static std::string to_upper(const std::string& s);
  static std::string to_lower(const std::string& s);

  Lexer lexer_;
  Token current_{};
  Token peek_{};
  bool has_peek_ = false;
  std::optional<ParseError> error_;
};

}  // namespace xsql
