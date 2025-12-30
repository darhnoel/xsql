#include "executor.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace xsql {

namespace {

std::string to_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

int compare_nullable_int(std::optional<int64_t> left, std::optional<int64_t> right) {
  if (!left.has_value() && !right.has_value()) return 0;
  if (!left.has_value()) return 1;
  if (!right.has_value()) return -1;
  if (*left < *right) return -1;
  if (*left > *right) return 1;
  return 0;
}

int compare_string(const std::string& left, const std::string& right) {
  if (left < right) return -1;
  if (left > right) return 1;
  return 0;
}

int compare_nodes(const HtmlNode& left, const HtmlNode& right, const std::string& field) {
  if (field == "node_id") {
    if (left.id < right.id) return -1;
    if (left.id > right.id) return 1;
    return 0;
  }
  if (field == "tag") return compare_string(left.tag, right.tag);
  if (field == "text") return compare_string(left.text, right.text);
  if (field == "parent_id") return compare_nullable_int(left.parent_id, right.parent_id);
  return 0;
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    size_t start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    if (start < i) out.push_back(s.substr(start, i - start));
  }
  return out;
}

std::optional<int64_t> parse_int64(const std::string& value) {
  try {
    size_t idx = 0;
    int64_t out = std::stoll(value, &idx);
    if (idx != value.size()) return std::nullopt;
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

bool string_in_list(const std::string& value, const std::vector<std::string>& list) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

bool match_attribute(const HtmlNode& node, const std::string& attr, const std::vector<std::string>& values, bool is_in) {
  auto it = node.attributes.find(attr);
  if (it == node.attributes.end()) return false;

  std::string attr_value = it->second;
  if (attr == "class") {
    auto tokens = split_ws(attr_value);
    for (const auto& token : tokens) {
      if (string_in_list(token, values)) return true;
    }
    return false;
  }

  if (is_in) {
    return string_in_list(attr_value, values);
  }
  return attr_value == values.front();
}

bool match_field(const HtmlNode& node,
                 Operand::FieldKind field_kind,
                 const std::string& attr,
                 const std::vector<std::string>& values,
                 CompareExpr::Op op) {
  bool is_in = op == CompareExpr::Op::In;
  if (field_kind == Operand::FieldKind::NodeId) {
    if (op == CompareExpr::Op::Regex) return false;
    auto target = parse_int64(values.front());
    if (!target.has_value()) return false;
    if (is_in) {
      for (const auto& value : values) {
        auto parsed = parse_int64(value);
        if (parsed.has_value() && *parsed == node.id) return true;
      }
      return false;
    }
    if (op == CompareExpr::Op::NotEq) return node.id != *target;
    return node.id == *target;
  }
  if (field_kind == Operand::FieldKind::ParentId) {
    if (!node.parent_id.has_value()) return false;
    if (op == CompareExpr::Op::Regex) return false;
    auto target = parse_int64(values.front());
    if (!target.has_value()) return false;
    if (is_in) {
      for (const auto& value : values) {
        auto parsed = parse_int64(value);
        if (parsed.has_value() && *parsed == *node.parent_id) return true;
      }
      return false;
    }
    if (op == CompareExpr::Op::NotEq) return *node.parent_id != *target;
    return *node.parent_id == *target;
  }
  if (field_kind == Operand::FieldKind::Attribute) {
    if (op == CompareExpr::Op::Regex) {
      auto it = node.attributes.find(attr);
      if (it == node.attributes.end()) return false;
      try {
        std::regex re(values.front(), std::regex::ECMAScript);
        return std::regex_search(it->second, re);
      } catch (const std::regex_error&) {
        return false;
      }
    }
    if (op == CompareExpr::Op::NotEq) {
      auto it = node.attributes.find(attr);
      if (it == node.attributes.end()) return false;
      if (attr == "class") {
        auto tokens = split_ws(it->second);
        for (const auto& token : tokens) {
          if (token == values.front()) return false;
        }
        return true;
      }
      return it->second != values.front();
    }
    return match_attribute(node, attr, values, is_in);
  }
  if (field_kind == Operand::FieldKind::Tag) {
    if (op == CompareExpr::Op::Regex) {
      try {
        std::regex re(values.front(), std::regex::ECMAScript);
        return std::regex_search(node.tag, re);
      } catch (const std::regex_error&) {
        return false;
      }
    }
    if (is_in) {
      for (const auto& v : values) {
        if (node.tag == to_lower(v)) return true;
      }
      return false;
    }
    if (op == CompareExpr::Op::NotEq) return node.tag != to_lower(values.front());
    return node.tag == to_lower(values.front());
  }
  if (op == CompareExpr::Op::Regex) {
    try {
      std::regex re(values.front(), std::regex::ECMAScript);
      return std::regex_search(node.text, re);
    } catch (const std::regex_error&) {
      return false;
    }
  }
  if (is_in) return string_in_list(node.text, values);
  if (op == CompareExpr::Op::NotEq) return node.text != values.front();
  return node.text == values.front();
}

bool has_child_node_id(const HtmlDocument& doc,
                       const std::vector<std::vector<int64_t>>& children,
                       const HtmlNode& node,
                       const std::vector<std::string>& values,
                       CompareExpr::Op op) {
  for (int64_t id : children.at(static_cast<size_t>(node.id))) {
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_field(child, Operand::FieldKind::NodeId, "", values, op)) return true;
  }
  return false;
}

bool has_descendant_node_id(const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children,
                            const HtmlNode& node,
                            const std::vector<std::string>& values,
                            CompareExpr::Op op) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_field(child, Operand::FieldKind::NodeId, "", values, op)) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

