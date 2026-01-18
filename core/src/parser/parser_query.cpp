#include "parser_internal.h"

namespace xsql {

/// Parses the core query clauses without enforcing the terminal token.
/// MUST parse SELECT/FROM and optional WHERE/ORDER/LIMIT/TO consistently.
/// Inputs are token streams; outputs are Query objects or errors.
bool Parser::parse_query_body(Query& q) {
  if (current_.type == TokenType::KeywordShow) {
    return parse_show(q);
  }
  if (current_.type == TokenType::KeywordDescribe) {
    return parse_describe(q);
  }
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
      bool saw_header = false;
      bool saw_export = false;
      if (current_.type != TokenType::RParen) {
        while (true) {
          if (current_.type != TokenType::Identifier) {
            return set_error("Expected HEADER, NOHEADER, or EXPORT inside TABLE()");
          }
          size_t option_start = current_.pos;
          std::string option = to_upper(current_.text);
          advance();
          if (option == "HEADER") {
            if (current_.type == TokenType::Equal) {
              advance();
            }
            if (current_.type == TokenType::Comma || current_.type == TokenType::RParen) {
              q.table_has_header = true;
            } else {
              if (current_.type != TokenType::Identifier) {
                return set_error("Expected ON or OFF after HEADER");
              }
              std::string value = to_upper(current_.text);
              if (value == "ON") {
                q.table_has_header = true;
              } else if (value == "OFF") {
                q.table_has_header = false;
              } else {
                return set_error("Expected ON or OFF after HEADER");
              }
              advance();
            }
            if (saw_header) {
              return set_error("Duplicate HEADER option inside TABLE()");
            }
            saw_header = true;
          } else if (option == "NOHEADER" || option == "NO_HEADER") {
            if (saw_header) {
              return set_error("Duplicate HEADER option inside TABLE()");
            }
            q.table_has_header = false;
            saw_header = true;
          } else if (option == "EXPORT") {
            if (current_.type == TokenType::Equal) {
              advance();
            }
            if (current_.type != TokenType::String) {
              return set_error("Expected string literal after EXPORT");
            }
            if (saw_export) {
              return set_error("Duplicate EXPORT option inside TABLE()");
            }
            Query::ExportSink sink;
            sink.kind = Query::ExportSink::Kind::Csv;
            sink.path = current_.text;
            sink.span = Span{option_start, current_.pos + current_.text.size()};
            q.export_sink = sink;
            saw_export = true;
            advance();
          } else {
            return set_error("Expected HEADER, NOHEADER, or EXPORT inside TABLE()");
          }
          if (current_.type == TokenType::Comma) {
            advance();
            continue;
          }
          break;
        }
      }
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

bool Parser::parse_show(Query& q) {
  size_t start = current_.pos;
  advance();
  if (current_.type == TokenType::KeywordInput) {
    q.kind = Query::Kind::ShowInput;
    advance();
  } else if (current_.type == TokenType::KeywordInputs) {
    q.kind = Query::Kind::ShowInputs;
    advance();
  } else if (current_.type == TokenType::KeywordFunctions) {
    q.kind = Query::Kind::ShowFunctions;
    advance();
  } else if (current_.type == TokenType::KeywordAxes) {
    q.kind = Query::Kind::ShowAxes;
    advance();
  } else if (current_.type == TokenType::KeywordOperators) {
    q.kind = Query::Kind::ShowOperators;
    advance();
  } else {
    return set_error("Expected INPUT, INPUTS, FUNCTIONS, AXES, or OPERATORS after SHOW");
  }
  q.span = Span{start, current_.pos};
  return true;
}

bool Parser::parse_describe(Query& q) {
  size_t start = current_.pos;
  advance();
  if (current_.type == TokenType::KeywordDocument) {
    q.kind = Query::Kind::DescribeDoc;
    advance();
    q.span = Span{start, current_.pos};
    return true;
  }
  if (current_.type == TokenType::Identifier) {
    std::string target = to_lower(current_.text);
    if (target == "doc" || target == "document") {
      q.kind = Query::Kind::DescribeDoc;
      advance();
      q.span = Span{start, current_.pos};
      return true;
    }
    if (target == "language") {
      q.kind = Query::Kind::DescribeLanguage;
      advance();
      q.span = Span{start, current_.pos};
      return true;
    }
  }
  return set_error("Expected DOC, DOCUMENT, or LANGUAGE after DESCRIBE");
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
