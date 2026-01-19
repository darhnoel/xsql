#include "html_parser.h"

#include <algorithm>

#include "html/parser_impl.h"

namespace xsql {

namespace {

std::vector<std::vector<int64_t>> build_children(const HtmlDocument& doc) {
  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (node.parent_id.has_value()) {
      children.at(static_cast<size_t>(*node.parent_id)).push_back(node.id);
    }
  }
  return children;
}

void assign_doc_order(HtmlDocument& doc,
                      const std::vector<std::vector<int64_t>>& children) {
  std::vector<int64_t> roots;
  roots.reserve(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (!node.parent_id.has_value()) {
      roots.push_back(node.id);
    }
  }
  int64_t order = 0;
  std::vector<int64_t> stack;
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
    stack.push_back(*it);
  }
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    doc.nodes.at(static_cast<size_t>(id)).doc_order = order++;
    const auto& kids = children.at(static_cast<size_t>(id));
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
      stack.push_back(*it);
    }
  }
}

void assign_max_depth(HtmlDocument& doc,
                      const std::vector<std::vector<int64_t>>& children) {
  std::vector<int64_t> roots;
  roots.reserve(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (!node.parent_id.has_value()) {
      roots.push_back(node.id);
    }
  }
  std::vector<std::pair<int64_t, bool>> stack;
  for (int64_t root : roots) {
    stack.push_back({root, false});
  }
  while (!stack.empty()) {
    auto [id, visited] = stack.back();
    stack.pop_back();
    if (!visited) {
      stack.push_back({id, true});
      for (int64_t child : children.at(static_cast<size_t>(id))) {
        stack.push_back({child, false});
      }
      continue;
    }
    int64_t max_child = -1;
    for (int64_t child : children.at(static_cast<size_t>(id))) {
      max_child = std::max(max_child,
                           doc.nodes.at(static_cast<size_t>(child)).max_depth);
    }
    doc.nodes.at(static_cast<size_t>(id)).max_depth = (max_child < 0) ? 0 : max_child + 1;
  }
}

void compute_document_metadata(HtmlDocument& doc) {
  if (doc.nodes.empty()) return;
  auto children = build_children(doc);
  assign_doc_order(doc, children);
  assign_max_depth(doc, children);
}

}  // namespace

/// Dispatches HTML parsing to the selected backend.
/// MUST choose libxml2 when enabled and MUST fall back deterministically otherwise.
/// Inputs are HTML strings; outputs are HtmlDocument with no side effects.
HtmlDocument parse_html(const std::string& html) {
#ifdef XSQL_USE_LIBXML2
  HtmlDocument doc = parse_html_libxml2(html);
#else
  // WHY: fallback parser keeps offline builds working without libxml2.
  HtmlDocument doc = parse_html_naive(html);
#endif
  compute_document_metadata(doc);
  return doc;
}

}  // namespace xsql