bool axis_has_parent_id(const HtmlDocument& doc,
                        const std::vector<std::vector<int64_t>>& children,
                        const HtmlNode& node,
                        Operand::Axis axis) {
  if (axis == Operand::Axis::Self) {
    return node.parent_id.has_value();
  }
  if (axis == Operand::Axis::Parent) {
    if (!node.parent_id.has_value()) return false;
    const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*node.parent_id));
    return parent.parent_id.has_value();
  }
  if (axis == Operand::Axis::Child) {
    for (int64_t id : children.at(static_cast<size_t>(node.id))) {
      const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
      if (child.parent_id.has_value()) return true;
    }
    return false;
  }
  if (axis == Operand::Axis::Ancestor) {
    const HtmlNode* current = &node;
    while (current->parent_id.has_value()) {
      const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
      if (parent.parent_id.has_value()) return true;
      current = &parent;
    }
    return false;
  }
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (child.parent_id.has_value()) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

bool has_descendant_field(const HtmlDocument& doc,
                          const std::vector<std::vector<int64_t>>& children,
                          const HtmlNode& node,
                          Operand::FieldKind field_kind,
                          const std::string& attr,
                          const std::vector<std::string>& values,
                          CompareExpr::Op op) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_field(child, field_kind, attr, values, op)) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

bool has_descendant_attribute_exists(const HtmlDocument& doc,
                                     const std::vector<std::vector<int64_t>>& children,
                                     const HtmlNode& node,
                                     const std::string& attr) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (child.attributes.find(attr) != child.attributes.end()) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

bool has_child_field(const HtmlDocument& doc,
                     const std::vector<std::vector<int64_t>>& children,
                     const HtmlNode& node,
                     Operand::FieldKind field_kind,
                     const std::string& attr,
                     const std::vector<std::string>& values,
                     CompareExpr::Op op) {
  for (int64_t id : children.at(static_cast<size_t>(node.id))) {
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_field(child, field_kind, attr, values, op)) return true;
  }
  return false;
}

bool has_child_attribute_exists(const HtmlDocument& doc,
                                const std::vector<std::vector<int64_t>>& children,
                                const HtmlNode& node,
                                const std::string& attr) {
  for (int64_t id : children.at(static_cast<size_t>(node.id))) {
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (child.attributes.find(attr) != child.attributes.end()) return true;
  }
  return false;
}

bool has_child_any(const std::vector<std::vector<int64_t>>& children, const HtmlNode& node) {
  return !children.at(static_cast<size_t>(node.id)).empty();
}

bool has_descendant_any(const std::vector<std::vector<int64_t>>& children, const HtmlNode& node) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    return true;
  }
  return false;
}

bool axis_has_attribute(const HtmlDocument& doc,
                        const std::vector<std::vector<int64_t>>& children,
                        const HtmlNode& node,
                        Operand::Axis axis,
                        const std::string& attr) {
  if (axis == Operand::Axis::Self) {
    return node.attributes.find(attr) != node.attributes.end();
  }
  if (axis == Operand::Axis::Parent) {
    if (!node.parent_id.has_value()) return false;
    const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*node.parent_id));
    return parent.attributes.find(attr) != parent.attributes.end();
  }
  if (axis == Operand::Axis::Child) {
    return has_child_attribute_exists(doc, children, node, attr);
  }
  if (axis == Operand::Axis::Ancestor) {
    const HtmlNode* current = &node;
    while (current->parent_id.has_value()) {
      const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
      if (parent.attributes.find(attr) != parent.attributes.end()) return true;
      current = &parent;
    }
    return false;
  }
  return has_descendant_attribute_exists(doc, children, node, attr);
}

