#include "parser_internal.h"

namespace xsql {

/// Parses an expression with OR precedence.
/// MUST build BinaryExpr nodes in left-associative order.
/// Inputs are tokens; outputs are Expr or errors.
bool Parser::parse_expr(Expr& out) {
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
bool Parser::parse_and_expr(Expr& out) {
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
bool Parser::parse_cmp_expr(Expr& out) {
  if (current_.type == TokenType::LParen) {
    advance();
    Expr inner;
    if (!parse_expr(inner)) return false;
    if (!consume(TokenType::RParen, "Expected ) to close expression")) return false;
    out = inner;
    return true;
  }
  if (current_.type == TokenType::KeywordExists) {
    Token exists_token = current_;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after EXISTS")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected axis name after EXISTS(");
    Operand::Axis axis = Operand::Axis::Self;
    std::string axis_name = to_upper(current_.text);
    if (axis_name == "SELF") {
      axis = Operand::Axis::Self;
    } else if (axis_name == "PARENT") {
      axis = Operand::Axis::Parent;
    } else if (axis_name == "CHILD") {
      axis = Operand::Axis::Child;
    } else if (axis_name == "ANCESTOR") {
      axis = Operand::Axis::Ancestor;
    } else if (axis_name == "DESCENDANT") {
      axis = Operand::Axis::Descendant;
    } else {
      return set_error("Expected axis name (self, parent, child, ancestor, descendant)");
    }
    advance();
    std::optional<Expr> filter;
    if (current_.type == TokenType::KeywordWhere) {
      advance();
      Expr inner;
      if (!parse_expr(inner)) return false;
      filter = inner;
    }
    if (!consume(TokenType::RParen, "Expected ) after EXISTS(...)")) return false;
    auto node = std::make_shared<ExistsExpr>();
    node->axis = axis;
    node->where = std::move(filter);
    node->span = Span{exists_token.pos, current_.pos};
    out = node;
    return true;
  }
  if (current_.type == TokenType::Identifier && peek().type == TokenType::KeywordHasDirectText) {
    Token tag_token = current_;
    Operand operand;
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::Tag;
    operand.attribute = to_lower(tag_token.text);
    operand.span = Span{tag_token.pos, tag_token.pos + tag_token.text.size()};
    advance();
    CompareExpr cmp;
    cmp.lhs = operand;
    cmp.op = CompareExpr::Op::HasDirectText;
    advance();
    if (current_.type != TokenType::String) {
      return set_error("Expected string literal after HAS_DIRECT_TEXT");
    }
    ValueList values;
    values.values.push_back(current_.text);
    values.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    cmp.rhs = values;
    out = cmp;
    return true;
  }
  Operand operand;
  if (!parse_operand(operand)) return false;

  CompareExpr cmp;
  cmp.lhs = operand;
  if (current_.type == TokenType::KeywordContains) {
    cmp.op = CompareExpr::Op::Contains;
    advance();
    if (current_.type == TokenType::KeywordAll) {
      cmp.op = CompareExpr::Op::ContainsAll;
      advance();
    } else if (current_.type == TokenType::KeywordAny) {
      cmp.op = CompareExpr::Op::ContainsAny;
      advance();
    }
    ValueList values;
    if (!parse_string_list(values)) return false;
    if (cmp.op == CompareExpr::Op::Contains && values.values.size() != 1) {
      return set_error("CONTAINS with multiple values requires ALL or ANY");
    }
    cmp.rhs = values;
    out = cmp;
    return true;
  }
  if (current_.type == TokenType::KeywordHasDirectText) {
    cmp.op = CompareExpr::Op::HasDirectText;
    advance();
    if (cmp.lhs.field_kind != Operand::FieldKind::Tag || cmp.lhs.attribute.empty()) {
      return set_error("HAS_DIRECT_TEXT expects a tag identifier");
    }
    if (current_.type != TokenType::String) {
      return set_error("Expected string literal after HAS_DIRECT_TEXT");
    }
    ValueList values;
    values.values.push_back(current_.text);
    values.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    cmp.rhs = values;
    out = cmp;
    return true;
  }
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
  return set_error("Expected =, <>, ~, IN, CONTAINS, HAS_DIRECT_TEXT, or IS");
}

/// Parses an operand with axes, fields, and qualifiers.
/// MUST set axis/field_kind consistently with grammar.
/// Inputs are tokens; outputs are Operand or errors.
bool Parser::parse_operand(Operand& operand) {
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
  if (to_upper(current_.text) == "SIBLING_POS") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::SiblingPos;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "MAX_DEPTH") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::MaxDepth;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "DOC_ORDER") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Parent;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Child;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Ancestor;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Descendant;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (to_upper(qualifier) == "SIBLING_POS") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::SiblingPos;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    if (to_upper(qualifier) == "MAX_DEPTH") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::MaxDepth;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    if (to_upper(qualifier) == "DOC_ORDER") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::DocOrder;
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
  if (to_upper(current_.text) == "SIBLING_POS") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::SiblingPos;
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
  if (to_upper(current_.text) == "MAX_DEPTH") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::MaxDepth;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (to_upper(current_.text) == "DOC_ORDER") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Parent;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Child;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Ancestor;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
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
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Descendant;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
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
bool Parser::parse_value_list(ValueList& values) {
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

/// Parses a string literal or list of string literals.
/// MUST reject non-string values and enforce list delimiters.
/// Inputs are tokens; outputs are ValueList or errors.
bool Parser::parse_string_list(ValueList& values) {
  if (current_.type == TokenType::String) {
    values.values.push_back(current_.text);
    values.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type == TokenType::LParen) {
    size_t start = current_.pos;
    advance();
    if (current_.type != TokenType::String) {
      return set_error("Expected string literal");
    }
    values.values.push_back(current_.text);
    advance();
    while (current_.type == TokenType::Comma) {
      advance();
      if (current_.type != TokenType::String) {
        return set_error("Expected string literal");
      }
      values.values.push_back(current_.text);
      advance();
    }
    if (!consume(TokenType::RParen, "Expected )")) return false;
    values.span = Span{start, current_.pos};
    return true;
  }
  return set_error("Expected string literal or list");
}

}  // namespace xsql
