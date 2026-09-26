#pragma once
#include "types.hpp"
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

    TextBuffer(std::string name, std::vector<std::string> initial_lines, std::string path = "");
    static std::shared_ptr<TextBuffer> from_file(const std::string& path);

    void push_undo(const std::vector<Cursor>& cursors);
    bool undo(std::vector<Cursor>& cursors);
    bool redo(std::vector<Cursor>& cursors);
    bool save_to_file(const std::string& path_override = "");
};