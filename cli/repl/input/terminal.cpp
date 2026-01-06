#include "terminal.h"

#include <iostream>

#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace xsql::cli {
namespace {

struct TermiosState {
  termios value{};
};

}  // namespace

TermiosGuard::TermiosGuard() : ok_(false) {
  auto* state = new TermiosState();
  if (tcgetattr(STDIN_FILENO, &state->value) != 0) {
    delete state;
    return;
  }
  termios raw = state->value;
  raw.c_lflag &= ~(ECHO | ICANON);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
    delete state;
    return;
  }
  // WHY: bracketed paste prevents pasted content from triggering editor controls.
  std::cout << "\033[?2004h" << std::flush;
  original_ = state;
  ok_ = true;
}

TermiosGuard::~TermiosGuard() {
  if (!ok_ || !original_) {
    return;
  }
  auto* state = static_cast<TermiosState*>(original_);
  std::cout << "\033[?2004l" << std::flush;
  tcsetattr(STDIN_FILENO, TCSANOW, &state->value);
  delete state;
  original_ = nullptr;
}

bool TermiosGuard::ok() const {
  return ok_;
}

int terminal_width() {
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    return ws.ws_col;
  }
  return 80;
}

}  // namespace xsql::cli
