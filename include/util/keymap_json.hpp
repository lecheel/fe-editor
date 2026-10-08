#pragma once
#include <string>
#include <vector>

// Minimal JSON tokenizer used by the keymap and position parsers.
// Recognizes { } : , "string" and ignores everything else (numbers,
// true/false, whitespace). String values are unescaped during tokenization.
struct KeymapJsonToken {
    enum Type { LBRACE, RBRACE, COLON, COMMA, STRING, NUMBER, OTHER } type;
    std::string value;
};

std::vector<KeymapJsonToken> tokenize_keymap_json(const std::string& text);