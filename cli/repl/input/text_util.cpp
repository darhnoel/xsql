#include "text_util.h"

#include <algorithm>
#include <cctype>

namespace xsql::cli {

size_t utf8_sequence_length(unsigned char lead) {
  if ((lead & 0x80) == 0x00) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

uint32_t decode_utf8(const std::string& text, size_t index, size_t* bytes) {
  if (index >= text.size()) {
    if (bytes) *bytes = 0;
    return 0;
  }
  unsigned char lead = static_cast<unsigned char>(text[index]);
  size_t len = utf8_sequence_length(lead);
  if (index + len > text.size()) {
    if (bytes) *bytes = 1;
    return lead;
  }
  if (len == 1) {
    if (bytes) *bytes = 1;
    return lead;
  }
  uint32_t cp = 0;
  if (len == 2) {
    cp = lead & 0x1F;
  } else if (len == 3) {
    cp = lead & 0x0F;
  } else {
    cp = lead & 0x07;
  }
  for (size_t i = 1; i < len; ++i) {
    unsigned char c = static_cast<unsigned char>(text[index + i]);
    if ((c & 0xC0) != 0x80) {
      if (bytes) *bytes = 1;
      return lead;
    }
    cp = (cp << 6) | (c & 0x3F);
  }
  if (bytes) *bytes = len;
  return cp;
}

bool is_combining_mark(uint32_t cp) {
  if ((cp >= 0x0300 && cp <= 0x036F) ||
      (cp >= 0x1AB0 && cp <= 0x1AFF) ||
      (cp >= 0x1DC0 && cp <= 0x1DFF) ||
      (cp >= 0x20D0 && cp <= 0x20FF) ||
      (cp >= 0xFE20 && cp <= 0xFE2F)) {
    return true;
  }
  if (cp == 0x200B || cp == 0x200C || cp == 0x200D) {
    return true;
  }
  return false;
}

bool is_khmer_combining(uint32_t cp) {
  return (cp >= 0x17B6 && cp <= 0x17D3) || cp == 0x17DD;
}

int display_width(uint32_t cp) {
  if (cp == 0) return 0;
  if (is_combining_mark(cp) || is_khmer_combining(cp)) {
    return 0;
  }
  return 1;
}

size_t prev_codepoint_start(const std::string& text, size_t index) {
  if (index == 0) return 0;
  size_t i = std::min(index, text.size());
  while (i > 0) {
    unsigned char c = static_cast<unsigned char>(text[i - 1]);
    if ((c & 0xC0) != 0x80) {
      return i - 1;
    }
    --i;
  }
  return 0;
}

size_t next_codepoint_start(const std::string& text, size_t index) {
  if (index >= text.size()) return text.size();
  size_t len = utf8_sequence_length(static_cast<unsigned char>(text[index]));
  return std::min(text.size(), index + len);
}

size_t column_to_index(const std::string& text,
                       size_t start,
                       size_t end,
                       size_t target_col) {
  size_t i = start;
  size_t col = 0;
  while (i < end) {
    size_t bytes = 0;
    uint32_t cp = decode_utf8(text, i, &bytes);
    int width = display_width(cp);
    if (col + width > target_col) {
      return i;
    }
    col += width;
    i += bytes ? bytes : 1;
  }
  return end;
}

size_t column_width(const std::string& text, size_t start, size_t end) {
  size_t i = start;
  size_t col = 0;
  while (i < end) {
    size_t bytes = 0;
    uint32_t cp = decode_utf8(text, i, &bytes);
    col += display_width(cp);
    i += bytes ? bytes : 1;
  }
  return col;
}

}  // namespace xsql::cli
