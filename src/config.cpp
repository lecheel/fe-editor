#include "config.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <vector>

int g_hunk_marker_style = 0; // 0: ~-= (signs), 1: | (bars)

namespace fs = std::filesystem;

namespace {

struct JsonToken {
    enum Type { LBRACE, RBRACE, COLON, COMMA, STRING, NUMBER, OTHER } type;
    std::string value;
};

std::vector<JsonToken> tokenize_json(const std::string& text) {
    std::vector<JsonToken> tokens;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }
        if (c == '{') {
            tokens.push_back({JsonToken::LBRACE, "{"});
            i++;
        } else if (c == '}') {
            tokens.push_back({JsonToken::RBRACE, "}"});
            i++;
        } else if (c == ':') {
            tokens.push_back({JsonToken::COLON, ":"});
            i++;
        } else if (c == ',') {
            tokens.push_back({JsonToken::COMMA, ","});
            i++;
        } else if (c == '"') {
            i++;
            std::string s;
            while (i < text.size()) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    s += text[i + 1];
                    i += 2;
                } else if (text[i] == '"') {
                    i++;
                    break;
                } else {
                    s += text[i++];
                }
            }
            tokens.push_back({JsonToken::STRING, s});
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            std::string num;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) ||
                                       text[i] == '-' || text[i] == '.')) {
                num += text[i++];
            }
            tokens.push_back({JsonToken::NUMBER, num});
        } else {
            i++;
        }
    }
    return tokens;
}

} // namespace

ConfigManager::ConfigManager() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg != '\0') {
        config_dir = (fs::path(xdg) / "fe").string();
    } else {
        const char* home = std::getenv("HOME");
        if (home && *home != '\0') {
            config_dir = (fs::path(home) / ".config" / "fe").string();
        } else {
            config_dir = ".config/fe";
        }
    }
    json_path = (fs::path(config_dir) / "config.json").string();
}

void ConfigManager::load() {
    if (fs::exists(json_path)) {
        load_json(json_path);
    }
}

void ConfigManager::load_json(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    auto tokens = tokenize_json(content);
    for (size_t i = 0; i + 3 < tokens.size(); ++i) {
        if (tokens[i].type == JsonToken::STRING &&
            tokens[i + 1].type == JsonToken::COLON &&
            tokens[i + 2].type == JsonToken::LBRACE) {

            std::string file_key = tokens[i].value;
            if (file_key == "settings") {
                size_t j = i + 3;
                while (j + 2 < tokens.size() && tokens[j].type != JsonToken::RBRACE) {
                    if (tokens[j].type == JsonToken::STRING &&
                        tokens[j + 1].type == JsonToken::COLON) {
                        std::string k = tokens[j].value;
                        std::string v = tokens[j + 2].value;
                        if (k == "show_line_numbers") {
                            settings.show_line_numbers = (v == "1" || v == "true");
                        } else if (k == "line_number_mode") {
                            int m = 0;
                            try { m = std::stoi(v); } catch (...) {}
                            settings.line_number_mode = static_cast<LineNumberMode>(m % 3);
                        } else if (k == "line_number_width") {
                            try { settings.line_number_width = std::stoi(v); } catch (...) {}
                        } else if (k == "highlight_current_line") {
                            settings.highlight_current_line = (v == "1" || v == "true");
                        } else if (k == "whichkey_delay_ms") {
                            try { settings.whichkey_delay_ms = std::stoi(v); } catch (...) {}
                        } else if (k == "hunk_marker_style") {
                            try { g_hunk_marker_style = std::stoi(v); } catch (...) {}
                        } else if (k == "scroll_offset" || k == "scrolloff" || k == "scroll_clamp_offset") {
                            try { settings.scroll_offset = std::stoi(v); } catch (...) {}
                        } else if (k == "hunk_diff_right_syntax" || k == "diff_right_syntax") {
                            settings.hunk_diff_right_syntax = (v == "1" || v == "true");
                        } else if (k == "search_wrap" || k == "wrapscan" || k == "wrap_scan") {
                            settings.search_wrap = (v == "1" || v == "true");
                        } else if (k == "theme") {
                            settings.theme = v;
                        }
                    }
                    j++;
                }
                continue;
            }
            if (file_key == "positions") continue;

            size_t j = i + 3;
            int line = -1;
            int col = -1;
            int y_val = -1;
            int x_val = -1;
            int scroll = 0;

            while (j + 2 < tokens.size() && tokens[j].type != JsonToken::RBRACE) {
                if (tokens[j].type == JsonToken::STRING &&
                    tokens[j + 1].type == JsonToken::COLON &&
                    tokens[j + 2].type == JsonToken::NUMBER) {
                    std::string prop = tokens[j].value;
                    int val = 0;
                    try { val = std::stoi(tokens[j + 2].value); } catch (...) {}

                    if (prop == "line" || prop == "row") line = val;
                    else if (prop == "col") col = val;
                    else if (prop == "y") y_val = val;
                    else if (prop == "x") x_val = val;
                    else if (prop == "scroll_y") scroll = val;

                    j += 3;
                    if (j < tokens.size() && tokens[j].type == JsonToken::COMMA) {
                        j++;
                    }
                } else {
                    j++;
                }
            }

            if (line != -1 || y_val != -1 || col != -1 || x_val != -1) {
                int final_y = (y_val >= 0) ? y_val : (line > 0 ? line - 1 : 0);
                int final_x = (x_val >= 0) ? x_val : (col > 0 ? col - 1 : 0);
                set_position(file_key, final_y, final_x, scroll);
            }
        }
    }
}

