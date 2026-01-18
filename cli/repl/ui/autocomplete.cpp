#include "autocomplete.h"

#include <algorithm>
#include <cctype>

namespace xsql::cli {

AutoCompleter::AutoCompleter() {
  keywords_ = {
      "select", "from", "where", "and", "or", "in", "limit", "to", "list", "table",
      "csv", "parquet", "count", "summarize", "order", "by", "asc", "desc", "document", "doc",
      "raw", "fragments", "contains", "all", "any", "has_direct_text",
      "attributes", "tag", "text", "parent", "child", "ancestor", "descendant",
      "parent_id", "sibling_pos", "inner_html", "trim", "is", "null", "header", "noheader",
      "no_header", "on", "off", "export", "tfidf", "top_terms", "min_df", "max_df",
      "stopwords", "english", "none", "default", "show", "describe", "input", "inputs",
      "functions", "axes", "operators", "language"
  };
  commands_ = {
      ".help", ".load", ".mode", ".display_mode", ".max_rows", ".reload_config",
      ".summarize", ".summarize_content",
      ".plugin", ".quit", ".q",
      ":help", ":load", ":quit", ":exit"
  };
#ifdef XSQL_ENABLE_KHMER_NUMBER
  commands_.push_back(".number_to_khmer");
  commands_.push_back(".khmer_to_number");
#endif
}

bool AutoCompleter::complete(std::string& buffer,
                             size_t& cursor,
                             std::vector<std::string>& suggestions) const {
  if (cursor > buffer.size()) cursor = buffer.size();
  size_t start = cursor;
  while (start > 0 && is_word_char(buffer[start - 1])) {
    --start;
  }
  if (start == cursor) return false;
  std::string word = buffer.substr(start, cursor - start);
  if (word.empty()) return false;
  bool is_cmd = word[0] == '.' || word[0] == ':';
  const auto& list = is_cmd ? commands_ : keywords_;
  std::string prefix = to_lower(word);
  std::vector<std::string> matches;
  for (const auto& cand : list) {
    if (starts_with(to_lower(cand), prefix)) {
      matches.push_back(cand);
    }
  }
  if (matches.empty()) return false;
  std::string common = longest_common_prefix_lower(matches);
  std::string cased_common = is_cmd ? common : apply_case(word, common);
  if (cased_common.size() > word.size() || matches.size() == 1) {
    buffer.replace(start, cursor - start, cased_common);
    cursor = start + cased_common.size();
    return true;
  }
  suggestions.reserve(matches.size());
  for (const auto& cand : matches) {
    suggestions.push_back(is_cmd ? cand : apply_case(word, cand));
  }
  return false;
}

bool AutoCompleter::is_word_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == ':';
}

std::string AutoCompleter::to_lower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

bool AutoCompleter::starts_with(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string AutoCompleter::longest_common_prefix_lower(const std::vector<std::string>& values) {
  if (values.empty()) return "";
  std::string prefix = to_lower(values[0]);
  for (size_t i = 1; i < values.size(); ++i) {
    size_t j = 0;
    std::string lower = to_lower(values[i]);
    while (j < prefix.size() && j < lower.size() && prefix[j] == lower[j]) {
      ++j;
    }
    prefix.resize(j);
    if (prefix.empty()) break;
  }
  return prefix;
}

std::string AutoCompleter::apply_case(const std::string& pattern, const std::string& value) {
  if (pattern.empty()) return value;
  bool all_upper = true;
  bool all_lower = true;
  for (char c : pattern) {
    if (std::isalpha(static_cast<unsigned char>(c))) {
      if (std::islower(static_cast<unsigned char>(c))) {
        all_upper = false;
      } else if (std::isupper(static_cast<unsigned char>(c))) {
        all_lower = false;
      }
    }
  }
  if (all_upper) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
  }
  if (all_lower) {
    return to_lower(value);
  }
  bool capitalized = std::isupper(static_cast<unsigned char>(pattern[0]));
  for (size_t i = 1; i < pattern.size(); ++i) {
    if (std::isalpha(static_cast<unsigned char>(pattern[i])) &&
        std::isupper(static_cast<unsigned char>(pattern[i]))) {
      capitalized = false;
      break;
    }
  }
  if (capitalized) {
    std::string out = to_lower(value);
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
  }
  return value;
}

}  // namespace xsql::cli
