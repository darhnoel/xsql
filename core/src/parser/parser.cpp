#include "query_parser_impl.h"

#include <cctype>
#include <memory>
#include <sstream>

#include "lexer.h"

namespace xsql {

namespace {

/// Implements recursive-descent parsing over the token stream.
/// MUST preserve token order and MUST set error_ on first failure.
/// Inputs are lexer tokens; outputs are ParseResult with no side effects.
class Parser {
 public:
  /// Constructs a parser for a given query input.
  /// MUST immediately read the first token to initialize state.
  /// Inputs are query strings; side effects include token consumption.
  explicit Parser(const std::string& input) : lexer_(input) { advance(); }

  /// Parses a full query and returns either a Query or a ParseError.
  /// MUST consume all tokens or report an unexpected trailing token.
  /// Inputs are internal state; outputs are ParseResult.
  ParseResult parse() {
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

 private:
  /// Parses the SELECT projection list, enforcing projection rules.
  /// MUST reject mixing tag-only and projected fields.
  /// Inputs are token stream; outputs are select items or errors.
  bool parse_select_list(std::vector<Query::SelectItem>& items) {
    bool saw_field = false;
    bool saw_tag_only = false;
    if (!parse_select_item(items, saw_field, saw_tag_only)) return false;
    while (current_.type == TokenType::Comma) {
      advance();
      if (!parse_select_item(items, saw_field, saw_tag_only)) return false;
    }
    // WHY: mixing tag-only and projected fields breaks output schema invariants.
    if (saw_field && saw_tag_only) {
      return set_error("Cannot mix tag-only and projected fields in SELECT");
    }
    return true;
  }

  /// Parses EXCLUDE fields for SELECT * projections.
  /// MUST accept a single field or a parenthesized list.
  /// Inputs are tokens; outputs are field names or errors.
  bool parse_exclude_list(std::vector<std::string>& fields) {
    if (current_.type == TokenType::Identifier) {
      fields.push_back(to_lower(current_.text));
      advance();
      return true;
    }
    if (current_.type == TokenType::LParen) {
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected field name in EXCLUDE list");
      }
      while (true) {
        if (current_.type != TokenType::Identifier) {
          return set_error("Expected field name in EXCLUDE list");
        }
        fields.push_back(to_lower(current_.text));
        advance();
        if (current_.type == TokenType::Comma) {
          advance();
          continue;
        }
        if (current_.type == TokenType::RParen) {
          advance();
          break;
        }
        return set_error("Expected , or ) after EXCLUDE field");
      }
      return true;
    }
    return set_error("Expected field name or list after EXCLUDE");
  }

  /// Parses a single select item including functions and aggregates.
  /// MUST set saw_field/saw_tag_only consistently for validation.
  /// Inputs are tokens; outputs are select items or errors.
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

