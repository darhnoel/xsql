#include "parser_impl.h"

#include <cctype>

#include "../util/string_util.h"

namespace xsql {

namespace {

/// Checks whether a character is valid in tag or attribute names.
/// MUST align with naive parser expectations to avoid malformed tokens.
/// Inputs are characters; outputs are booleans with no side effects.
bool is_name_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':';
}

/// Advances an index past ASCII whitespace in the input string.
/// MUST not skip non-whitespace content to preserve parsing boundaries.
/// Inputs are string/index; outputs are updated index with no side effects.
void skip_ws(const std::string& s, size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
}

}  // namespace

/// Parses HTML using a simple tag scanner for offline environments.
/// MUST avoid executing scripts and MUST preserve document order.
/// Inputs are HTML strings; outputs are HtmlDocument with no side effects.
HtmlDocument parse_html_naive(const std::string& html) {
  HtmlDocument doc;
  struct OpenNode {
    int64_t id = 0;
    size_t content_start = 0;
  };
  std::string lower_html = util::to_lower(html);
  std::vector<OpenNode> stack;
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      // WHY: comments are skipped to prevent bogus tag extraction.
      if (html.compare(i, 4, "<!--") == 0) {
        size_t end = html.find("-->", i + 4);
        i = (end == std::string::npos) ? html.size() : end + 3;
        continue;
      }
      if (i + 1 < html.size() && html[i + 1] == '/') {
        size_t close_start = i;
        i += 2;
        while (i < html.size() && is_name_char(html[i])) {
          ++i;
        }
        size_t close = html.find('>', i);
        i = (close == std::string::npos) ? html.size() : close + 1;
        if (!stack.empty()) {
          OpenNode open = stack.back();
          if (close_start >= open.content_start) {
            doc.nodes[static_cast<size_t>(open.id)].inner_html =
                html.substr(open.content_start, close_start - open.content_start);
          }
          stack.pop_back();
        }
        continue;
      }

      ++i;
      skip_ws(html, i);
      std::string tag;
      while (i < html.size() && is_name_char(html[i])) {
        tag.push_back(html[i++]);
      }
      if (tag.empty()) {
        ++i;
        continue;
      }
      HtmlNode node;
      node.id = static_cast<int64_t>(doc.nodes.size());
      node.tag = util::to_lower(tag);
      if (!stack.empty()) {
        node.parent_id = stack.back().id;
      }

      bool self_close = false;
      while (i < html.size()) {
        skip_ws(html, i);
        if (i >= html.size()) break;
        if (html[i] == '/') {
          self_close = true;
          ++i;
          skip_ws(html, i);
          if (i < html.size() && html[i] == '>') {
            ++i;
          }
          break;
        }
        if (html[i] == '>') {
          ++i;
          break;
        }

        std::string attr_name;
        while (i < html.size() && is_name_char(html[i])) {
          attr_name.push_back(html[i++]);
        }
        if (attr_name.empty()) {
          ++i;
          continue;
        }
        attr_name = util::to_lower(attr_name);
        skip_ws(html, i);
        std::string attr_value;
        if (i < html.size() && html[i] == '=') {
          ++i;
          skip_ws(html, i);
          if (i < html.size() && (html[i] == '\'' || html[i] == '"')) {
            char quote = html[i++];
            while (i < html.size() && html[i] != quote) {
              attr_value.push_back(html[i++]);
            }
            if (i < html.size() && html[i] == quote) ++i;
          } else {
            while (i < html.size() && !std::isspace(static_cast<unsigned char>(html[i])) &&
                   html[i] != '>' && html[i] != '/') {
              attr_value.push_back(html[i++]);
            }
          }
        }
        if (!attr_name.empty()) {
          node.attributes[attr_name] = attr_value;
        }
      }

      doc.nodes.push_back(node);
      HtmlNode& current = doc.nodes.back();
      size_t content_start = i;
      if (!self_close && (node.tag == "script" || node.tag == "style")) {
        std::string close_tag = "</" + node.tag;
        size_t close_start = lower_html.find(close_tag, content_start);
        if (close_start == std::string::npos) {
          current.inner_html = html.substr(content_start);
          current.text += current.inner_html;
          i = html.size();
          continue;
        }
        current.inner_html = html.substr(content_start, close_start - content_start);
        current.text += current.inner_html;
        size_t close_end = html.find('>', close_start);
        i = (close_end == std::string::npos) ? html.size() : close_end + 1;
        continue;
      }
      if (!self_close) {
        stack.push_back(OpenNode{node.id, content_start});
      }
      continue;
    }

    size_t start = i;
    while (i < html.size() && html[i] != '<') {
      ++i;
    }
    if (!stack.empty()) {
      std::string text = html.substr(start, i - start);
      for (const auto& open : stack) {
        doc.nodes[static_cast<size_t>(open.id)].text += text;
      }
    }
  }

  return doc;
}

}  // namespace xsql
