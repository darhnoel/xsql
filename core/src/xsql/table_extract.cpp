#include "xsql_internal.h"

#include <cctype>

#include "../util/string_util.h"

namespace xsql::xsql_internal {

namespace {

/// Checks whether a character is valid in tag names.
/// MUST align with parser assumptions for tag scanning.
/// Inputs are characters; outputs are booleans with no side effects.
bool is_name_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':';
}

/// Determines whether a tag is void and should not increase nesting depth.
/// MUST match HTML void tag semantics to avoid unbalanced output.
/// Inputs are tag names; outputs are booleans with no side effects.
bool is_void_tag(const std::string& tag) {
  static const std::vector<std::string> kVoidTags = {
      "area", "base", "br",   "col",  "embed", "hr",    "img",   "input",
      "link", "meta", "param","source","track","wbr"};
  for (const auto& name : kVoidTags) {
    if (tag == name) return true;
  }
  return false;
}

/// Determines whether a tag should be treated as inline for text extraction.
/// MUST remain a conservative list to avoid pulling large descendant text.
/// Inputs are tag names; outputs are booleans with no side effects.
bool is_inline_tag(const std::string& tag) {
  static const std::vector<std::string> kInlineTags = {
      "a", "abbr", "b", "bdi", "bdo", "br", "cite", "code", "data", "dfn",
      "em", "i", "kbd", "mark", "q", "rp", "rt", "ruby", "s", "samp",
      "small", "span", "strong", "sub", "sup", "time", "u", "var", "wbr"};
  for (const auto& name : kInlineTags) {
    if (tag == name) return true;
  }
  return false;
}

/// Finds the end of a tag while respecting quoted attributes.
/// MUST ignore '>' characters inside quotes to prevent premature termination.
/// Inputs are HTML strings and start indices; outputs are end positions or npos.
size_t find_tag_end(const std::string& html, size_t start) {
  bool in_quote = false;
  char quote = '\0';
  for (size_t i = start; i < html.size(); ++i) {
    char c = html[i];
    if (in_quote) {
      if (c == quote) {
        in_quote = false;
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      in_quote = true;
      quote = c;
      continue;
    }
    if (c == '>') return i;
  }
  return std::string::npos;
}

}  // namespace

/// Truncates inner_html to a maximum nesting depth.
/// MUST preserve valid tag structure within the depth limit.
/// Inputs are HTML strings and depth; outputs are truncated HTML strings.
std::string limit_inner_html(const std::string& html, size_t max_depth) {
  std::string out;
  out.reserve(html.size());
  size_t i = 0;
  int depth = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      if (html.compare(i, 4, "<!--") == 0) {
        size_t end = html.find("-->", i + 4);
        size_t stop = (end == std::string::npos) ? html.size() : end + 3;
        if (depth <= static_cast<int>(max_depth)) {
          out.append(html, i, stop - i);
        }
        i = stop;
        continue;
      }
      bool is_end = (i + 1 < html.size() && html[i + 1] == '/');
      size_t tag_end = find_tag_end(html, i + 1);
      if (tag_end == std::string::npos) {
        out.append(html, i, html.size() - i);
        break;
      }

      size_t name_start = i + (is_end ? 2 : 1);
      while (name_start < tag_end && std::isspace(static_cast<unsigned char>(html[name_start]))) {
        ++name_start;
      }
      size_t name_end = name_start;
      while (name_end < tag_end && is_name_char(html[name_end])) {
        ++name_end;
      }
      std::string tag = util::to_lower(html.substr(name_start, name_end - name_start));

      bool self_closing = false;
      if (!is_end) {
        size_t j = tag_end;
        while (j > i && std::isspace(static_cast<unsigned char>(html[j - 1]))) {
          --j;
        }
        if (j > i && html[j - 1] == '/') {
          self_closing = true;
        }
        // WHY: void tags should not increase depth to keep output well-formed.
        if (is_void_tag(tag)) {
          self_closing = true;
        }
      }

      if (!is_end) {
        int element_depth = depth + 1;
        if (element_depth <= static_cast<int>(max_depth)) {
          out.append(html, i, tag_end - i + 1);
        }
        if (!self_closing) {
          depth++;
        }
      } else {
        if (depth <= static_cast<int>(max_depth)) {
          out.append(html, i, tag_end - i + 1);
        }
        if (depth > 0) {
          depth--;
        }
      }
      i = tag_end + 1;
      continue;
    }
    out.push_back(html[i++]);
  }
  return out;
}

