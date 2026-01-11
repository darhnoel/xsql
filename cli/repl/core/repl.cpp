#include "repl.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unistd.h>

#include "cli_utils.h"
#include "export/export_sinks.h"
#include "render/duckbox_renderer.h"
#include "ui/color.h"
#include "repl/commands/registry.h"
#include "repl/core/line_editor.h"
#include "repl/plugin_manager.h"

namespace xsql::cli {

int run_repl(ReplConfig& config) {
  std::string active_source = config.input;
  std::optional<std::string> active_html;
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::string line;

  if (!isatty(fileno(stdout))) {
    config.color = false;
    config.highlight = false;
  }

  std::string prompt = config.color ? (std::string(kColor.blue) + "xsql> " + kColor.reset) : "xsql> ";
  LineEditor editor(5, prompt, 6);
  editor.set_keyword_color(config.color);
  std::string cont_prompt = config.color ? (std::string(kColor.cyan) + "> " + kColor.reset) : "> ";
  editor.set_cont_prompt(cont_prompt, 2);
  CommandRegistry registry;
  register_default_commands(registry);
  PluginManager plugin_manager(registry);
  CommandContext command_ctx{
      config,
      editor,
      active_source,
      active_html,
      last_full_output,
      display_full,
      max_rows,
      plugin_manager,
  };

  while (true) {
    editor.set_prompt(prompt, 6);
    if (!editor.read_line(line)) {
      break;
    }
    line = sanitize_pasted_line(line);
    if (line == ":quit" || line == ":exit" || line == ".quit" || line == ".q") {
      break;
    }
    if (registry.try_handle(line, command_ctx)) {
      if (!line.empty()) {
        editor.add_history(line);
      }
      continue;
    }
    if (line.empty()) {
      continue;
    }
    std::string query_text = trim_semicolon(line);
    query_text = rewrite_from_path_if_needed(query_text);
    editor.add_history(query_text);
    try {
      xsql::QueryResult result;
      auto source = parse_query_source(query_text);
      if (source.has_value() && source->kind == xsql::Source::Kind::Url) {
        result = xsql::execute_query_from_url(source->value, query_text, config.timeout_ms);
      } else if (source.has_value() && source->kind == xsql::Source::Kind::Path) {
        result = xsql::execute_query_from_file(source->value, query_text);
      } else if (source.has_value() && source->kind == xsql::Source::Kind::RawHtml) {
        result = xsql::execute_query_from_document("", query_text);
      } else if (source.has_value() && source->kind == xsql::Source::Kind::Fragments && !source->needs_input) {
        result = xsql::execute_query_from_document("", query_text);
      } else {
        if (active_source.empty() && !active_html.has_value()) {
          if (config.color) std::cerr << kColor.red;
          std::cerr << "No input loaded. Use :load <path|url> or start with --input <path|url>." << std::endl;
          if (config.color) std::cerr << kColor.reset;
          continue;
        }
        if (!active_html.has_value()) {
          active_html = load_html_input(active_source, config.timeout_ms);
        }
        result = xsql::execute_query_from_document(*active_html, query_text);
        if (!active_source.empty() &&
            (!source.has_value() || source->kind == xsql::Source::Kind::Document)) {
          for (auto& row : result.rows) {
            row.source_uri = active_source;
          }
        }
      }
      if (result.export_sink.kind != xsql::QueryResult::ExportSink::Kind::None) {
        std::string export_error;
        if (!xsql::cli::export_result(result, export_error)) {
          throw std::runtime_error(export_error);
        }
        std::cout << "Wrote " << export_kind_label(result.export_sink.kind)
                  << ": " << result.export_sink.path << std::endl;
        editor.reset_render_state();
        continue;
      }
      if (config.output_mode == "duckbox") {
        if (result.to_table) {
          if (result.tables.empty()) {
            std::cout << "(empty table)" << std::endl;
          } else {
            for (size_t i = 0; i < result.tables.size(); ++i) {
              if (result.tables.size() > 1) {
                std::cout << "Table node_id=" << result.tables[i].node_id << std::endl;
              }
              std::cout << render_table_duckbox(result.tables[i], result.table_has_header,
                                                config.highlight, config.color, max_rows)
                        << std::endl;
            }
          }
        } else if (!result.to_list) {
          xsql::render::DuckboxOptions options;
          options.max_width = 0;
          options.max_rows = max_rows;
          options.highlight = config.highlight;
          options.is_tty = config.color;
          std::cout << xsql::render::render_duckbox(result, options) << std::endl;
        } else {
          std::string json_out = build_json_list(result);
          last_full_output = json_out;
          if (display_full) {
            std::cout << colorize_json(json_out, config.color) << std::endl;
          } else {
            TruncateResult truncated = truncate_output(json_out, 10, 10);
            std::cout << colorize_json(truncated.output, config.color) << std::endl;
          }
        }
      } else {
        std::string json_out = result.to_table ? build_table_json(result)
                              : (result.to_list ? build_json_list(result) : build_json(result));
        last_full_output = json_out;
        if (config.output_mode == "plain") {
          std::cout << json_out << std::endl;
        } else if (display_full) {
          std::cout << colorize_json(json_out, config.color) << std::endl;
        } else {
          TruncateResult truncated = truncate_output(json_out, 10, 10);
          std::cout << colorize_json(truncated.output, config.color) << std::endl;
        }
      }
      editor.reset_render_state();
    } catch (const std::exception& ex) {
      if (config.color) std::cerr << kColor.red;
      std::cerr << "Error: " << ex.what() << std::endl;
      if (config.color) std::cerr << kColor.reset;
      if (config.color) std::cerr << kColor.yellow;
      std::cerr << "Tip: Check SELECT/FROM/WHERE syntax." << std::endl;
      if (config.color) std::cerr << kColor.reset;
      editor.reset_render_state();
    }
  }
  return 0;
}

}  // namespace xsql::cli
