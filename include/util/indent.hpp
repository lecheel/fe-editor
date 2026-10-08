#pragma once
#include <string>
#include <vector>

// Per-language indentation settings.
struct IndentInfo {
    bool use_tabs{false};
    int  tab_size{4};
    std::string unit{"    "};
};

// Returns the indentation unit (tabs or spaces) for the given language id.
IndentInfo get_indent_info_for_lang(const std::string& lang);

// Returns the line-comment prefix ("//", "#", or "") for the given language.
std::string get_line_comment_prefix(const std::string& lang);

// Computes the indentation string for `lines[line_idx]` based on the previous
// non-blank line, bracket nesting, and (for python) dedent keywords.
// Returns "" for line_idx <= 0.
std::string compute_line_indent(const std::vector<std::string>& lines,
                                int line_idx,
                                const std::string& lang);