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

inline bool is_word_char_ac(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

inline std::string get_prefix_before_cursor(const std::string& line, int cursor_x) {
    if (cursor_x <= 0 || line.empty()) return "";
    int cx = std::min(cursor_x, static_cast<int>(line.size()));
    int start = cx;
    while (start > 0 && is_word_char_ac(line[start - 1])) {
        start--;
    }
    return line.substr(start, cx - start);
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