#include "summarize_content_command.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "../../cli_utils.h"
#include "../../render/duckbox_renderer.h"
#include "../../ui/color.h"
#include "../plugin_manager.h"
#include "html_parser.h"
#include "xsql/xsql_internal.h"

namespace xsql::cli {
namespace {

std::string trim_text(const std::string& value) {
  size_t start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

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

uint32_t decode_utf8(const std::string& text, size_t index, size_t* bytes) {
  if (index >= text.size()) {
    if (bytes) *bytes = 0;
    return 0;
  }
  unsigned char lead = static_cast<unsigned char>(text[index]);
  if ((lead & 0x80) == 0) {
    if (bytes) *bytes = 1;
    return lead;
  }
  size_t len = 1;
  if ((lead & 0xE0) == 0xC0) len = 2;
  else if ((lead & 0xF0) == 0xE0) len = 3;
  else if ((lead & 0xF8) == 0xF0) len = 4;
  if (index + len > text.size()) {
    if (bytes) *bytes = 1;
    return lead;
  }
  uint32_t cp = 0;
  if (len == 2) cp = lead & 0x1F;
  else if (len == 3) cp = lead & 0x0F;
  else cp = lead & 0x07;
  for (size_t i = 1; i < len; ++i) {
    unsigned char c = static_cast<unsigned char>(text[index + i]);
    if ((c & 0xC0) != 0x80) {
      if (bytes) *bytes = 1;
      return lead;
    }
    cp = (cp << 6) | (c & 0x3F);
  }
  if (bytes) *bytes = len;
  return cp;
}

bool token_has_word_char(const std::string& token) {
  size_t i = 0;
  while (i < token.size()) {
    size_t bytes = 0;
    uint32_t cp = decode_utf8(token, i, &bytes);
    if (cp < 0x80) {
      if (std::isalnum(static_cast<unsigned char>(cp))) return true;
    } else if ((cp >= 0x1780 && cp <= 0x17FF) || (cp >= 0x19E0 && cp <= 0x19FF)) {
      return true;
    }
    i += bytes ? bytes : 1;
  }
  return false;
}

std::string to_lower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

}  // namespace

CommandHandler make_summarize_content_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".summarize_content", 0) != 0) {
      return false;
    }
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    bool show_help = false;
    std::string target;
    std::string lang = "default";
    size_t max_tokens = 20;
    std::string token;
    while (iss >> token) {
      if (token == "--help" || token == "-h") {
        show_help = true;
        continue;
      }
      if (token == "--lang") {
        iss >> lang;
        continue;
      }
      if (token == "--max_tokens") {
        std::string value;
        iss >> value;
        try {
          size_t parsed = static_cast<size_t>(std::stoul(value));
          if (parsed == 0) {
            throw std::invalid_argument("max_tokens must be > 0");
          }
          max_tokens = parsed;
        } catch (const std::exception&) {
          std::cerr << "Error: --max_tokens must be a positive integer." << std::endl;
          return true;
        }
        continue;
      }
      if (target.empty()) {
        target = token;
      }
    }
    if (show_help) {
      std::cout
          << "Usage: .summarize_content [doc|alias|path|url] [--lang <code>] [--max_tokens <n>]\n"
             "  --lang <code>       Language tokenizer (default|english|khmer)\n"
             "  --max_tokens <n>    Limit number of tokens shown\n";
      return true;
    }
    target = trim_semicolon(target);
    bool use_alias = false;
    std::string alias;
    if (target.empty() || target == "doc" || target == "document") {
      alias = ctx.active_alias;
      use_alias = true;
    } else if (ctx.sources.find(target) != ctx.sources.end()) {
      alias = target;
      use_alias = true;
    }
    lang = to_lower(lang);
    if (lang == "english") {
      lang = "default";
    }
    if (lang == "khmer" && !ctx.plugin_manager.has_tokenizer(lang)) {
      std::string error;
      if (!ctx.plugin_manager.load("khmer_segmenter", error)) {
        bool missing = error.find("No such file or directory") != std::string::npos ||
                       error.find("cannot open shared object file") != std::string::npos;
        if (missing) {
          std::cerr << "Khmer content summarization requires the khmer_segmenter plugin."
                    << std::endl;
          std::cerr << "Run: .plugin install khmer_segmenter" << std::endl;
        } else {
          std::cerr << "Error: " << error << std::endl;
        }
        return true;
      }
    }
    try {
      std::string html;
      if (use_alias) {
        auto it = ctx.sources.find(alias);
        if (it == ctx.sources.end() || it->second.source.empty()) {
          if (!alias.empty()) {
            std::cerr << "No input loaded for alias '" << alias
                      << "'. Use .load <path|url> --alias " << alias << "." << std::endl;
          } else {
            std::cerr << "No input loaded. Use .load <path|url> or start with --input <path|url>."
                      << std::endl;
          }
          return true;
        }
        if (!it->second.html.has_value()) {
          it->second.html = load_html_input(it->second.source, ctx.config.timeout_ms);
        }
        html = *it->second.html;
      } else {
        html = load_html_input(target, ctx.config.timeout_ms);
      }
      std::string raw_text = trim_text(strip_html_text(html));
      if (raw_text.empty()) {
        std::cout << "(no content)" << std::endl;
        return true;
      }
      std::unordered_map<std::string, size_t> df;
      std::unordered_map<std::string, size_t> tf;
      std::vector<std::string> tokens;
      if (lang == "default") {
        tokens = tokenize_default(raw_text);
      } else {
        std::string error;
        if (!ctx.plugin_manager.tokenize(lang, raw_text, tokens, error)) {
          std::cerr << "Error: " << error << std::endl;
          return true;
        }
      }
      if (tokens.empty()) {
        std::cout << "(no content)" << std::endl;
        return true;
      }
      std::unordered_set<std::string> seen;
      for (const auto& tok : tokens) {
        std::string cleaned = trim_text(tok);
        if (cleaned.empty()) continue;
        if (!token_has_word_char(cleaned)) continue;
        tf[cleaned] += 1;
        seen.insert(cleaned);
      }
      for (const auto& tok : seen) {
        df[tok] += 1;
      }
      size_t doc_count = 1;
      struct ScoreEntry {
        std::string token;
        size_t count = 0;
        double score = 0.0;
      };
      std::vector<ScoreEntry> scores;
      scores.reserve(tf.size());
      for (const auto& kv : tf) {
        size_t freq = kv.second;
        size_t doc_freq = df[kv.first];
        double idf = std::log((1.0 + doc_count) / (1.0 + doc_freq)) + 1.0;
        scores.push_back(ScoreEntry{kv.first, freq, freq * idf});
      }
      std::sort(scores.begin(), scores.end(),
                [](const ScoreEntry& a, const ScoreEntry& b) {
                  if (a.score != b.score) return a.score > b.score;
                  if (a.count != b.count) return a.count > b.count;
                  return a.token < b.token;
                });
      size_t limit = std::min<size_t>(scores.size(), max_tokens);
      if (ctx.config.output_mode == "duckbox") {
        xsql::QueryResult result;
        result.columns = {"token", "count", "score"};
        for (size_t i = 0; i < limit; ++i) {
          const auto& entry = scores[i];
          xsql::QueryResultRow row;
          row.node_id = static_cast<int64_t>(entry.count);
          row.attributes["token"] = entry.token;
          std::ostringstream oss;
          oss.setf(std::ios::fixed);
          oss.precision(3);
          oss << entry.score;
          row.attributes["score"] = oss.str();
          result.rows.push_back(std::move(row));
        }
        xsql::render::DuckboxOptions options;
        options.max_width = 0;
        options.max_rows = ctx.max_rows;
        options.highlight = ctx.config.highlight;
        options.is_tty = ctx.config.color;
        std::cout << xsql::render::render_duckbox(result, options) << std::endl;
      } else {
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < limit; ++i) {
          if (i > 0) out << ",";
          out << "{\"token\":\"" << scores[i].token << "\",\"count\":" << scores[i].count
              << ",\"score\":" << std::fixed << std::setprecision(3) << scores[i].score << "}";
        }
        out << "]";
        std::string json_out = out.str();
        ctx.last_full_output = json_out;
        if (ctx.display_full) {
          std::cout << colorize_json(json_out, ctx.config.color) << std::endl;
        } else {
          TruncateResult truncated = truncate_output(json_out, 10, 10);
          std::cout << colorize_json(truncated.output, ctx.config.color) << std::endl;
        }
      }
      ctx.editor.reset_render_state();
    } catch (const std::exception& ex) {
      if (ctx.config.color) std::cerr << kColor.red;
      std::cerr << "Error: " << ex.what() << std::endl;
      if (ctx.config.color) std::cerr << kColor.reset;
    }
    return true;
  };
}

}  // namespace xsql::cli
