#include "workspace.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

namespace fs = std::filesystem;

namespace {

struct WsJsonToken {
    enum Type { LBRACE, RBRACE, LBRACKET, RBRACKET, COLON, COMMA, STRING, NUMBER, OTHER } type;
    std::string value;
};

std::vector<WsJsonToken> tokenize_ws_json(const std::string& text) {
    std::vector<WsJsonToken> tokens;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }
        if (c == '{') {
            tokens.push_back({WsJsonToken::LBRACE, "{"});
            i++;
        } else if (c == '}') {
            tokens.push_back({WsJsonToken::RBRACE, "}"});
            i++;
        } else if (c == '[') {
            tokens.push_back({WsJsonToken::LBRACKET, "["});
            i++;
        } else if (c == ']') {
            tokens.push_back({WsJsonToken::RBRACKET, "]"});
            i++;
        } else if (c == ':') {
            tokens.push_back({WsJsonToken::COLON, ":"});
            i++;
        } else if (c == ',') {
            tokens.push_back({WsJsonToken::COMMA, ","});
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
            tokens.push_back({WsJsonToken::STRING, s});
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            std::string num;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) ||
                                       text[i] == '-' || text[i] == '.')) {
                num += text[i++];
            }
            tokens.push_back({WsJsonToken::NUMBER, num});
        } else {
            i++;
        }
    }
    return tokens;
}

std::string escape_json_str(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

} // namespace

WorkspaceStorage::WorkspaceStorage(std::string dir) : config_dir(std::move(dir)) {}

std::string WorkspaceStorage::get_slot_path(int slot) const {
    return (fs::path(config_dir) / ("workspace_" + std::to_string(slot) + ".json")).string();
}

std::string WorkspaceStorage::get_state_path() const {
    return (fs::path(config_dir) / "workspace_state.json").string();
}

int WorkspaceStorage::get_last_active() const {
    std::ifstream in(get_state_path());
    if (!in.is_open()) return -1;
    std::stringstream ss;
    ss << in.rdbuf();
    auto tokens = tokenize_ws_json(ss.str());
    for (size_t i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i].type == WsJsonToken::STRING && tokens[i].value == "last_active" &&
            tokens[i + 1].type == WsJsonToken::COLON &&
            tokens[i + 2].type == WsJsonToken::NUMBER) {
            try {
                return std::stoi(tokens[i + 2].value);
            } catch (...) {}
        }
    }
    return -1;
}

void WorkspaceStorage::set_last_active(int slot) {
    std::error_code ec;
    fs::create_directories(config_dir, ec);
    std::ofstream out(get_state_path());
    if (!out.is_open()) return;
    out << "{\n  \"last_active\": " << slot << "\n}\n";
}

bool WorkspaceStorage::load_snapshot(int slot, WorkspaceSnapshot& out) const {
    if (slot < 0 || slot >= 5) return false;
    std::ifstream in(get_slot_path(slot));
    if (!in.is_open()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    auto tokens = tokenize_ws_json(ss.str());

    out = WorkspaceSnapshot{};
    size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].type == WsJsonToken::STRING && i + 2 < tokens.size() &&
            tokens[i + 1].type == WsJsonToken::COLON) {
            std::string key = tokens[i].value;
            if (key == "description" && tokens[i + 2].type == WsJsonToken::STRING) {
                out.description = tokens[i + 2].value;
                i += 3;
                continue;
            } else if (key == "active_idx" && tokens[i + 2].type == WsJsonToken::NUMBER) {
                try { out.active_idx = std::stoi(tokens[i + 2].value); } catch (...) {}
                i += 3;
                continue;
            } else if (key == "buffers" && tokens[i + 2].type == WsJsonToken::LBRACKET) {
                i += 3;
                while (i < tokens.size() && tokens[i].type != WsJsonToken::RBRACKET) {
                    if (tokens[i].type == WsJsonToken::STRING) {
                        out.buffers.push_back(tokens[i].value);
                    }
                    i++;
                }
                if (i < tokens.size()) i++;
                continue;
            } else if (key == "windows" && tokens[i + 2].type == WsJsonToken::LBRACKET) {
                i += 3;
                while (i < tokens.size() && tokens[i].type != WsJsonToken::RBRACKET) {
                    if (tokens[i].type == WsJsonToken::LBRACE) {
                        WorkspaceWindowSnapshot win;
                        i++;
                        while (i < tokens.size() && tokens[i].type != WsJsonToken::RBRACE) {
                            if (tokens[i].type == WsJsonToken::STRING && i + 2 < tokens.size() &&
                                tokens[i + 1].type == WsJsonToken::COLON) {
                                std::string prop = tokens[i].value;
                                if (prop == "file" && tokens[i + 2].type == WsJsonToken::STRING) {
                                    win.file = tokens[i + 2].value;
                                } else if (prop == "scroll_y" && tokens[i + 2].type == WsJsonToken::NUMBER) {
                                    try { win.scroll_y = std::stoi(tokens[i + 2].value); } catch (...) {}
                                } else if (prop == "scroll_x" && tokens[i + 2].type == WsJsonToken::NUMBER) {
                                    try { win.scroll_x = std::stoi(tokens[i + 2].value); } catch (...) {}
                                }
                                i += 3;
                                continue;
                            }
                            i++;
                        }
                        out.windows.push_back(win);
                    }
                    i++;
                }
                if (i < tokens.size()) i++;
                continue;
            }
        }
        i++;
    }
    return true;
}

bool WorkspaceStorage::save_snapshot(int slot, const WorkspaceSnapshot& snapshot) {
    if (slot < 0 || slot >= 5) return false;
    std::error_code ec;
    fs::create_directories(config_dir, ec);
    std::ofstream out(get_slot_path(slot));
    if (!out.is_open()) return false;

    out << "{\n";
    out << "  \"description\": \"" << escape_json_str(snapshot.description) << "\",\n";
    out << "  \"active_idx\": " << snapshot.active_idx << ",\n";
    out << "  \"buffers\": [\n";
    for (size_t i = 0; i < snapshot.buffers.size(); ++i) {
        out << "    \"" << escape_json_str(snapshot.buffers[i]) << "\""
            << (i + 1 < snapshot.buffers.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"windows\": [\n";
    for (size_t i = 0; i < snapshot.windows.size(); ++i) {
        out << "    {\n";
        out << "      \"file\": \"" << escape_json_str(snapshot.windows[i].file) << "\",\n";
        out << "      \"scroll_y\": " << snapshot.windows[i].scroll_y << ",\n";
        out << "      \"scroll_x\": " << snapshot.windows[i].scroll_x << "\n";
        out << "    }" << (i + 1 < snapshot.windows.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

bool WorkspaceStorage::delete_snapshot(int slot) {
    if (slot < 0 || slot >= 5) return false;
    std::error_code ec;
    return fs::remove(get_slot_path(slot), ec);
}

std::string WorkspaceStorage::get_description(int slot) const {
    WorkspaceSnapshot snap;
    if (load_snapshot(slot, snap)) {
        return snap.description;
    }
    return "";
}

bool WorkspaceStorage::set_description(int slot, const std::string& desc) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceSnapshot snap;
    load_snapshot(slot, snap);
    snap.description = desc;
    return save_snapshot(slot, snap);
}