#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <cmath>

struct AutocompleteState {
    bool active = false;
    bool manual = false; // true if explicitly triggered via Alt-/
    std::string prefix;
    std::vector<std::string> candidates;
    size_t selected_idx = 0;

    static AutocompleteState& instance() {
        static AutocompleteState s;
        return s;
    }

    void reset() {
        active = false;
        manual = false;
        prefix.clear();
        candidates.clear();
        selected_idx = 0;
    }

    std::string get_selected_candidate() const {
        if (!active || candidates.empty() || selected_idx >= candidates.size()) {
            return "";
        }
        return candidates[selected_idx];
    }

    std::string get_ghost_suffix() const {
        if (!active || candidates.empty() || selected_idx >= candidates.size()) {
            return "";
        }
        const std::string& cand = candidates[selected_idx];
        if (cand.size() > prefix.size()) {
            if (cand.compare(0, prefix.size(), prefix) == 0) {
                return cand.substr(prefix.size());
            }
            bool ci_match = true;
            for (size_t i = 0; i < prefix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(cand[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i]))) {
                    ci_match = false;
                    break;
                }
            }
            if (ci_match) {
                return cand.substr(prefix.size());
            }
        }
        return "";
    }
};

#include <filesystem>

namespace fs = std::filesystem;

inline bool is_word_char_ac(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

inline bool is_path_char_ac(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) ||
           c == '_' || c == '-' || c == '.' || c == '/' || c == '\\' ||
           c == '~' || c == '+' || c == '@';
}

inline std::string get_prefix_before_cursor(const std::string& line, int cursor_x) {
    if (cursor_x <= 0 || line.empty()) return "";
    int cx = std::min(cursor_x, static_cast<int>(line.size()));

    // Check if there is a path prefix starting with "./" or "../" before the cursor
    int p_start = cx;
    while (p_start > 0 && is_path_char_ac(line[p_start - 1])) {
        p_start--;
    }
    std::string path_cand = line.substr(p_start, cx - p_start);
    if (path_cand.rfind("./", 0) == 0 || path_cand.rfind("../", 0) == 0) {
        return path_cand;
    }

    // Fall back to identifier word prefix
    int start = cx;
    while (start > 0 && is_word_char_ac(line[start - 1])) {
        start--;
    }
    return line.substr(start, cx - start);
}

