#include "history.h"

#include <filesystem>
#include <fstream>

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
  add_entry(line);
  if (persist_ && !path_.empty()) {
    std::ofstream out(path_, std::ios::app);
    if (out) {
      out << line << "\n";
    }
  }
}

void History::set_max_entries(size_t max_entries) {
  if (max_entries == 0) return;
  max_entries_ = max_entries;
  trim_to_max();
}

bool History::set_path(const std::string& path, std::string& error) {
  path_ = path;
  persist_ = false;
  if (path_.empty()) {
    return true;
  }
  std::filesystem::path parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      error = "Failed to create history directory: " + ec.message();
      return false;
    }
  }
  if (!load_from_file(error)) {
    return false;
  }
  persist_ = true;
  return true;
}

void History::add_entry(const std::string& line) {
  entries_.push_back(line);
  trim_to_max();
  index_ = entries_.size();
}

bool History::load_from_file(std::string& error) {
  entries_.clear();
  index_ = 0;
  if (!std::filesystem::exists(path_)) {
    return true;
  }
  std::ifstream in(path_);
  if (!in) {
    error = "Failed to open history file: " + path_;
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (!entries_.empty() && entries_.back() == line) continue;
    entries_.push_back(line);
    trim_to_max();
  }
  index_ = entries_.size();
  return true;
}

void History::trim_to_max() {
  while (entries_.size() > max_entries_) {
    entries_.erase(entries_.begin());
  }
  if (index_ > entries_.size()) {
    index_ = entries_.size();
  }
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
