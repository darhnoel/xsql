#include "query_parser.h"

#include <cctype>
#include <memory>
#include <sstream>

namespace xsql {

namespace {

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
  KeywordDocument,
  KeywordLimit,
  KeywordOrder,
  KeywordBy,
  KeywordAsc,
  KeywordDesc,
  KeywordAs,
  KeywordTo,
  KeywordList,
  KeywordCount,
  KeywordTable,
  KeywordIs,
  KeywordNot,
  KeywordNull,
  Equal,
  NotEqual,
  RegexMatch
};

struct Token {
  TokenType type;
  std::string text;
  size_t pos = 0;
};

class Lexer {
 public:
  explicit Lexer(const std::string& input) : input_(input) {}

  Token next() {
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

    ++pos_;
    return Token{TokenType::End, "", pos_ - 1};
  }

 private:
  Token lex_string() {
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

  Token lex_identifier_or_keyword() {
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
    if (upper == "DOCUMENT") return Token{TokenType::KeywordDocument, out, start};
    if (upper == "LIMIT") return Token{TokenType::KeywordLimit, out, start};
    if (upper == "ORDER") return Token{TokenType::KeywordOrder, out, start};
    if (upper == "BY") return Token{TokenType::KeywordBy, out, start};
    if (upper == "ASC") return Token{TokenType::KeywordAsc, out, start};
    if (upper == "DESC") return Token{TokenType::KeywordDesc, out, start};
    if (upper == "AS") return Token{TokenType::KeywordAs, out, start};
    if (upper == "TO") return Token{TokenType::KeywordTo, out, start};
    if (upper == "LIST") return Token{TokenType::KeywordList, out, start};
    if (upper == "COUNT") return Token{TokenType::KeywordCount, out, start};
    if (upper == "TABLE") return Token{TokenType::KeywordTable, out, start};
    if (upper == "IS") return Token{TokenType::KeywordIs, out, start};
    if (upper == "NOT") return Token{TokenType::KeywordNot, out, start};
    if (upper == "NULL") return Token{TokenType::KeywordNull, out, start};
    return Token{TokenType::Identifier, out, start};
  }

  Token lex_number() {
    size_t start = pos_;
    std::string out;
    while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
      out.push_back(input_[pos_++]);
    }
    return Token{TokenType::Number, out, start};
  }

  void skip_ws() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
      ++pos_;
    }
  }

  static bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
  }

  static bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
  }

  static std::string to_upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
  }

  const std::string& input_;
  size_t pos_ = 0;
};

class Parser {
 public:
  explicit Parser(const std::string& input) : lexer_(input) { advance(); }

  ParseResult parse() {
    Query q;
    if (!consume(TokenType::KeywordSelect, "Expected SELECT")) return error_result();
    if (!parse_select_list(q.select_items)) return error_result();
    if (!consume(TokenType::KeywordFrom, "Expected FROM")) return error_result();
    if (!parse_source(q.source)) return error_result();

    if (current_.type == TokenType::KeywordWhere) {
      advance();
      Expr expr;
      if (!parse_expr(expr)) return error_result();
      q.where = expr;
    }

    if (current_.type == TokenType::KeywordOrder) {
      advance();
      if (!consume(TokenType::KeywordBy, "Expected BY after ORDER")) return error_result();
      while (true) {
        Query::OrderBy order_by;
        if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordCount) {
          set_error("Expected field after ORDER BY");
          return error_result();
        }
        if (current_.type == TokenType::KeywordCount) {
          order_by.field = "count";
        } else {
          order_by.field = to_lower(current_.text);
        }
        order_by.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        if (current_.type == TokenType::KeywordAsc) {
          advance();
        } else if (current_.type == TokenType::KeywordDesc) {
          order_by.descending = true;
          advance();
        }
        q.order_by.push_back(order_by);
        if (current_.type == TokenType::Comma) {
          advance();
          continue;
        }
        break;
      }
    }

    if (current_.type == TokenType::KeywordLimit) {
      advance();
      size_t limit = 0;
      if (!parse_limit(limit)) return error_result();
      q.limit = limit;
    }

