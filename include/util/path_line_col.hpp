#pragma once
#include <string>
#include <vector>

// A file path with an optional 1-based line/column suffix.
// line == -1 / col == -1 means "unspecified".
struct FileLocationTarget {
    std::string path;
    int line = -1;
    int col  = -1;
};

// Parses a single argument of the form "path", "path:line", or "path:line:col".
// The colon is only treated as a separator if the resulting stripped path is a
// real file (or the original is not), so paths with colons (e.g. C:\...) are
// left intact when they name existing files.
FileLocationTarget parse_file_spec(const std::string& arg);

// Parses argv-style file location args. Supports:
//   path                  -> {path, -1, -1}
//   path:line             -> {path, line, -1}
//   path:line:col         -> {path, line, col}
//   +line                 -> pending line (applies to next file)
//   +line:col  / +line,col -> pending line+col (applies to next file)
//   file1 +50             -> trailing pending applies to last file
std::vector<FileLocationTarget> parse_file_location_args(
    const std::vector<std::string>& raw_args);

// In-place splits "path:line[:col]" into path/line/col. Leaves path untouched
// when the suffix isn't numeric or when the original string names an existing
// file and the stripped one does not. Used by :e, :sp, :vsp, :delta, etc.
void split_path_line_col(std::string& path, int& line, int& col);