#include "util/keymap_json.hpp"
#include <cctype>

std::vector<KeymapJsonToken> tokenize_keymap_json(const std::string& text) {
    std::vector<KeymapJsonToken> tokens;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }
        if (c == '{') {
            tokens.push_back({KeymapJsonToken::LBRACE, "{"});
            i++;
        } else if (c == '}') {
            tokens.push_back({KeymapJsonToken::RBRACE, "}"});
            i++;
        } else if (c == ':') {
            tokens.push_back({KeymapJsonToken::COLON, ":"});
            i++;
        } else if (c == ',') {
            tokens.push_back({KeymapJsonToken::COMMA, ","});
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
            tokens.push_back({KeymapJsonToken::STRING, s});
        } else {
            i++;
        }
    }
    return tokens;
}