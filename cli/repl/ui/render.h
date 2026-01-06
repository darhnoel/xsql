#pragma once

#include <cstddef>
#include <string>

namespace xsql::cli {

/// Computes how many terminal rows the buffer occupies with prompts.
/// MUST match LineEditor rendering rules to keep cursor placement correct.
/// Inputs are buffer/prompt metadata; outputs are line counts.
int compute_render_lines(const std::string& buffer,
                         const std::string& prompt,
                         size_t prompt_len,
                         const std::string& cont_prompt,
                         size_t cont_prompt_len,
                         int width);

/// Computes the line index of the cursor for the current buffer.
/// MUST use the same wrapping logic as compute_render_lines.
/// Inputs are buffer/cursor/prompt metadata; outputs are line index.
int compute_cursor_line(const std::string& buffer,
                        size_t cursor,
                        const std::string& prompt,
                        size_t prompt_len,
                        const std::string& cont_prompt,
                        size_t cont_prompt_len,
                        int width);

/// Renders the buffer with optional keyword coloring.
/// MUST preserve literal text and MUST not alter buffer content.
/// Inputs are buffer text; side effects include terminal writes.
void render_buffer(const std::string& buffer,
                   bool keyword_color,
                   const std::string& cont_prompt = "");

}  // namespace xsql::cli
