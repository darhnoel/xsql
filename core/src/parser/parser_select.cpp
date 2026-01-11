#include "parser_internal.h"

namespace xsql {

/// Parses the SELECT projection list, enforcing projection rules.
/// MUST reject mixing tag-only and projected fields.
/// Inputs are token stream; outputs are select items or errors.
bool Parser::parse_select_list(std::vector<Query::SelectItem>& items) {
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
bool Parser::parse_exclude_list(std::vector<std::string>& fields) {
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
bool Parser::parse_select_item(std::vector<Query::SelectItem>& items, bool& saw_field, bool& saw_tag_only) {
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

}  // namespace xsql
