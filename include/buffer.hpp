#pragma once
#include "types.hpp"
#include "diff.hpp"
#include "syntax.hpp"
#include <memory>
#include <string>
#include <vector>

class TextBuffer {
public:
    std::string name;
    std::string file_path;
    std::vector<std::string> lines;
    std::vector<BufferSnapshot> undo_stack;
    std::vector<BufferSnapshot> redo_stack;
    bool modified{false};
    size_t version{0};

    bool is_git_repo{false};
    bool git_tracked{false};
    std::string git_branch;
    std::vector<std::string> git_base_lines;
    mutable std::vector<GitHunk> cached_hunks;
    mutable size_t last_diff_version{static_cast<size_t>(-1)};
    mutable bool hunks_dirty{true};

    std::shared_ptr<SyntaxHighlighter> syntax;

    TextBuffer(std::string name, std::vector<std::string> initial_lines, std::string path = "");
    static std::shared_ptr<TextBuffer> from_file(const std::string& path);

    void init_git_status();
    const std::vector<GitHunk>& get_hunks() const;
    void invalidate_hunks() const { hunks_dirty = true; }

    void push_undo(const std::vector<Cursor>& cursors);
    bool undo(std::vector<Cursor>& cursors);
    bool redo(std::vector<Cursor>& cursors);
    bool save_to_file(const std::string& path_override = "");
};