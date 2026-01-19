#include "xsql_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xsql::xsql_internal {
namespace {

const std::unordered_set<std::string>& english_stopwords() {
  static const std::unordered_set<std::string> kStopwords = {
      "a", "an", "and", "are", "as", "at", "be", "but", "by", "for", "from", "has",
      "have", "he", "her", "his", "i", "in", "is", "it", "its", "of", "on", "or",
      "she", "that", "the", "their", "they", "to", "was", "were", "with", "you"
  };
  return kStopwords;
}

const std::unordered_set<std::string>& no_stopwords() {
  static const std::unordered_set<std::string> kEmpty;
  return kEmpty;
}

bool starts_with_ci(const std::string& input, size_t pos, const std::string& token) {
  if (pos + token.size() > input.size()) return false;
  for (size_t i = 0; i < token.size(); ++i) {
    char a = static_cast<char>(std::tolower(static_cast<unsigned char>(input[pos + i])));
    char b = static_cast<char>(std::tolower(static_cast<unsigned char>(token[i])));
    if (a != b) return false;
  }
  return true;
}

size_t find_ci(const std::string& input, const std::string& token, size_t start) {
  for (size_t i = start; i + token.size() <= input.size(); ++i) {
    if (starts_with_ci(input, i, token)) return i;
  }
  return std::string::npos;
}

/// Strips HTML tags and scripted content to keep TFIDF focused on visible text.
std::string strip_html_text(const std::string& html) {
  std::string out;
  out.reserve(html.size());
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      if (starts_with_ci(html, i, "<!--")) {
        size_t end = html.find("-->", i + 4);
        i = (end == std::string::npos) ? html.size() : end + 3;
        continue;
      }
      if (starts_with_ci(html, i, "<script")) {
        size_t end = find_ci(html, "</script>", i + 7);
        i = (end == std::string::npos) ? html.size() : end + 9;
        continue;
      }
      if (starts_with_ci(html, i, "<style")) {
        size_t end = find_ci(html, "</style>", i + 6);
        i = (end == std::string::npos) ? html.size() : end + 8;
        continue;
      }
      if (starts_with_ci(html, i, "<noscript")) {
        size_t end = find_ci(html, "</noscript>", i + 9);
        i = (end == std::string::npos) ? html.size() : end + 11;
        continue;
      }
      size_t end = html.find('>', i + 1);
      i = (end == std::string::npos) ? html.size() : end + 1;
      continue;
    }
    out.push_back(html[i]);
    ++i;
  }
  return out;
}

/// Tokenizes text for TFIDF using the default summarize_content rules.
/// MUST emit lowercase word tokens so scoring is deterministic.
std::vector<std::string> tokenize_default(const std::string& text) {
  std::vector<std::string> tokens;
  std::string current;
  for (unsigned char uc : text) {
    if (std::isalnum(uc) || uc == '_') {
      current.push_back(static_cast<char>(std::tolower(uc)));
    } else {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    }
  }
  if (!current.empty()) tokens.push_back(current);
  return tokens;
}

}  // namespace

/// Computes TFIDF scores per node so each result row includes a term-score dictionary.
/// MUST return rows aligned with the input node order and capped to TOP_TERMS.
std::vector<QueryResultRow> build_tfidf_rows(const Query& query,
                                             const std::vector<HtmlNode>& nodes) {
  std::vector<QueryResultRow> rows;
  if (nodes.empty()) return rows;
  const auto& item = query.select_items[0];
  const auto& stopwords = (item.tfidf_stopwords == Query::SelectItem::TfidfStopwords::English)
                              ? english_stopwords()
                              : no_stopwords();
  std::vector<std::unordered_map<std::string, size_t>> term_counts;
  term_counts.reserve(nodes.size());
  std::vector<size_t> token_totals;
  token_totals.reserve(nodes.size());
  std::unordered_map<std::string, size_t> doc_freq;
  for (const auto& node : nodes) {
    std::string cleaned = strip_html_text(node.inner_html);
    auto tokens = tokenize_default(cleaned);
    std::unordered_map<std::string, size_t> counts;
    size_t total = 0;
    for (const auto& token : tokens) {
      if (!stopwords.empty() && stopwords.find(token) != stopwords.end()) {
        continue;
      }
      ++counts[token];
      ++total;
    }
    for (const auto& kv : counts) {
      ++doc_freq[kv.first];
    }
    term_counts.push_back(std::move(counts));
    token_totals.push_back(total);
  }
  const size_t doc_count = nodes.size();
  size_t min_df = item.tfidf_min_df;
  size_t max_df = item.tfidf_max_df == 0 ? doc_count : std::min(item.tfidf_max_df, doc_count);
  for (size_t idx = 0; idx < nodes.size(); ++idx) {
    QueryResultRow row;
    const auto& node = nodes[idx];
    row.node_id = node.id;
    row.parent_id = node.parent_id;
    row.tag = node.tag;
    row.max_depth = node.max_depth;
    row.doc_order = node.doc_order;
    size_t total = token_totals[idx];
    std::vector<std::pair<std::string, double>> scored;
    if (total > 0) {
      scored.reserve(term_counts[idx].size());
      for (const auto& kv : term_counts[idx]) {
        const std::string& term = kv.first;
        size_t df = doc_freq[term];
        if (df < min_df || df > max_df) continue;
        double tf = static_cast<double>(kv.second) / static_cast<double>(total);
        double idf = std::log((static_cast<double>(doc_count) + 1.0) /
                              (static_cast<double>(df) + 1.0)) + 1.0;
        scored.emplace_back(term, tf * idf);
      }
      std::sort(scored.begin(), scored.end(),
                [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
                });
      if (scored.size() > item.tfidf_top_terms) {
        scored.resize(item.tfidf_top_terms);
      }
      for (const auto& entry : scored) {
        row.term_scores[entry.first] = entry.second;
      }
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace xsql::xsql_internal
