#pragma once
#include <string>
#include <vector>

// Hunk classification for in-memory git-style diffs.
enum class HunkType {
    ADDED,
    MODIFIED,
    DELETED
};

// A single diff hunk describing the transformation between two line vectors.
struct GitHunk {
    int orig_start{0};  // 0-based line in base version
    int orig_count{0};  // line count in base version
    int cur_start{0};   // 0-based line in current buffer
    int cur_count{0};   // line count in current buffer
    HunkType type{HunkType::MODIFIED};
    std::vector<std::string> orig_lines;
    std::vector<std::string> cur_lines;
};

// Compute a Myers diff between two line vectors, grouping consecutive
// insert/delete operations into hunks suitable for git-style display.
std::vector<GitHunk> compute_myers_diff(const std::vector<std::string>& a,
                                        const std::vector<std::string>& b);