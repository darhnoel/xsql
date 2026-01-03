#include "cli_args.h"

#include <string>

namespace xsql::cli {

/// Prints the startup help so users see baseline usage without flags.
/// MUST keep examples aligned with current CLI and MUST not throw on stream errors.
/// Inputs are the output stream; side effects are writing text to stdout/stderr.
void print_startup_help(std::ostream& os) {
  os << "xsql - XSQL command line interface\n\n";
  os << "Usage:\n";
  os << "  xsql --query <query> [--input <path>]\n";
  os << "  xsql --query-file <file> [--input <path>]\n";
  os << "  xsql --interactive [--input <path>]\n";
  os << "  xsql --mode duckbox|json|plain\n";
  os << "  xsql --highlight on|off\n";
  os << "  xsql --timeout-ms <n>\n";
  os << "  xsql --color=disabled\n\n";
  os << "Notes:\n";
  os << "  - If --input is omitted, HTML is read from stdin.\n";
  os << "  - URLs are supported when libcurl is available.\n";
  os << "  - TO LIST() outputs a JSON list for a single projected column.\n";
  os << "  - TO TABLE() extracts HTML tables into rows.\n\n";
  os << "Examples:\n";
  os << "  xsql --query \"SELECT table FROM doc\" --input ./data/index.html\n";
  os << "  xsql --query \"SELECT link.href FROM doc WHERE attributes.rel = 'preload' TO LIST()\" --input ./data/index.html\n";
  os << "  xsql --interactive --input ./data/index.html\n";
}

/// Prints the explicit help requested by --help.
/// MUST stay synchronized with supported flags and MUST not throw on stream errors.
/// Inputs are the output stream; side effects are writing text to stdout/stderr.
void print_help(std::ostream& os) {
  os << "Usage: xsql --query <query> [--input <path>]\n";
  os << "       xsql --query-file <file> [--input <path>]\n";
  os << "       xsql --interactive [--input <path>]\n";
  os << "       xsql --mode duckbox|json|plain\n";
  os << "       xsql --highlight on|off\n";
  os << "       xsql --timeout-ms <n>\n";
  os << "       xsql --color=disabled\n";
  os << "If --input is omitted, HTML is read from stdin.\n";
  os << "Use TO CSV('file.csv') or TO PARQUET('file.parquet') in queries to export.\n";
}

/// Parses argv into typed options so main can dispatch consistently.
/// MUST return false for invalid flags and MUST not mutate options on parse failure.
/// Inputs are argc/argv; outputs are options/error and no external side effects.
bool parse_cli_args(int argc, char** argv, CliOptions& options, std::string& error) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--query" && i + 1 < argc) {
      options.query = argv[++i];
    } else if (arg == "--query-file" && i + 1 < argc) {
      options.query_file = argv[++i];
    } else if (arg == "--input" && i + 1 < argc) {
      options.input = argv[++i];
    } else if (arg == "--interactive") {
      options.interactive = true;
    } else if (arg == "--mode" && i + 1 < argc) {
      options.output_mode = argv[++i];
    } else if (arg == "--highlight" && i + 1 < argc) {
      std::string value = argv[++i];
      if (value == "on") {
        options.highlight = true;
      } else if (value == "off") {
        options.highlight = false;
      } else {
        // WHY: invalid highlight values must fail fast to avoid ambiguous UI state.
        error = "Invalid --highlight value (use on|off)";
        return false;
      }
    } else if (arg == "--color=disabled") {
      options.color = false;
    } else if (arg == "--timeout-ms" && i + 1 < argc) {
      options.timeout_ms = std::stoi(argv[++i]);
    } else if (arg == "--help") {
      options.show_help = true;
    }
  }
  return true;
}

}  // namespace xsql::cli