bool axis_has_any_node(const HtmlDocument& doc,
                       const std::vector<std::vector<int64_t>>& children,
                       const HtmlNode& node,
                       Operand::Axis axis) {
  if (axis == Operand::Axis::Self) return true;
  if (axis == Operand::Axis::Parent) return node.parent_id.has_value();
  if (axis == Operand::Axis::Child) return has_child_any(children, node);
  if (axis == Operand::Axis::Ancestor) {
    return node.parent_id.has_value();
  }
  return has_descendant_any(children, node);
}

bool eval_expr(const Expr& expr,
               const HtmlDocument& doc,
               const std::vector<std::vector<int64_t>>& children,
               const HtmlNode& node) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    std::vector<std::string> values = cmp.rhs.values;
    bool is_in = cmp.op == CompareExpr::Op::In;
  if (cmp.op == CompareExpr::Op::IsNull || cmp.op == CompareExpr::Op::IsNotNull) {
    bool exists = false;
    if (cmp.lhs.field_kind == Operand::FieldKind::AttributesMap) {
      exists = !node.attributes.empty();
    } else if (cmp.lhs.field_kind == Operand::FieldKind::Attribute) {
      exists = axis_has_attribute(doc, children, node, cmp.lhs.axis, cmp.lhs.attribute);
    } else if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
      exists = axis_has_any_node(doc, children, node, cmp.lhs.axis);
    } else if (cmp.lhs.field_kind == Operand::FieldKind::ParentId) {
      exists = axis_has_parent_id(doc, children, node, cmp.lhs.axis);
    } else {
      exists = axis_has_any_node(doc, children, node, cmp.lhs.axis);
    }
      return (cmp.op == CompareExpr::Op::IsNull) ? !exists : exists;
    }
    if (cmp.lhs.axis == Operand::Axis::Parent) {
      if (!node.parent_id.has_value()) return false;
      const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*node.parent_id));
      if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
        return match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
      }
      return match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
    }
    if (cmp.lhs.axis == Operand::Axis::Child) {
      if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
        return has_child_node_id(doc, children, node, values, cmp.op);
      }
      return has_child_field(doc, children, node, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
    }
    if (cmp.lhs.axis == Operand::Axis::Ancestor) {
      const HtmlNode* current = &node;
      while (current->parent_id.has_value()) {
        const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
        if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
          if (match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op)) return true;
        } else {
          if (match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op)) return true;
        }
        current = &parent;
      }
      return false;
    }
    if (cmp.lhs.axis == Operand::Axis::Descendant) {
      if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
        return has_descendant_node_id(doc, children, node, values, cmp.op);
      }
      return has_descendant_field(doc, children, node, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
    }
    return match_field(node, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
  }

  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  bool left = eval_expr(bin.left, doc, children, node);
  bool right = eval_expr(bin.right, doc, children, node);
  if (bin.op == BinaryExpr::Op::And) return left && right;
  return left || right;
}

}  // namespace

ExecuteResult execute_query(const Query& query, const HtmlDocument& doc, const std::string& source_uri) {
  ExecuteResult result;
  std::vector<std::string> select_tags;
  select_tags.reserve(query.select_items.size());
  bool select_all = false;
  for (const auto& item : query.select_items) {
    if (item.tag == "*") {
      select_all = true;
      break;
    }
    select_tags.push_back(to_lower(item.tag));
  }

  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (node.parent_id.has_value()) {
      children.at(static_cast<size_t>(*node.parent_id)).push_back(node.id);
    }
  }

  for (const auto& node : doc.nodes) {
    if (!select_all && !string_in_list(node.tag, select_tags)) continue;
    if (query.where.has_value()) {
      if (!eval_expr(*query.where, doc, children, node)) continue;
    }
    HtmlNode out = node;
    out.tag = node.tag;
    result.nodes.push_back(out);
  }

  if (!query.order_by.empty()) {
    std::stable_sort(result.nodes.begin(), result.nodes.end(),
                     [&](const HtmlNode& left, const HtmlNode& right) {
                       for (const auto& order_by : query.order_by) {
                         int cmp = compare_nodes(left, right, order_by.field);
                         if (cmp == 0) continue;
                         if (order_by.descending) {
                           return cmp > 0;
                         }
                         return cmp < 0;
                       }
                       return false;
                     });
  }

  if (query.limit.has_value() && result.nodes.size() > *query.limit) {
    result.nodes.resize(*query.limit);
  }

  return result;
}

}  // namespace xsql
