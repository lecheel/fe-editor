#include "buffer.hpp"
#include <fstream>

TextBuffer::TextBuffer(std::string name, std::vector<std::string> initial_lines, std::string path)
    : name(std::move(name)), file_path(std::move(path)), lines(std::move(initial_lines)) {}

void TextBuffer::push_undo(const std::vector<Cursor>& cursors) {
    undo_stack.push_back({lines, cursors});
    redo_stack.clear();
    modified = true;
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
    return true;
}