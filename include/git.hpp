#pragma once
#include <string>
#include <vector>

// Snapshot of git state relevant to a single file path.
struct GitStatus {
    bool is_repo{false};
    bool tracked{false};
    std::string branch;
    std::vector<std::string> base_lines;
};

// Detect git repository status for a given file path by shelling out to git.
// Returns an empty (is_repo=false) status when the path is empty or outside
// any git work-tree. When the file is tracked, base_lines is populated from
// the git index (falling back to HEAD).
GitStatus detect_git_status(const std::string& file_path);