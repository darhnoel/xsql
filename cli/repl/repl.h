#pragma once

#include <string>

namespace xsql::cli {

/// Carries runtime REPL settings that can be mutated during a session.
/// MUST keep fields synchronized with CLI flags and MUST remain valid for run_repl.
/// Inputs are from CLI parsing; outputs affect interactive behavior.
struct ReplConfig {
  std::string input;
  bool color = true;
  bool highlight = true;
  std::string output_mode = "duckbox";
  int timeout_ms = 5000;
};

/// Runs the interactive REPL loop and returns an exit status.
/// MUST respect config overrides and MUST not throw on normal user exits.
/// Inputs are config values; outputs are a process status with terminal IO side effects.
int run_repl(ReplConfig& config);

}  // namespace xsql::cli