void ConfigManager::save() {
    std::error_code ec;
    fs::create_directories(config_dir, ec);
    save_json(json_path);
}

void ConfigManager::save_json(const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n";
    out << "  \"settings\": {\n";
    out << "    \"show_line_numbers\": " << (settings.show_line_numbers ? "1" : "0") << ",\n";
    out << "    \"line_number_mode\": " << static_cast<int>(settings.line_number_mode) << ",\n";
    out << "    \"whichkey_delay_ms\": " << settings.whichkey_delay_ms << ",\n";
    out << "    \"line_number_width\": " << settings.line_number_width << ",\n";
    out << "    \"highlight_current_line\": " << (settings.highlight_current_line ? "1" : "0") << ",\n";
    out << "    \"hunk_marker_style\": " << g_hunk_marker_style << ",\n";
    out << "    \"scroll_offset\": " << settings.scroll_offset << ",\n";
    out << "    \"hunk_diff_right_syntax\": " << (settings.hunk_diff_right_syntax ? "1" : "0") << ",\n";
    out << "    \"search_wrap\": " << (settings.search_wrap ? "1" : "0") << ",\n";
    out << "    \"theme\": \"" << settings.theme << "\"\n";
    out << "  },\n";
    out << "  \"positions\": {\n";
    size_t idx = 0;
    for (auto it = positions.begin(); it != positions.end(); ++it, ++idx) {
        const auto& key = it->first;
        const auto& pos = it->second;

        out << "    \"";
        for (char c : key) {
            if (c == '"') out << "\\\"";
            else if (c == '\\') out << "\\\\";
            else out << c;
        }
        out << "\": {\n";
        out << "      \"line\": " << (pos.y + 1) << ",\n";
        out << "      \"col\": " << (pos.x + 1) << ",\n";
        out << "      \"row\": " << (pos.y + 1) << ",\n";
        out << "      \"y\": " << pos.y << ",\n";
        out << "      \"x\": " << pos.x << ",\n";
        out << "      \"scroll_y\": " << pos.scroll_y << "\n";
        out << "    }";
        if (std::next(it) != positions.end()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  }\n";
    out << "}\n";
}

bool ConfigManager::get_position(const std::string& path, FilePosition& out) const {
    if (path.empty()) return false;
    auto it = positions.find(path);
    if (it != positions.end()) {
        out = it->second;
        return true;
    }

    std::error_code ec;
    fs::path p(path);
    if (p.is_relative()) {
        std::string abs_p = fs::absolute(p, ec).lexically_normal().string();
        if (!ec) {
            it = positions.find(abs_p);
            if (it != positions.end()) {
                out = it->second;
                return true;
            }
        }
    } else {
        std::string fname = p.filename().string();
        it = positions.find(fname);
        if (it != positions.end()) {
            out = it->second;
            return true;
        }
    }
    return false;
}

void ConfigManager::set_position(const std::string& path, int y, int x, int scroll_y) {
    if (path.empty()) return;
    FilePosition pos{y, x, scroll_y};
    positions[path] = pos;

    std::error_code ec;
    fs::path p(path);
    if (p.is_relative()) {
        std::string abs_p = fs::absolute(p, ec).lexically_normal().string();
        if (!ec && !abs_p.empty()) {
            positions[abs_p] = pos;
        }
    }
}