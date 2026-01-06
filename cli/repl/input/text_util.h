#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xsql::cli {

/// Computes the UTF-8 sequence length from a leading byte.
/// MUST return 1 for invalid sequences to avoid infinite loops.
/// Inputs are raw bytes; outputs are byte counts with no side effects.
size_t utf8_sequence_length(unsigned char lead);

/// Decodes a UTF-8 codepoint and reports its byte length.
/// MUST return a single-byte fallback on malformed sequences.
/// Inputs are text/index; outputs are codepoint + bytes consumed.
uint32_t decode_utf8(const std::string& text, size_t index, size_t* bytes);

/// Returns whether a codepoint is a combining mark.
/// MUST treat zero-width joiners as combining to preserve cursor width.
/// Inputs are codepoints; outputs are boolean with no side effects.
bool is_combining_mark(uint32_t cp);

/// Returns whether a codepoint is a Khmer combining mark.
/// MUST map Khmer diacritics to zero display width.
/// Inputs are codepoints; outputs are boolean with no side effects.
bool is_khmer_combining(uint32_t cp);

/// Returns display width for a codepoint (currently 0 or 1).
/// MUST treat combining marks as width 0.
/// Inputs are codepoints; outputs are width with no side effects.
int display_width(uint32_t cp);

/// Finds the byte index of the previous codepoint boundary.
/// MUST return 0 when at the start of the string.
/// Inputs are text/index; outputs are byte offsets.
size_t prev_codepoint_start(const std::string& text, size_t index);

/// Finds the byte index of the next codepoint boundary.
/// MUST return text.size() when at or beyond the end.
/// Inputs are text/index; outputs are byte offsets.
size_t next_codepoint_start(const std::string& text, size_t index);

/// Maps a display column to a byte index within a slice.
/// MUST avoid splitting a multi-byte codepoint.
/// Inputs are text slice + target column; outputs are byte offsets.
size_t column_to_index(const std::string& text, size_t start, size_t end, size_t target_col);

/// Computes display width of a UTF-8 string slice.
/// MUST count combining marks as zero-width.
/// Inputs are text slice; outputs are column width.
size_t column_width(const std::string& text, size_t start, size_t end);

}  // namespace xsql::cli
