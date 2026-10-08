#include "util/path_line_col.hpp"
#include <cctype>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {

bool is_all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

} // namespace

FileLocationTarget parse_file_spec(const std::string& arg) {
    FileLocationTarget target;
    target.path = arg;
    target.line = -1;
    target.col  = -1;

    if (arg.empty()) return target;

    size_t last_colon = arg.rfind(':');
    if (last_colon != std::string::npos && last_colon > 0) {
        std::string part1 = arg.substr(last_colon + 1);

        if (is_all_digits(part1)) {
            size_t prev_colon = arg.rfind(':', last_colon - 1);
            if (prev_colon != std::string::npos && prev_colon > 0) {
                std::string part2 = arg.substr(prev_colon + 1,
                                                last_colon - prev_colon - 1);
                if (is_all_digits(part2)) {
                    std::string potential_path = arg.substr(0, prev_colon);
                    std::error_code ec;
                    if (!fs::exists(arg, ec) || fs::exists(potential_path, ec)) {
                        target.path = potential_path;
                        try { target.line = std::stoi(part2); } catch (...) {}
                        try { target.col  = std::stoi(part1); } catch (...) {}
                        return target;
                    }
                }
            }

            std::string potential_path = arg.substr(0, last_colon);
            std::error_code ec;
            if (!fs::exists(arg, ec) || fs::exists(potential_path, ec)) {
                target.path = potential_path;
                try { target.line = std::stoi(part1); } catch (...) {}
                target.col = -1;
                return target;
            }
        }
    }

    return target;
}

std::vector<FileLocationTarget> parse_file_location_args(
    const std::vector<std::string>& raw_args) {

    std::vector<FileLocationTarget> targets;
    int pending_line = -1;
    int pending_col  = -1;

    for (const auto& arg : raw_args) {
        if (arg.empty()) continue;

        // Check for +<line> or +<line>:<col> or +<line>,<col>
        if (arg[0] == '+' && arg.size() > 1 &&
            std::isdigit(static_cast<unsigned char>(arg[1]))) {
            int pline = -1, pcol = -1;
            size_t sep = arg.find_first_of(":,", 1);
            if (sep != std::string::npos) {
                std::string lstr = arg.substr(1, sep - 1);
                std::string cstr = arg.substr(sep + 1);
                try { pline = std::stoi(lstr); } catch (...) {}
                try { pcol  = std::stoi(cstr); } catch (...) {}
            } else {
                try { pline = std::stoi(arg.substr(1)); } catch (...) {}
            }
            pending_line = pline;
            pending_col  = pcol;
            continue;
        }

        FileLocationTarget t = parse_file_spec(arg);
        if (pending_line > 0) {
            if (t.line <= 0) {
                t.line = pending_line;
                if (pending_col > 0 && t.col <= 0) {
                    t.col = pending_col;
                }
            }
            pending_line = -1;
            pending_col  = -1;
        }
        targets.push_back(t);
    }

    // Trailing +<line> applied to last file (e.g. file1 +50)
    if (pending_line > 0 && !targets.empty()) {
        if (targets.back().line <= 0) {
            targets.back().line = pending_line;
            if (pending_col > 0 && targets.back().col <= 0) {
                targets.back().col = pending_col;
            }
        }
    }

    return targets;
}

void split_path_line_col(std::string& path, int& line, int& col) {
    size_t last_colon = path.rfind(':');
    if (last_colon == std::string::npos || last_colon == 0) return;

    std::string part1 = path.substr(last_colon + 1);
    if (!is_all_digits(part1)) return;

    std::error_code ec;
    size_t prev_colon = path.rfind(':', last_colon - 1);
    if (prev_colon != std::string::npos && prev_colon > 0) {
        std::string part2 = path.substr(prev_colon + 1,
                                        last_colon - prev_colon - 1);
        if (!is_all_digits(part2)) return;
        std::string ppath = path.substr(0, prev_colon);
        if (!fs::exists(path, ec) || fs::exists(ppath, ec)) {
            path = ppath;
            try { line = std::stoi(part2); } catch (...) {}
            try { col  = std::stoi(part1); } catch (...) {}
        }
        return;
    }

    std::string ppath = path.substr(0, last_colon);
    if (!fs::exists(path, ec) || fs::exists(ppath, ec)) {
        path = ppath;
        try { line = std::stoi(part1); } catch (...) {}
    }
}