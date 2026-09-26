#include "buffer.hpp"
#include "diff.hpp"
#include "git.hpp"
#include <fstream>
#include <memory>

TextBuffer::TextBuffer(std::string name, std::vector<std::string> initial_lines, std::string path)
    : name(std::move(name)), file_path(std::move(path)), lines(std::move(initial_lines)) {
    init_git_status();
    syntax = std::make_shared<SyntaxHighlighter>();
    syntax->init_for_file(!file_path.empty() ? file_path : name);
    syntax->update_text(lines);
}

std::shared_ptr<TextBuffer> TextBuffer::from_file(const std::string& path) {
    std::vector<std::string> loaded_lines;
    std::ifstream in(path);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            loaded_lines.push_back(line);
        }
        if (loaded_lines.empty()) {
            loaded_lines.push_back("");
        }
    } else {
        loaded_lines.push_back("");
    }
    return std::make_shared<TextBuffer>(path, loaded_lines, path);
}

void TextBuffer::init_git_status() {
    auto s = detect_git_status(file_path);
    is_git_repo   = s.is_repo;
    git_tracked   = s.tracked;
    git_branch    = std::move(s.branch);
    git_base_lines = std::move(s.base_lines);
    hunks_dirty   = true;
}

const std::vector<GitHunk>& TextBuffer::get_hunks() const {
    if (!is_git_repo) {
        cached_hunks.clear();
        return cached_hunks;
    }
    if (hunks_dirty || last_diff_version != version) {
        cached_hunks = compute_myers_diff(git_base_lines, lines);
        last_diff_version = version;
        hunks_dirty = false;
    }
    return cached_hunks;
}

void TextBuffer::push_undo(const std::vector<Cursor>& cursors) {
    undo_stack.push_back({lines, cursors});
    redo_stack.clear();
    modified = true;
    version++;
    invalidate_hunks();
    if (syntax) syntax->update_text(lines);
    if (undo_stack.size() > 100) {
        undo_stack.erase(undo_stack.begin());
    }
}

bool TextBuffer::undo(std::vector<Cursor>& cursors) {
    if (undo_stack.empty()) return false;
    redo_stack.push_back({lines, cursors});
    auto state = undo_stack.back();
    undo_stack.pop_back();
    lines = state.lines;
    cursors = state.cursors;
    modified = true;
    version++;
    invalidate_hunks();
    if (syntax) syntax->update_text(lines);
    return true;
}

bool TextBuffer::redo(std::vector<Cursor>& cursors) {
    if (redo_stack.empty()) return false;
    undo_stack.push_back({lines, cursors});
    auto state = redo_stack.back();
    redo_stack.pop_back();
    lines = state.lines;
    cursors = state.cursors;
    modified = true;
    version++;
    invalidate_hunks();
    if (syntax) syntax->update_text(lines);
    return true;
}

bool TextBuffer::save_to_file(const std::string& path_override) {
    std::string target = !path_override.empty() ? path_override : (!file_path.empty() ? file_path : name);
    std::ofstream out(target);
    if (!out.is_open()) return false;
    for (size_t i = 0; i < lines.size(); ++i) {
        out << lines[i] << "\n";
    }
    file_path = target;
    name = target;
    modified = false;
    init_git_status();
    if (syntax) {
        syntax->init_for_file(target);
        syntax->update_text(lines);
    }
    return true;
}