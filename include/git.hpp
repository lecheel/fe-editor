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

struct GitStatusFile {
    char glyph{' '};
    std::string path;
};

struct GitCommitInfo {
    std::string hash;
    std::string subject;
    std::vector<GitStatusFile> files;
};

struct GitStashEntry {
    int index{0};
    std::string ref;
    std::string subject;
};

struct GitBranchEntry {
    std::string name;
    bool is_current{false};
    std::string reltime;
};

struct GitViewData {
    std::string root;
    bool is_repo{false};
    std::vector<GitStatusFile> staged;
    std::vector<GitStatusFile> unstaged;
    std::vector<GitStatusFile> untracked;
    std::vector<GitCommitInfo> recent_commits;
    std::vector<GitStashEntry> stashes;
    std::vector<GitBranchEntry> branches;
};

GitViewData query_git_view_data(const std::string& repo_root);
bool git_is_clean(const std::string& repo_root);
bool git_stage_file(const std::string& repo_root, const std::string& rel_path);
bool git_unstage_file(const std::string& repo_root, const std::string& rel_path);
bool git_checkout_branch(const std::string& repo_root, const std::string& branch_name);
bool git_stash_push(const std::string& repo_root);
bool git_stash_pop(const std::string& repo_root, int stash_idx);
bool git_stash_drop(const std::string& repo_root, int stash_idx);

std::vector<std::string> git_get_file_lines(const std::string& repo_root,
                                            const std::string& rev,
                                            const std::string& rel_path);