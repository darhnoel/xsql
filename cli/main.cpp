#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "xsql/xsql.h"
#include "export/export_sinks.h"
#include "render/duckbox_renderer.h"
#include "cli_args.h"
#include "cli_utils.h"
#include "repl/core/repl.h"
#include "ui/color.h"

using namespace xsql::cli;

/// Entry point that parses CLI options and dispatches to batch or REPL modes.
/// MUST preserve exit codes for script usage and MUST not hide fatal errors.
/// Inputs are argc/argv; outputs are process status with IO side effects.
int main(int argc, char** argv) {
  CliOptions options;
  if (argc == 1) {
    print_startup_help(std::cout);
    return 0;
  }

  std::string arg_error;
  if (!parse_cli_args(argc, argv, options, arg_error)) {
    std::cerr << arg_error << "\n";
    return 1;
  }
  if (options.show_help) {
    print_help(std::cout);
    return 0;
  }

  std::string query = options.query;
  std::string query_file = options.query_file;
  std::string input = options.input;
  bool interactive = options.interactive;
  bool color = options.color;
  std::string output_mode = options.output_mode;
  // Assumption: default highlight is on (per CLI flag requirement); auto-disabled on non-TTY.
  bool highlight = options.highlight;
  int timeout_ms = options.timeout_ms;

  // WHY: reject unknown modes to avoid silently changing output contracts.
  if (output_mode != "duckbox" && output_mode != "json" && output_mode != "plain") {
    std::cerr << "Invalid --mode value (use duckbox|json|plain)\n";
    return 1;
  }

  try {
    if (interactive) {
      ReplConfig repl_config;
      repl_config.input = input;
      repl_config.color = color;
      repl_config.highlight = highlight;
      repl_config.output_mode = output_mode;
      repl_config.timeout_ms = timeout_ms;
      return run_repl(repl_config);
    }

    if (!query_file.empty()) {
      query = read_file(query_file);
    }
    if (query.empty()) {
      throw std::runtime_error("Missing --query or --query-file");
    }

    query = rewrite_from_path_if_needed(query);
    xsql::QueryResult result;
    auto source = parse_query_source(query);
    if (source.has_value() && source->kind == xsql::Source::Kind::Url) {
      result = xsql::execute_query_from_url(source->value, query, timeout_ms);
    } else if (source.has_value() && source->kind == xsql::Source::Kind::Path) {
      result = xsql::execute_query_from_file(source->value, query);
    } else if (source.has_value() && source->kind == xsql::Source::Kind::RawHtml) {
      result = xsql::execute_query_from_document("", query);
    } else if (source.has_value() && source->kind == xsql::Source::Kind::Fragments && !source->needs_input) {
      result = xsql::execute_query_from_document("", query);
    } else if (input.empty() || input == "document") {
      std::string html = read_stdin();
      result = xsql::execute_query_from_document(html, query);
    } else {
      if (is_url(input)) {
        result = xsql::execute_query_from_url(input, query, timeout_ms);
      } else {
        result = xsql::execute_query_from_file(input, query);
      }
    }

    if (result.export_sink.kind != xsql::QueryResult::ExportSink::Kind::None) {
      std::string export_error;
      if (!xsql::cli::export_result(result, export_error)) {
        throw std::runtime_error(export_error);
      }
      std::cout << "Wrote " << export_kind_label(result.export_sink.kind)
                << ": " << result.export_sink.path << std::endl;
      return 0;
    }

    if (output_mode == "duckbox") {
      if (result.to_table) {
        if (result.tables.empty()) {
          std::cout << "(empty table)" << std::endl;
        } else {
          for (size_t i = 0; i < result.tables.size(); ++i) {
            if (result.tables.size() > 1) {
              std::cout << "Table node_id=" << result.tables[i].node_id << std::endl;
            }
            std::cout << render_table_duckbox(result.tables[i], result.table_has_header, highlight,
                                              color, 40)
                      << std::endl;
          }
        }
      } else if (!result.to_list) {
        xsql::render::DuckboxOptions options;
        options.max_width = 0;
        options.max_rows = 40;
        options.highlight = highlight;
        options.is_tty = color;
        std::cout << xsql::render::render_duckbox(result, options) << std::endl;
      } else {
        std::string json_out = build_json_list(result);
        if (output_mode == "plain") {
          std::cout << json_out << std::endl;
        } else {
          TruncateResult truncated = truncate_output(json_out, 10, 10);
          std::cout << colorize_json(truncated.output, color) << std::endl;
        }
      }
    } else {
      std::string json_out = result.to_table ? build_table_json(result)
                            : (result.to_list ? build_json_list(result) : build_json(result));
      if (output_mode == "plain") {
        std::cout << json_out << std::endl;
      } else {
        TruncateResult truncated = truncate_output(json_out, 10, 10);
        std::cout << colorize_json(truncated.output, color) << std::endl;
      }
    }
    return 0;
  } catch (const std::exception& ex) {
    if (color) std::cerr << kColor.red;
    std::cerr << "Error: " << ex.what() << std::endl;
    if (color) std::cerr << kColor.reset;
    return 1;
  }
}