    if (current_.type == TokenType::KeywordTo) {
      advance();
      if (current_.type == TokenType::KeywordList) {
        advance();
        if (!consume(TokenType::LParen, "Expected ( after LIST")) return error_result();
        if (!consume(TokenType::RParen, "Expected ) after LIST(")) return error_result();
        q.to_list = true;
      } else if (current_.type == TokenType::KeywordTable) {
        advance();
        if (!consume(TokenType::LParen, "Expected ( after TABLE")) return error_result();
        if (!consume(TokenType::RParen, "Expected ) after TABLE(")) return error_result();
        q.to_table = true;
      } else {
        set_error("Expected LIST or TABLE after TO");
        return error_result();
      }
    }

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

 private:
  bool parse_select_list(std::vector<Query::SelectItem>& items) {
    bool saw_field = false;
    bool saw_tag_only = false;
    if (!parse_select_item(items, saw_field, saw_tag_only)) return false;
    while (current_.type == TokenType::Comma) {
      advance();
      if (!parse_select_item(items, saw_field, saw_tag_only)) return false;
    }
    if (saw_field && saw_tag_only) {
      return set_error("Cannot mix tag-only and projected fields in SELECT");
    }
    return true;
  }

  bool parse_select_item(std::vector<Query::SelectItem>& items, bool& saw_field, bool& saw_tag_only) {
    if (current_.type == TokenType::Identifier && to_upper(current_.text) == "SUMMARIZE") {
      Query::SelectItem item;
      item.aggregate = Query::SelectItem::Aggregate::Summarize;
      size_t start = current_.pos;
      advance();
      if (!consume(TokenType::LParen, "Expected ( after SUMMARIZE")) return false;
      if (current_.type != TokenType::Star) {
        return set_error("Expected * inside SUMMARIZE()");
      }
      item.tag = "*";
      item.span = Span{start, current_.pos + 1};
      advance();
      if (!consume(TokenType::RParen, "Expected ) after SUMMARIZE argument")) return false;
      items.push_back(item);
      saw_field = true;
      return true;
    }
    if (current_.type == TokenType::Identifier && to_upper(current_.text) == "TRIM") {
      Query::SelectItem item;
      size_t start = current_.pos;
      advance();
      if (!consume(TokenType::LParen, "Expected ( after TRIM")) return false;
      if (current_.type == TokenType::Identifier && to_upper(current_.text) == "INNER_HTML") {
        size_t inner_start = current_.pos;
        advance();
        if (!consume(TokenType::LParen, "Expected ( after inner_html")) return false;
        if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
          return set_error("Expected tag identifier inside inner_html()");
        }
        item.tag = current_.text;
        item.field = "inner_html";
        item.inner_html_function = true;
        advance();
        if (current_.type == TokenType::Comma) {
          advance();
          if (current_.type != TokenType::Number) {
            return set_error("Expected numeric depth in inner_html()");
          }
          try {
            item.inner_html_depth = static_cast<size_t>(std::stoull(current_.text));
          } catch (...) {
            return set_error("Invalid inner_html() depth");
          }
          advance();
        }
        if (!consume(TokenType::RParen, "Expected ) after inner_html argument")) return false;
        item.span = Span{inner_start, current_.pos};
      } else if (current_.type == TokenType::Identifier && to_upper(current_.text) == "TEXT") {
        size_t text_start = current_.pos;
        advance();
        if (!consume(TokenType::LParen, "Expected ( after text")) return false;
        if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
          return set_error("Expected tag identifier inside text()");
        }
        item.tag = current_.text;
        item.field = "text";
        item.text_function = true;
        advance();
        if (!consume(TokenType::RParen, "Expected ) after text argument")) return false;
        item.span = Span{text_start, current_.pos};
      } else {
        if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
          return set_error("Expected tag identifier inside TRIM()");
        }
        item.tag = current_.text;
        advance();
        if (current_.type != TokenType::Dot) {
          return set_error("Expected field after tag inside TRIM()");
        }
        advance();
        if (current_.type != TokenType::Identifier) {
          return set_error("Expected field identifier after '.'");
        }
        item.field = current_.text;
        advance();
      }
      if (!consume(TokenType::RParen, "Expected ) after TRIM argument")) return false;
      item.trim = true;
      item.span = Span{start, current_.pos};
      items.push_back(item);
      saw_field = true;
      return true;
    }
    if (current_.type == TokenType::KeywordCount) {
      Query::SelectItem item;
      item.aggregate = Query::SelectItem::Aggregate::Count;
      size_t start = current_.pos;
      advance();
      if (!consume(TokenType::LParen, "Expected ( after COUNT")) return false;
      if (current_.type == TokenType::Star) {
        item.tag = "*";
        item.field = "count";
        item.span = Span{start, current_.pos + 1};
        advance();
        if (!consume(TokenType::RParen, "Expected ) after COUNT argument")) return false;
        items.push_back(item);
        saw_field = true;
        return true;
      }
      if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
        return set_error("Expected tag identifier inside COUNT()");
      }
      item.tag = current_.text;
      item.field = "count";
      item.span = Span{start, current_.pos + current_.text.size()};
      advance();
      if (!consume(TokenType::RParen, "Expected ) after COUNT argument")) return false;
      items.push_back(item);
      saw_field = true;
      return true;
    }
    if (current_.type == TokenType::Star) {
      Query::SelectItem item;
      item.tag = "*";
      item.span = Span{current_.pos, current_.pos + 1};
      advance();
      items.push_back(item);
      saw_tag_only = true;
      return true;
    }
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier");
    }
    Token tag_token = current_;
    size_t start = current_.pos;
    advance();
    if (to_upper(tag_token.text) == "TEXT" && current_.type == TokenType::LParen) {
      Query::SelectItem item;
      item.field = "text";
      item.text_function = true;
      advance();
      if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
        return set_error("Expected tag identifier inside text()");
      }
      item.tag = current_.text;
      advance();
      if (!consume(TokenType::RParen, "Expected ) after text argument")) return false;
      item.span = Span{start, current_.pos};
      items.push_back(item);
      saw_field = true;
      return true;
    }
    if (to_upper(tag_token.text) == "INNER_HTML" && current_.type == TokenType::LParen) {
      Query::SelectItem item;
      item.field = "inner_html";
      item.inner_html_function = true;
      advance();
      if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
        return set_error("Expected tag identifier inside inner_html()");
      }
      item.tag = current_.text;
      advance();
      if (current_.type == TokenType::Comma) {
        advance();
        if (current_.type != TokenType::Number) {
          return set_error("Expected numeric depth in inner_html()");
        }
        try {
          item.inner_html_depth = static_cast<size_t>(std::stoull(current_.text));
        } catch (...) {
          return set_error("Invalid inner_html() depth");
        }
        advance();
      }
      if (!consume(TokenType::RParen, "Expected ) after inner_html argument")) return false;
      item.span = Span{start, current_.pos};
      items.push_back(item);
      saw_field = true;
      return true;
    }
    Query::SelectItem item;
    item.tag = tag_token.text;
    if (current_.type == TokenType::LParen) {
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected field identifier inside tag()");
      }
      while (true) {
        if (current_.type != TokenType::Identifier) {
          return set_error("Expected field identifier inside tag()");
        }
        Query::SelectItem field_item;
        field_item.tag = tag_token.text;
        field_item.field = current_.text;
        field_item.span = Span{start, current_.pos + current_.text.size()};
        items.push_back(field_item);
        saw_field = true;
        advance();
        if (current_.type == TokenType::Comma) {
          advance();
          continue;
        }
        if (current_.type == TokenType::RParen) {
          advance();
          break;
        }
        return set_error("Expected , or ) after field identifier");
      }
      return true;
    }
    if (current_.type == TokenType::Dot) {
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected field identifier after '.'");
      }
      item.field = current_.text;
      item.span = Span{start, current_.pos + current_.text.size()};
      advance();
      saw_field = true;
    } else {
      item.span = Span{start, current_.pos};
      saw_tag_only = true;
    }
    items.push_back(item);
    return true;
  }

  bool parse_source(Source& src) {
    if (current_.type == TokenType::KeywordDocument) {
      src.kind = Source::Kind::Document;
      src.value = "document";
      src.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
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
    return set_error("Expected document, alias, or string literal source");
  }

  bool parse_source_alias(Source& src) {
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

  bool parse_expr(Expr& out) {
    Expr left;
    if (!parse_and_expr(left)) return false;
    while (current_.type == TokenType::KeywordOr) {
      Token op = current_;
      advance();
      Expr right;
      if (!parse_and_expr(right)) return false;
      auto node = std::make_shared<BinaryExpr>();
      node->op = BinaryExpr::Op::Or;
      node->left = left;
      node->right = right;
      node->span = Span{op.pos, current_.pos};
      left = node;
    }
    out = left;
    return true;
  }

  bool parse_and_expr(Expr& out) {
    Expr left;
    if (!parse_cmp_expr(left)) return false;
    while (current_.type == TokenType::KeywordAnd) {
      Token op = current_;
      advance();
      Expr right;
      if (!parse_cmp_expr(right)) return false;
      auto node = std::make_shared<BinaryExpr>();
      node->op = BinaryExpr::Op::And;
      node->left = left;
      node->right = right;
      node->span = Span{op.pos, current_.pos};
      left = node;
    }
    out = left;
    return true;
  }

  bool parse_cmp_expr(Expr& out) {
    Operand operand;
    if (!parse_operand(operand)) return false;

    CompareExpr cmp;
    cmp.lhs = operand;
    if (current_.type == TokenType::Equal) {
      cmp.op = CompareExpr::Op::Eq;
      advance();
      ValueList values;
      if (!parse_value_list(values)) return false;
      cmp.rhs = values;
      out = cmp;
      return true;
    }
    if (current_.type == TokenType::KeywordIn) {
      cmp.op = CompareExpr::Op::In;
      advance();
      ValueList values;
      if (!parse_value_list(values)) return false;
      cmp.rhs = values;
      out = cmp;
      return true;
    }
    if (current_.type == TokenType::NotEqual) {
      cmp.op = CompareExpr::Op::NotEq;
      advance();
      ValueList values;
      if (!parse_value_list(values)) return false;
      cmp.rhs = values;
      out = cmp;
      return true;
    }
    if (current_.type == TokenType::RegexMatch) {
      cmp.op = CompareExpr::Op::Regex;
      advance();
      ValueList values;
      if (!parse_value_list(values)) return false;
      if (values.values.size() != 1) return set_error("Regex expects a single pattern");
      cmp.rhs = values;
      out = cmp;
      return true;
    }
    if (current_.type == TokenType::KeywordIs) {
      advance();
      if (current_.type == TokenType::KeywordNot) {
        advance();
        if (!consume(TokenType::KeywordNull, "Expected NULL after IS NOT")) return false;
        cmp.op = CompareExpr::Op::IsNotNull;
        out = cmp;
        return true;
      }
      if (!consume(TokenType::KeywordNull, "Expected NULL after IS")) return false;
      cmp.op = CompareExpr::Op::IsNull;
      out = cmp;
      return true;
    }
    return set_error("Expected =, <>, ~, IN, or IS");
  }

  bool parse_operand(Operand& operand) {
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected identifier");
    }
    if (to_upper(current_.text) == "ATTRIBUTES") {
      advance();
      if (current_.type == TokenType::Dot) {
        advance();
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Self;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::AttributesMap;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      return true;
    }
    if (to_upper(current_.text) == "TEXT") {
      advance();
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::Text;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      return true;
    }
    if (to_upper(current_.text) == "TAG") {
      advance();
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::Tag;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      return true;
    }
    if (to_upper(current_.text) == "NODE_ID") {
      advance();
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::NodeId;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      return true;
    }
    if (to_upper(current_.text) == "PARENT_ID") {
      advance();
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::ParentId;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      return true;
    }
    if (to_upper(current_.text) == "PARENT") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after parent")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, or parent_id after parent");
      std::string next = to_upper(current_.text);
      if (next == "ATTRIBUTES") {
        advance();
        if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Parent;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID") {
        operand.axis = Operand::Axis::Parent;
        if (next == "TAG") {
          operand.field_kind = Operand::FieldKind::Tag;
        } else if (next == "TEXT") {
          operand.field_kind = Operand::FieldKind::Text;
        } else if (next == "NODE_ID") {
          operand.field_kind = Operand::FieldKind::NodeId;
        } else {
          operand.field_kind = Operand::FieldKind::ParentId;
        }
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      return set_error("Expected attributes, tag, text, or parent_id after parent");
    }
    if (to_upper(current_.text) == "CHILD") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after child")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, node_id, or parent_id after child");
      std::string next = to_upper(current_.text);
      if (next == "ATTRIBUTES") {
        advance();
        if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Child;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID") {
        operand.axis = Operand::Axis::Child;
        if (next == "TAG") {
          operand.field_kind = Operand::FieldKind::Tag;
        } else if (next == "TEXT") {
          operand.field_kind = Operand::FieldKind::Text;
        } else if (next == "NODE_ID") {
          operand.field_kind = Operand::FieldKind::NodeId;
        } else {
          operand.field_kind = Operand::FieldKind::ParentId;
        }
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      return set_error("Expected attributes, tag, text, or parent_id after child");
    }
    if (to_upper(current_.text) == "ANCESTOR") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after ancestor")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, node_id, or parent_id after ancestor");
      std::string next = to_upper(current_.text);
      if (next == "ATTRIBUTES") {
        advance();
        if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Ancestor;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID") {
        operand.axis = Operand::Axis::Ancestor;
        if (next == "TAG") {
          operand.field_kind = Operand::FieldKind::Tag;
        } else if (next == "TEXT") {
          operand.field_kind = Operand::FieldKind::Text;
        } else if (next == "NODE_ID") {
          operand.field_kind = Operand::FieldKind::NodeId;
        } else {
          operand.field_kind = Operand::FieldKind::ParentId;
        }
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      return set_error("Expected attributes, tag, text, or parent_id after ancestor");
    }
    if (to_upper(current_.text) == "DESCENDANT") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after descendant")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, node_id, or parent_id after descendant");
      std::string next = to_upper(current_.text);
      if (next == "ATTRIBUTES") {
        advance();
        if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Descendant;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID") {
        operand.axis = Operand::Axis::Descendant;
        if (next == "TAG") {
          operand.field_kind = Operand::FieldKind::Tag;
        } else if (next == "TEXT") {
          operand.field_kind = Operand::FieldKind::Text;
        } else if (next == "NODE_ID") {
          operand.field_kind = Operand::FieldKind::NodeId;
        } else {
          operand.field_kind = Operand::FieldKind::ParentId;
        }
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      return set_error("Expected attributes, tag, text, or parent_id after descendant");
    }
    std::string qualifier = current_.text;
    advance();
    if (current_.type != TokenType::Dot) {
      if (to_upper(qualifier) == "TEXT") {
        operand.axis = Operand::Axis::Self;
        operand.field_kind = Operand::FieldKind::Text;
        operand.span = Span{current_.pos, current_.pos};
        return true;
      }
      if (to_upper(qualifier) == "TAG") {
        operand.axis = Operand::Axis::Self;
        operand.field_kind = Operand::FieldKind::Tag;
        operand.span = Span{current_.pos, current_.pos};
        return true;
      }
      if (to_upper(qualifier) == "NODE_ID") {
        operand.axis = Operand::Axis::Self;
        operand.field_kind = Operand::FieldKind::NodeId;
        operand.span = Span{current_.pos, current_.pos};
        return true;
      }
      if (to_upper(qualifier) == "PARENT_ID") {
        operand.axis = Operand::Axis::Self;
        operand.field_kind = Operand::FieldKind::ParentId;
        operand.span = Span{current_.pos, current_.pos};
        return true;
      }
      operand.attribute = qualifier;
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    advance();
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected attributes, parent, or attribute name after qualifier");
    }
    if (to_upper(current_.text) == "ATTRIBUTES") {
      advance();
      if (current_.type == TokenType::Dot) {
        advance();
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Self;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::AttributesMap;
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      return true;
    }
    if (to_upper(current_.text) == "PARENT_ID") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::ParentId;
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (to_upper(current_.text) == "NODE_ID") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::NodeId;
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (to_upper(current_.text) == "PARENT") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after parent")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, node_id, or parent_id after parent");
      std::string next = to_upper(current_.text);
      if (next == "ATTRIBUTES") {
        advance();
        if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Parent;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID") {
        operand.axis = Operand::Axis::Parent;
        if (next == "TAG") {
          operand.field_kind = Operand::FieldKind::Tag;
        } else if (next == "TEXT") {
          operand.field_kind = Operand::FieldKind::Text;
        } else if (next == "NODE_ID") {
          operand.field_kind = Operand::FieldKind::NodeId;
        } else {
          operand.field_kind = Operand::FieldKind::ParentId;
        }
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      return set_error("Expected attributes, tag, text, or parent_id after parent");
    }
    if (to_upper(current_.text) == "CHILD") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after child")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, or parent_id after child");
      std::string next = to_upper(current_.text);
      if (next == "ATTRIBUTES") {
        advance();
        if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Child;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID") {
        operand.axis = Operand::Axis::Child;
        if (next == "TAG") {
          operand.field_kind = Operand::FieldKind::Tag;
        } else if (next == "TEXT") {
          operand.field_kind = Operand::FieldKind::Text;
        } else if (next == "NODE_ID") {
          operand.field_kind = Operand::FieldKind::NodeId;
        } else {
          operand.field_kind = Operand::FieldKind::ParentId;
        }
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      return set_error("Expected attributes, tag, text, or parent_id after child");
    }
    if (to_upper(current_.text) == "ANCESTOR") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after ancestor")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, or parent_id after ancestor");
      std::string next = to_upper(current_.text);
      if (next == "ATTRIBUTES") {
        advance();
        if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Ancestor;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID") {
        operand.axis = Operand::Axis::Ancestor;
        if (next == "TAG") {
          operand.field_kind = Operand::FieldKind::Tag;
        } else if (next == "TEXT") {
          operand.field_kind = Operand::FieldKind::Text;
        } else if (next == "NODE_ID") {
          operand.field_kind = Operand::FieldKind::NodeId;
        } else {
          operand.field_kind = Operand::FieldKind::ParentId;
        }
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      return set_error("Expected attributes, tag, text, or parent_id after ancestor");
    }
    if (to_upper(current_.text) == "DESCENDANT") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after descendant")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, or parent_id after descendant");
      std::string next = to_upper(current_.text);
      if (next == "ATTRIBUTES") {
        advance();
        if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
        operand.attribute = current_.text;
        operand.axis = Operand::Axis::Descendant;
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID") {
        operand.axis = Operand::Axis::Descendant;
        if (next == "TAG") {
          operand.field_kind = Operand::FieldKind::Tag;
        } else if (next == "TEXT") {
          operand.field_kind = Operand::FieldKind::Text;
        } else if (next == "NODE_ID") {
          operand.field_kind = Operand::FieldKind::NodeId;
        } else {
          operand.field_kind = Operand::FieldKind::ParentId;
        }
        operand.qualifier = qualifier;
        operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      return set_error("Expected attributes, tag, text, or parent_id after descendant");
    }
    operand.attribute = current_.text;
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::Attribute;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }

  bool parse_value_list(ValueList& values) {
    if (current_.type == TokenType::String) {
      values.values.push_back(current_.text);
      values.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (current_.type == TokenType::Number) {
      values.values.push_back(current_.text);
      values.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (current_.type == TokenType::LParen) {
      size_t start = current_.pos;
      advance();
      if (current_.type != TokenType::String && current_.type != TokenType::Number) {
        return set_error("Expected string literal or number");
      }
      values.values.push_back(current_.text);
      advance();
      while (current_.type == TokenType::Comma) {
        advance();
        if (current_.type != TokenType::String && current_.type != TokenType::Number) {
          return set_error("Expected string literal or number");
        }
        values.values.push_back(current_.text);
        advance();
      }
      if (!consume(TokenType::RParen, "Expected )")) return false;
      values.span = Span{start, current_.pos};
      return true;
    }
    return set_error("Expected literal or list");
  }

  bool parse_limit(size_t& limit) {
    if (current_.type != TokenType::Number) {
      return set_error("Expected numeric literal after LIMIT");
    }
    try {
      limit = static_cast<size_t>(std::stoull(current_.text));
    } catch (...) {
      return set_error("Invalid LIMIT value");
    }
    advance();
    return true;
  }

  bool consume_identifier(const std::string& expected) {
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected identifier");
    }
    if (to_upper(current_.text) != to_upper(expected)) {
      return set_error("Expected identifier: " + expected);
    }
    advance();
    return true;
  }

  bool consume(TokenType type, const std::string& message) {
    if (current_.type != type) {
      return set_error(message);
    }
    advance();
    return true;
  }

  bool set_error(const std::string& message) {
    error_ = ParseError{message, current_.pos};
    return false;
  }

  ParseResult error_result() {
    ParseResult res;
    res.error = error_;
    return res;
  }

  void advance() { current_ = lexer_.next(); }

  static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
  }

  static std::string to_upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
  }

  static std::string to_lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
  }

  Lexer lexer_;
  Token current_{};
  std::optional<ParseError> error_;
};

}  // namespace

ParseResult parse_query(const std::string& input) {
  Parser parser(input);
  return parser.parse();
}

}  // namespace xsql