inline std::vector<std::string> find_path_candidates(
    const std::string& prefix,
    const std::string& project_dir = ".",
    const std::string& buffer_path = "",
    const std::vector<std::string>& buffer_lines = {}
) {
    if (prefix.rfind("./", 0) != 0 && prefix.rfind("../", 0) != 0) {
        return {};
    }

    size_t last_slash = prefix.find_last_of("/\\");
    if (last_slash == std::string::npos) {
        return {};
    }

    std::string dir_part = prefix.substr(0, last_slash + 1);
    std::string file_part = prefix.substr(last_slash + 1);

    std::vector<std::string> exact_matches;
    std::vector<std::string> ci_matches;
    std::unordered_set<std::string> seen;

    std::string fp_lower = file_part;
    std::transform(fp_lower.begin(), fp_lower.end(), fp_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::vector<fs::path> roots_to_check;
    if (!buffer_path.empty()) {
        std::error_code ec;
        fs::path bp(buffer_path);
        if (bp.has_parent_path()) {
            roots_to_check.push_back(bp.parent_path());
        }
    }
    std::string proj_root = !project_dir.empty() ? project_dir : ".";
    roots_to_check.push_back(fs::path(proj_root));
    if (proj_root != ".") {
        roots_to_check.push_back(fs::path("."));
    }

    for (const auto& root : roots_to_check) {
        std::error_code ec;
        fs::path search_path = root / dir_part;
        if (!fs::exists(search_path, ec) || !fs::is_directory(search_path, ec)) {
            continue;
        }

        for (const auto& entry : fs::directory_iterator(search_path, fs::directory_options::skip_permission_denied, ec)) {
            std::string fn = entry.path().filename().string();
            if (fn.empty() || fn == "." || fn == "..") continue;
            if (fn.front() == '.' && (file_part.empty() || file_part.front() != '.')) continue;

            bool is_dir = entry.is_directory(ec);
            std::string cand = dir_part + fn + (is_dir ? "/" : "");
            if (seen.find(cand) != seen.end()) continue;

            if (file_part.empty()) {
                seen.insert(cand);
                exact_matches.push_back(cand);
            } else if (fn.rfind(file_part, 0) == 0) {
                seen.insert(cand);
                exact_matches.push_back(cand);
            } else {
                std::string fn_lower = fn;
                std::transform(fn_lower.begin(), fn_lower.end(), fn_lower.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (fn_lower.rfind(fp_lower, 0) == 0) {
                    seen.insert(cand);
                    ci_matches.push_back(cand);
                }
            }
            if (exact_matches.size() + ci_matches.size() >= 50) break;
        }
        if (exact_matches.size() + ci_matches.size() >= 50) break;
    }

    // Also scan buffer lines for matching path strings
    if (!buffer_lines.empty() && exact_matches.size() + ci_matches.size() < 50) {
        for (const auto& l : buffer_lines) {
            size_t pos = 0;
            while ((pos = l.find(dir_part, pos)) != std::string::npos) {
                size_t end = pos + dir_part.size();
                while (end < l.size() && is_path_char_ac(l[end])) {
                    end++;
                }
                if (end > pos + dir_part.size()) {
                    std::string word = l.substr(pos, end - pos);
                    std::string fn = word.substr(dir_part.size());
                    if (!fn.empty() && seen.find(word) == seen.end()) {
                        if (file_part.empty() || fn.rfind(file_part, 0) == 0) {
                            seen.insert(word);
                            exact_matches.push_back(word);
                        } else {
                            std::string fn_lower = fn;
                            std::transform(fn_lower.begin(), fn_lower.end(), fn_lower.begin(),
                                           [](unsigned char c) { return std::tolower(c); });
                            if (fn_lower.rfind(fp_lower, 0) == 0) {
                                seen.insert(word);
                                ci_matches.push_back(word);
                            }
                        }
                    }
                }
                pos = (end > pos) ? end : pos + 1;
            }
            if (exact_matches.size() + ci_matches.size() >= 50) break;
        }
    }

    auto sort_cands = [](std::vector<std::string>& list) {
        std::sort(list.begin(), list.end(), [](const std::string& a, const std::string& b) {
            bool a_dir = (!a.empty() && a.back() == '/');
            bool b_dir = (!b.empty() && b.back() == '/');
            if (a_dir != b_dir) return a_dir > b_dir;
            return a < b;
        });
    };
    sort_cands(exact_matches);
    sort_cands(ci_matches);
    exact_matches.insert(exact_matches.end(), ci_matches.begin(), ci_matches.end());
    if (exact_matches.size() > 50) {
        exact_matches.resize(50);
    }
    return exact_matches;
}

inline std::vector<std::string> find_local_buffer_candidates(
    const std::vector<std::string>& lines,
    int cursor_y,
    const std::string& prefix
) {
    std::vector<std::string> exact_matches;
    std::vector<std::string> ci_matches;
    std::unordered_set<std::string> seen;

    if (prefix.empty()) return exact_matches;

    int total_lines = static_cast<int>(lines.size());
    int max_dist = std::max(cursor_y, total_lines - 1 - cursor_y);

    auto check_line = [&](int y) {
        if (y < 0 || y >= total_lines) return;
        const std::string& l = lines[y];
        size_t i = 0;
        while (i < l.size()) {
            while (i < l.size() && !is_word_char_ac(l[i])) i++;
            size_t start = i;
            while (i < l.size() && is_word_char_ac(l[i])) i++;
            if (i > start) {
                std::string word = l.substr(start, i - start);
                if (word.size() > prefix.size() && word != prefix) {
                    if (seen.find(word) == seen.end()) {
                        seen.insert(word);
                        if (word.compare(0, prefix.size(), prefix) == 0) {
                            exact_matches.push_back(word);
                        } else {
                            bool ci_match = true;
                            for (size_t k = 0; k < prefix.size(); ++k) {
                                if (std::tolower(static_cast<unsigned char>(word[k])) !=
                                    std::tolower(static_cast<unsigned char>(prefix[k]))) {
                                    ci_match = false;
                                    break;
                                }
                            }
                            if (ci_match) {
                                ci_matches.push_back(word);
                            }
                        }
                    }
                }
            }
        }
    };

    // Radiate outward from cursor_y for local proximity ranking
    for (int d = 0; d <= max_dist; ++d) {
        check_line(cursor_y - d);
        if (d > 0) {
            check_line(cursor_y + d);
        }
        if (exact_matches.size() + ci_matches.size() >= 50) break;
    }

    exact_matches.insert(exact_matches.end(), ci_matches.begin(), ci_matches.end());
    return exact_matches;
}