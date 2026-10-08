#include "util/indent.hpp"
#include <cctype>

IndentInfo get_indent_info_for_lang(const std::string& lang) {
    IndentInfo info;
    if (lang == "go") {
        info.use_tabs = true;
        info.tab_size = 4;
        info.unit = "\t";
    } else if (lang == "json" || lang == "yaml" || lang == "yml" ||
               lang == "html" || lang == "css" || lang == "toml" ||
               lang == "javascript" || lang == "typescript" || lang == "lua") {
        info.use_tabs = false;
        info.tab_size = 2;
        info.unit = "  ";
    } else {
        info.use_tabs = false;
        info.tab_size = 4;
        info.unit = "    ";
    }
    return info;
}

std::string get_line_comment_prefix(const std::string& lang) {
    if (lang == "rust" || lang == "go" || lang == "json" ||
        lang == "javascript" || lang == "typescript" ||
        lang == "cpp" || lang == "c" || lang == "c++" ||
        lang == "java" || lang == "kotlin" || lang == "swift" ||
        lang == "dart" || lang == "php" || lang == "scala") {
        return "//";
    }
    if (lang == "bash" || lang == "sh" || lang == "shell" ||
        lang == "python" || lang == "toml" ||
        lang == "yaml" || lang == "yml" || lang == "ruby" ||
        lang == "perl" || lang == "r" || lang == "makefile" ||
        lang == "dockerfile" || lang == "cmake" || lang == "nix") {
        return "#";
    }
    return "";
}

std::string compute_line_indent(const std::vector<std::string>& lines,
                                int line_idx,
                                const std::string& lang) {
    if (line_idx <= 0 || lines.empty()) return "";
    IndentInfo info = get_indent_info_for_lang(lang);

    int prev_idx = line_idx - 1;
    while (prev_idx >= 0) {
        bool all_space = true;
        for (char c : lines[prev_idx]) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                all_space = false;
                break;
            }
        }
        if (!all_space) break;
        prev_idx--;
    }
    if (prev_idx < 0) return "";

    const std::string& prev = lines[prev_idx];
    size_t p = 0;
    while (p < prev.size() && (prev[p] == ' ' || prev[p] == '\t')) p++;
    std::string base_indent = prev.substr(0, p);
    std::string trimmed_prev = prev.substr(p);

    if (lang == "python" || lang == "bash") {
        size_t h = trimmed_prev.find('#');
        if (h != std::string::npos) trimmed_prev = trimmed_prev.substr(0, h);
    } else {
        size_t c = trimmed_prev.find("//");
        if (c != std::string::npos) trimmed_prev = trimmed_prev.substr(0, c);
    }
    while (!trimmed_prev.empty() && std::isspace(static_cast<unsigned char>(trimmed_prev.back()))) {
        trimmed_prev.pop_back();
    }

    if (!trimmed_prev.empty()) {
        char b = trimmed_prev.back();
        if (b == '{' || b == '(' || b == '[' || (lang == "python" && b == ':')) {
            base_indent += info.unit;
        }
    }

    if (line_idx < static_cast<int>(lines.size())) {
        const std::string& cur = lines[line_idx];
        size_t cp = 0;
        while (cp < cur.size() && (cur[cp] == ' ' || cur[cp] == '\t')) cp++;
        std::string trimmed_cur = cur.substr(cp);

        if (!trimmed_cur.empty() &&
            (trimmed_cur[0] == '}' || trimmed_cur[0] == ')' || trimmed_cur[0] == ']')) {
            if (base_indent.size() >= info.unit.size() &&
                base_indent.compare(base_indent.size() - info.unit.size(),
                                     info.unit.size(), info.unit) == 0) {
                base_indent.erase(base_indent.size() - info.unit.size());
            } else if (base_indent.size() >= static_cast<size_t>(info.tab_size)) {
                base_indent.erase(base_indent.size() - info.tab_size);
            }
        } else if (lang == "python" && !trimmed_cur.empty()) {
            if (trimmed_cur.rfind("elif", 0) == 0 ||
                trimmed_cur.rfind("else:", 0) == 0 ||
                trimmed_cur.rfind("except", 0) == 0 ||
                trimmed_cur.rfind("finally:", 0) == 0) {
                if (base_indent.size() >= info.unit.size()) {
                    base_indent.erase(base_indent.size() - info.unit.size());
                }
            }
        }
    }

    return base_indent;
}