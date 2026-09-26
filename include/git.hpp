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

// Detect git repository status for a given file path using libgit2.
// Returns an empty (is_repo=false) status when the path is empty or outside
// any git work-tree. When the file is tracked, base_lines is populated from
// the git index (falling back to HEAD).
GitStatus detect_git_status(const std::string& file_path);

// Detect git repository top-level directory using libgit2.
std::string detect_git_repo_root(const std::string& start_path);

// Collect all tracked and untracked (non-ignored) project files using libgit2.
bool get_git_project_files(const std::string& repo_root, std::vector<std::string>& files);