  /// Parses the FROM source, supporting document, path, or URL forms.
  /// MUST normalize source kinds and capture spans for errors.
  /// Inputs are tokens; outputs are Source or errors.
  bool parse_source(Source& src) {
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
  bool parse_subquery(std::shared_ptr<Query>& out) {
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

  /// Parses the core query clauses without enforcing the terminal token.
  /// MUST parse SELECT/FROM and optional WHERE/ORDER/LIMIT/TO consistently.
  /// Inputs are token streams; outputs are Query objects or errors.
  bool parse_query_body(Query& q) {
    if (!consume(TokenType::KeywordSelect, "Expected SELECT")) return false;
    if (!parse_select_list(q.select_items)) return false;
    if (current_.type == TokenType::KeywordExclude) {
      advance();
      if (!parse_exclude_list(q.exclude_fields)) return false;
    }
    if (!consume(TokenType::KeywordFrom, "Expected FROM")) return false;
    if (!parse_source(q.source)) return false;

    if (current_.type == TokenType::KeywordWhere) {
      advance();
      Expr expr;
      if (!parse_expr(expr)) return false;
      q.where = expr;
    }

    if (current_.type == TokenType::KeywordOrder) {
      advance();
      if (!consume(TokenType::KeywordBy, "Expected BY after ORDER")) return false;
      while (true) {
        Query::OrderBy order_by;
        if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordCount) {
          set_error("Expected field after ORDER BY");
          return false;
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
      if (!parse_limit(limit)) return false;
      q.limit = limit;
    }

    if (current_.type == TokenType::KeywordTo) {
      advance();
      if (current_.type == TokenType::KeywordList) {
        advance();
        if (!consume(TokenType::LParen, "Expected ( after LIST")) return false;
        if (!consume(TokenType::RParen, "Expected ) after LIST(")) return false;
        q.to_list = true;
      } else if (current_.type == TokenType::KeywordTable) {
        advance();
        if (!consume(TokenType::LParen, "Expected ( after TABLE")) return false;
        if (!consume(TokenType::RParen, "Expected ) after TABLE(")) return false;
        q.to_table = true;
      } else if (current_.type == TokenType::KeywordCsv || current_.type == TokenType::KeywordParquet) {
        Query::ExportSink sink;
        sink.kind = (current_.type == TokenType::KeywordCsv)
                        ? Query::ExportSink::Kind::Csv
                        : Query::ExportSink::Kind::Parquet;
        size_t start = current_.pos;
        advance();
        if (!consume(TokenType::LParen, "Expected ( after export target")) return false;
        if (current_.type != TokenType::String) {
          set_error("Expected string literal path inside export target");
          return false;
        }
        sink.path = current_.text;
        sink.span = Span{start, current_.pos + current_.text.size()};
        advance();
        if (!consume(TokenType::RParen, "Expected ) after export path")) return false;
        q.export_sink = sink;
      } else {
        set_error("Expected LIST, TABLE, CSV, or PARQUET after TO");
        return false;
      }
    }
    return true;
  }

  /// Parses an optional alias after a source.
  /// MUST accept AS or bare identifiers for aliasing.
  /// Inputs are tokens; outputs are updated Source or errors.
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

  /// Parses an expression with OR precedence.
  /// MUST build BinaryExpr nodes in left-associative order.
  /// Inputs are tokens; outputs are Expr or errors.
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

  /// Parses an expression with AND precedence.
  /// MUST build BinaryExpr nodes in left-associative order.
  /// Inputs are tokens; outputs are Expr or errors.
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

  /// Parses comparison predicates and NULL checks.
  /// MUST validate operators and value list cardinality.
  /// Inputs are tokens; outputs are CompareExpr or errors.
  bool parse_cmp_expr(Expr& out) {
    if (current_.type == TokenType::LParen) {
      advance();
      Expr inner;
      if (!parse_expr(inner)) return false;
      if (!consume(TokenType::RParen, "Expected ) to close expression")) return false;
      out = inner;
      return true;
    }
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

  /// Parses an operand with axes, fields, and qualifiers.
  /// MUST set axis/field_kind consistently with grammar.
  /// Inputs are tokens; outputs are Operand or errors.
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

  /// Parses a literal or list of literals for comparisons.
  /// MUST accept strings/numbers and enforce list delimiters.
  /// Inputs are tokens; outputs are ValueList or errors.
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

  /// Parses the LIMIT value as a non-negative integer.
  /// MUST reject non-numeric values and invalid conversions.
  /// Inputs are tokens; outputs are limit value or errors.
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

  /// Consumes an identifier matching the expected string.
  /// MUST enforce exact match and report errors on mismatch.
  /// Inputs are tokens/expected; outputs are success or error.
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

  /// Consumes a token of the expected type or sets a parse error.
  /// MUST advance the token stream on success.
  /// Inputs are token type/message; outputs are success or error.
  bool consume(TokenType type, const std::string& message) {
    if (current_.type != type) {
      return set_error(message);
    }
    advance();
    return true;
  }

  /// Records the first parse error for reporting.
  /// MUST preserve the earliest error position for clarity.
  /// Inputs are error message; outputs are false with stored error.
  bool set_error(const std::string& message) {
    error_ = ParseError{message, current_.pos};
    return false;
  }

  /// Produces a ParseResult using the recorded error.
  /// MUST return an empty query with the stored error.
  /// Inputs are internal state; outputs are ParseResult.
  ParseResult error_result() {
    ParseResult res;
    res.error = error_;
    return res;
  }

  /// Advances to the next token in the input stream.
  /// MUST be called after consuming tokens to keep state in sync.
  /// Inputs are internal state; outputs are updated current_.
  void advance() { current_ = lexer_.next(); }

  /// Tests whether a string starts with a given prefix.
  /// MUST be exact and ASCII-safe for parser purposes.
  /// Inputs are strings; outputs are boolean with no side effects.
  static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
  }

  /// Normalizes strings to uppercase for keyword checks.
  /// MUST avoid locale-sensitive behavior for determinism.
  /// Inputs are strings; outputs are uppercase strings.
  static std::string to_upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
  }

  /// Normalizes strings to lowercase for matching fields and tags.
  /// MUST avoid locale-sensitive behavior for determinism.
  /// Inputs are strings; outputs are lowercase strings.
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

/// Entry point for parsing that constructs a parser instance.
/// MUST not throw on parse errors and MUST return ParseResult consistently.
/// Inputs are query text; outputs are ParseResult with optional error.
ParseResult parse_query_impl(const std::string& input) {
  Parser parser(input);
  return parser.parse();
}

}  // namespace xsql
