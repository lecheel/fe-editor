#pragma once
#include <vector>
#include <string>

enum class Mode {
    NORMAL,
    INSERT,
    VISUAL,
    VISUAL_BLOCK,
    COMMAND
};

enum class SplitType {
    NONE,
    HORIZONTAL,
    VERTICAL
};

struct Cursor {
    int y{0};
    int x{0};

    bool operator<(const Cursor& o) const {
        if (y != o.y) return y < o.y;
        return x < o.x;
    }
    bool operator==(const Cursor& o) const {
        return y == o.y && x == o.x;
    }
};

struct BufferSnapshot {
    std::vector<std::string> lines;
    std::vector<Cursor> cursors;
};