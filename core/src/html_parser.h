#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xsql {

struct HtmlNode {
  int64_t id = 0;
  std::string tag;
  std::string text;
  std::string inner_html;
  std::unordered_map<std::string, std::string> attributes;
  std::optional<int64_t> parent_id;
  int64_t max_depth = 0;
  int64_t doc_order = 0;
};

struct HtmlDocument {
  std::vector<HtmlNode> nodes;
};

HtmlDocument parse_html(const std::string& html);

}  // namespace xsql
