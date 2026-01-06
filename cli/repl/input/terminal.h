#pragma once

namespace xsql::cli {

/// Manages raw terminal mode and bracketed paste for interactive editing.
/// MUST restore terminal settings on destruction to avoid leaving the shell broken.
/// Inputs are implicit terminal state; side effects include ioctl and escape writes.
class TermiosGuard {
 public:
  /// Enables raw mode to read individual keystrokes for line editing.
  /// MUST succeed entirely or fall back without partial terminal changes.
  /// Inputs are current termios settings; side effects include terminal updates.
  TermiosGuard();
  /// Restores terminal settings and disables bracketed paste.
  /// MUST run even on early returns to avoid leaving raw mode enabled.
  /// Inputs are stored termios state; side effects include terminal resets.
  ~TermiosGuard();
  /// Reports whether raw mode was successfully enabled.
  /// MUST be checked before relying on raw input behavior.
  /// Inputs are none; outputs are boolean with no side effects.
  bool ok() const;

 private:
  void* original_ = nullptr;
  bool ok_ = false;
};

/// Detects terminal width for cursor positioning and line wrapping.
/// MUST fall back to a sane default when detection fails.
/// Inputs are terminal state; outputs are column count with no side effects.
int terminal_width();

}  // namespace xsql::cli
