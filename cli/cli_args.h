#pragma once

#include <string>
#include <ostream>

namespace xsql::cli {

/// Captures CLI arguments so main can dispatch without re-parsing raw argv.
/// MUST keep defaults consistent with CLI behavior and MUST validate after parsing.
/// Inputs are argv; outputs are populated fields with no side effects by itself.
struct CliOptions {
  std::string query;
  std::string query_file;
  std::string input;
  bool interactive = false;
  bool color = true;
  std::string output_mode = "duckbox";
  bool highlight = true;
  int timeout_ms = 5000;
  bool show_help = false;
};

/// Prints the brief startup help shown when no arguments are provided.
/// MUST remain user-facing and MUST not throw on stream failures.
/// Inputs are the output stream; side effects are writing help text.
void print_startup_help(std::ostream& os);
/// Prints the full help text for explicit --help.
/// MUST remain accurate to supported flags and MUST not throw on stream failures.
/// Inputs are the output stream; side effects are writing help text.
void print_help(std::ostream& os);
/// Parses CLI flags into options and reports a user-facing error string.
/// MUST leave options in a valid state and MUST return false on invalid flags.
/// Inputs are argc/argv; outputs are options/error with no external side effects.
bool parse_cli_args(int argc, char** argv, CliOptions& options, std::string& error);

}  // namespace xsql::cli
