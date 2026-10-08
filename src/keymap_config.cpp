#include "keymap.hpp"
#include "engine.hpp"
#include "action.hpp"
#include "util/keymap_json.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace Keymap;

void KeymapConfig::load(const std::string& config_dir) {
    keymap_path = (fs::path(config_dir) / "keymap.json").string();
    normal_map.clear();
    insert_map.clear();
    visual_map.clear();

    std::error_code ec;
    if (!fs::exists(keymap_path, ec)) {
        if (fs::exists("keymap.json", ec)) {
            keymap_path = "keymap.json";
        } else {
            fs::create_directories(config_dir, ec);
            std::ofstream out(keymap_path);
            if (out.is_open()) {
                out << "{\n"
                    << "  \"normal\": {\n"
                    << "    \"<C-s>\": \":w\",\n"
                    << "    \"<C-S-v>\": \"bracket_paste\",\n"
                    << "    \"<Space>w\": \":w\",\n"
                    << "    \"<Space>y\": \"copy_clipboard\",\n"
                    << "    \"<Space>pp\": \"full_replace_paste\",\n"
                    << "    \"<Space>pv\": \"paste_clipboard\",\n"
                    << "    \"<Space>q\": \":q\",\n"
                    << "    \"<Space>f\": \"filepicker\",\n"
                    << "    \"<Space>g\": \"ripgrep\",\n"
                    << "    \"<Space>d\": \"hunk_diff\",\n"
                    << "    \"<Space>u\": \"undo\",\n"
                    << "    \"<A-0>\": \"ws_list\",\n"
                    << "    \"H\": \"0\",\n"
                    << "    \"L\": \"$\"\n"
                    << "  },\n"
                    << "  \"insert\": {\n"
                    << "    \"<C-s>\": \":w\",\n"
                    << "    \"<C-S-v>\": \"bracket_paste\",\n"
                    << "    \"<A-u>\": \"undo\",\n"
                    << "    \"<A-d>\": \"delete_line\"\n"
                    << "  },\n"
                    << "  \"visual\": {\n"
                    << "    \"<C-s>\": \":w\",\n"
                    << "    \"<C-S-v>\": \"bracket_paste\",\n"
                    << "    \"<Space>y\": \"copy_clipboard\",\n"
                    << "    \"<Space>pp\": \"full_replace_paste\",\n"
                    << "    \"<Space>pv\": \"paste_clipboard\",\n"
                    << "    \"H\": \"0\",\n"
                    << "    \"L\": \"$\"\n"
                    << "  }\n"
                    << "}\n";
            }
        }
    }

    std::ifstream in(keymap_path);
    if (!in.is_open()) return;

    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    auto tokens = tokenize_keymap_json(content);

    std::string current_mode_section;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == KeymapJsonToken::STRING &&
            i + 2 < tokens.size() &&
            tokens[i + 1].type == KeymapJsonToken::COLON &&
            tokens[i + 2].type == KeymapJsonToken::LBRACE) {
            current_mode_section = tokens[i].value;
            for (char& c : current_mode_section) c = std::tolower(static_cast<unsigned char>(c));
            i += 2;
            continue;
        }

        if (tokens[i].type == KeymapJsonToken::RBRACE) {
            current_mode_section.clear();
            continue;
        }

        if (!current_mode_section.empty() &&
            tokens[i].type == KeymapJsonToken::STRING &&
            i + 2 < tokens.size() &&
            tokens[i + 1].type == KeymapJsonToken::COLON &&
            tokens[i + 2].type == KeymapJsonToken::STRING) {

            std::string key_str = Keymap::normalize_key_chord(tokens[i].value);
            std::string action_str = tokens[i + 2].value;

            if (current_mode_section == "normal" || current_mode_section == "norm" || current_mode_section == "n") {
                normal_map[key_str] = action_str;
            } else if (current_mode_section == "insert" || current_mode_section == "ins" || current_mode_section == "i") {
                insert_map[key_str] = action_str;
            } else if (current_mode_section == "visual" || current_mode_section == "vis" || current_mode_section == "v" ||
                       current_mode_section == "visual_block") {
                visual_map[key_str] = action_str;
            }
            i += 2;
        }
    }
}

bool KeymapConfig::execute_action(VimEngine& engine, Mode mode, const std::string& action) {
    if (action.empty()) return false;

    if (action.front() == ':') {
        engine.execute_command(action.substr(1));
        return true;
    }

    std::string act = action;
    for (char& c : act) c = std::tolower(static_cast<unsigned char>(c));

    if (ActionRegistry::instance().execute(act, engine, mode)) {
        return true;
    }

    engine.execute_command(action);
    return true;
}

bool KeymapConfig::has_mapping(Mode mode, const ncinput& ni, uint32_t key) const {
    std::string k_str = Keymap::key_to_string(ni, key);
    if (k_str.empty()) return false;

    const std::unordered_map<std::string, std::string>* target_map = nullptr;
    if (mode == Mode::NORMAL) target_map = &normal_map;
    else if (mode == Mode::INSERT) target_map = &insert_map;
    else if (mode == Mode::VISUAL || mode == Mode::VISUAL_BLOCK) target_map = &visual_map;

    if (!target_map) return false;
    return target_map->find(k_str) != target_map->end();
}

bool KeymapConfig::handle_key(VimEngine& engine, Mode mode, const ncinput& ni, uint32_t key) {
    std::string k_str = Keymap::key_to_string(ni, key);
    if (k_str.empty()) return false;

    const std::unordered_map<std::string, std::string>* target_map = nullptr;
    if (mode == Mode::NORMAL) target_map = &normal_map;
    else if (mode == Mode::INSERT) target_map = &insert_map;
    else if (mode == Mode::VISUAL || mode == Mode::VISUAL_BLOCK) target_map = &visual_map;

    if (!target_map) return false;

    auto it = target_map->find(k_str);
    if (it != target_map->end()) {
        return execute_action(engine, mode, it->second);
    }
    return false;
}

bool KeymapConfig::handle_whichkey(VimEngine& engine, const ncinput& ni, uint32_t key) {
    if (is_esc(ni, key)) {
        engine.set_info_msg("");
        return true;
    }

    std::string k_str = Keymap::key_to_string(ni, key);
    if (k_str.empty()) return false;

    std::string chord = "<Space>" + k_str;
    Mode cur_mode = engine.get_mode();
    const std::unordered_map<std::string, std::string>* target_map = nullptr;
    if (cur_mode == Mode::NORMAL) target_map = &normal_map;
    else if (cur_mode == Mode::VISUAL || cur_mode == Mode::VISUAL_BLOCK) target_map = &visual_map;
    else if (cur_mode == Mode::INSERT) target_map = &insert_map;

    if (target_map) {
        auto it = target_map->find(chord);
        if (it != target_map->end()) {
            return execute_action(engine, cur_mode, it->second);
        }
    }
    if (target_map != &normal_map) {
        auto it = normal_map.find(chord);
        if (it != normal_map.end()) {
            return execute_action(engine, cur_mode, it->second);
        }
    }
    return false;
}