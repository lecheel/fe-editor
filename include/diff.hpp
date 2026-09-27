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

// Aligned row for full-screen side-by-side hunk diff view (F14)
struct AlignedRow {
    int left_idx{-1};   // 0-based index into Left (working) lines, -1 for virtual pad
    int right_idx{-1};  // 0-based index into Right (HEAD) lines, -1 for virtual pad
    int hunk_idx{-1};   // index into Hunks, -1 for context
};

struct AlignedHunk {
    int id{0};
    int first_row{0};
    HunkType kind{HunkType::MODIFIED};
    int left_start{0};  // 0-based line in left (working)
    int left_count{0};  // line count in left
    int right_start{0}; // 0-based line in right (HEAD)
    int right_count{0}; // line count in right
    std::vector<std::string> left_lines;
    std::vector<std::string> right_lines;

    bool empty_left() const { return left_count == 0; }
    bool empty_right() const { return right_count == 0; }
};

struct AlignedDiff {
    std::vector<std::string> left_lines;
    std::vector<std::string> right_lines;
    std::vector<AlignedHunk> hunks;
    std::vector<AlignedRow> rows;

    int next_hunk_row(int cur_row) const;
    int prev_hunk_row(int cur_row) const;
};

// Compute side-by-side aligned diff with virtual padding between working content (left) and HEAD (right)
AlignedDiff compute_aligned_diff(const std::vector<std::string>& left,
                                 const std::vector<std::string>& right);