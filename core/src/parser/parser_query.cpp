#include "parser_internal.h"

namespace xsql {

/// Parses the core query clauses without enforcing the terminal token.
/// MUST parse SELECT/FROM and optional WHERE/ORDER/LIMIT/TO consistently.
/// Inputs are token streams; outputs are Query objects or errors.
bool Parser::parse_query_body(Query& q) {
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

/// Parses the LIMIT value as a non-negative integer.
/// MUST reject non-numeric values and invalid conversions.
/// Inputs are tokens; outputs are limit value or errors.
bool Parser::parse_limit(size_t& limit) {
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

}  // namespace xsql
