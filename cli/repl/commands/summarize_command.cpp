#include "summarize_command.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "../../cli_utils.h"
#include "../../render/duckbox_renderer.h"
#include "../../ui/color.h"
#include "../../../core/src/html_parser.h"

namespace xsql::cli {

CommandHandler make_summarize_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".summarize", 0) != 0) {
      return false;
    }
    std::istringstream iss(line);
    std::string cmd;
    std::string target;
    iss >> cmd >> target;
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
    try {
      std::string html;
      if (use_alias) {
        auto it = ctx.sources.find(alias);
        if (it == ctx.sources.end() || it->second.source.empty()) {
          if (!alias.empty()) {
            std::cerr << "No input loaded for alias '" << alias
                      << "'. Use .load <path|url> --alias " << alias << "." << std::endl;
          } else {
            std::cerr << "No input loaded. Use .load <path|url> or start with --input <path|url>." << std::endl;
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
      xsql::HtmlDocument doc = xsql::parse_html(html);
      std::unordered_map<std::string, size_t> counts;
      for (const auto& node : doc.nodes) {
        ++counts[node.tag];
      }
      std::vector<std::pair<std::string, size_t>> summary;
      summary.reserve(counts.size());
      for (const auto& kv : counts) {
        summary.emplace_back(kv.first, kv.second);
      }
      std::sort(summary.begin(), summary.end(),
                [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
                });
      if (ctx.config.output_mode == "duckbox") {
        xsql::QueryResult result;
        result.columns = {"tag", "count"};
        for (const auto& item : summary) {
          xsql::QueryResultRow row;
          row.tag = item.first;
          row.node_id = static_cast<int64_t>(item.second);
          result.rows.push_back(std::move(row));
        }
        xsql::render::DuckboxOptions options;
        options.max_width = 0;
        options.max_rows = ctx.max_rows;
        options.highlight = ctx.config.highlight;
        options.is_tty = ctx.config.color;
        std::cout << xsql::render::render_duckbox(result, options) << std::endl;
      } else {
        std::string json_out = build_summary_json(summary);
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
