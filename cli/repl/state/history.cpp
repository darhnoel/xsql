#include "history.h"

namespace xsql::cli {

History::History(size_t max_entries) : max_entries_(max_entries) {}

void History::reset_navigation() {
  index_ = entries_.size();
  current_buffer_.clear();
}

bool History::empty() const {
  return entries_.empty();
}

void History::add(const std::string& line) {
  if (line.empty()) return;
  if (!entries_.empty() && entries_.back() == line) return;
  entries_.push_back(line);
  if (entries_.size() > max_entries_) {
    entries_.erase(entries_.begin());
  }
  index_ = entries_.size();
}

bool History::prev(std::string& buffer) {
  if (entries_.empty() || index_ == 0) {
    return false;
  }
  if (index_ == entries_.size()) {
    current_buffer_ = buffer;
  }
  --index_;
  buffer = entries_[index_];
  return true;
}

bool History::next(std::string& buffer) {
  if (index_ >= entries_.size()) {
    return false;
  }
  ++index_;
  if (index_ == entries_.size()) {
    buffer = current_buffer_;
  } else {
    buffer = entries_[index_];
  }
  return true;
}

}  // namespace xsql::cli
