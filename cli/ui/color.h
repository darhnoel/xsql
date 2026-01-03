#pragma once

namespace xsql::cli {

/// Defines ANSI color codes for CLI output styling and diagnostics.
/// MUST remain valid ANSI sequences and MUST stay ASCII-only for terminal compatibility.
/// Inputs are the constant strings; side effects occur when printed to terminals.
struct Color {
  const char* reset = "\033[0m";
  const char* red = "\033[31m";
  const char* green = "\033[32m";
  const char* yellow = "\033[33m";
  const char* blue = "\033[34m";
  const char* magenta = "\033[35m";
  const char* cyan = "\033[36m";
  const char* dim = "\033[2m";
};

/// Provides a shared color palette instance to keep output styling consistent.
/// MUST be initialized exactly once and MUST remain immutable in normal usage.
/// Inputs/outputs are the global instance; side effects happen on usage in I/O.
extern Color kColor;

}  // namespace xsql::cli