/// Extracts only direct text nodes from inner_html (depth 0).
/// MUST exclude text inside nested tags and MUST preserve order.
/// Inputs are HTML strings; outputs are text-only strings.
std::string extract_direct_text(const std::string& html) {
  std::string out;
  out.reserve(html.size());
  size_t i = 0;
  int depth = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      if (html.compare(i, 4, "<!--") == 0) {
        size_t end = html.find("-->", i + 4);
        i = (end == std::string::npos) ? html.size() : end + 3;
        continue;
      }
      bool is_end = (i + 1 < html.size() && html[i + 1] == '/');
      size_t tag_end = find_tag_end(html, i + 1);
      if (tag_end == std::string::npos) {
        break;
      }
      size_t name_start = i + (is_end ? 2 : 1);
      while (name_start < tag_end && std::isspace(static_cast<unsigned char>(html[name_start]))) {
        ++name_start;
      }
      size_t name_end = name_start;
      while (name_end < tag_end && is_name_char(html[name_end])) {
        ++name_end;
      }
      std::string tag = util::to_lower(html.substr(name_start, name_end - name_start));
      bool self_closing = false;
      if (!is_end) {
        size_t j = tag_end;
        while (j > i && std::isspace(static_cast<unsigned char>(html[j - 1]))) {
          --j;
        }
        if (j > i && html[j - 1] == '/') {
          self_closing = true;
        }
        if (is_void_tag(tag)) {
          self_closing = true;
        }
      }
      bool inline_tag = is_inline_tag(tag);
      if (!is_end && !self_closing && !inline_tag) {
        depth++;
      } else if (is_end && depth > 0 && !inline_tag) {
        depth--;
      }
      i = tag_end + 1;
      continue;
    }
    if (depth == 0) {
      out.push_back(html[i]);
    }
    ++i;
  }
  return out;
}

/// Builds adjacency lists for child traversal in table extraction.
/// MUST preserve original document order for deterministic output.
/// Inputs are HtmlDocument; outputs are children vectors.
std::vector<std::vector<int64_t>> build_children(const HtmlDocument& doc) {
  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (node.parent_id.has_value()) {
      children.at(static_cast<size_t>(*node.parent_id)).push_back(node.id);
    }
  }
  return children;
}

/// Collects table rows and cell text for TO TABLE rendering/export.
/// MUST preserve row order and MUST skip empty rows.
/// Inputs are doc/children/table_id; outputs are row vectors.
void collect_rows(const HtmlDocument& doc,
                  const std::vector<std::vector<int64_t>>& children,
                  int64_t table_id,
                  std::vector<std::vector<std::string>>& out_rows) {
  std::vector<int64_t> stack;
  stack.push_back(table_id);
  std::vector<int64_t> tr_nodes;
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& node = doc.nodes.at(static_cast<size_t>(id));
    if (node.tag == "tr") {
      tr_nodes.push_back(id);
      continue;
    }
    const auto& kids = children.at(static_cast<size_t>(id));
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
      stack.push_back(*it);
    }
  }

  for (int64_t tr_id : tr_nodes) {
    std::vector<std::string> row;
    std::vector<int64_t> cell_stack;
    const auto& kids = children.at(static_cast<size_t>(tr_id));
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
      cell_stack.push_back(*it);
    }
    while (!cell_stack.empty()) {
      int64_t id = cell_stack.back();
      cell_stack.pop_back();
      const HtmlNode& node = doc.nodes.at(static_cast<size_t>(id));
      if (node.tag == "td" || node.tag == "th") {
        row.push_back(util::trim_ws(node.text));
        continue;
      }
      const auto& next = children.at(static_cast<size_t>(id));
      for (auto it = next.rbegin(); it != next.rend(); ++it) {
        cell_stack.push_back(*it);
      }
    }
    if (!row.empty()) {
      out_rows.push_back(row);
    }
  }
}

}  // namespace xsql::xsql_internal
