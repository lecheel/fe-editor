#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

struct SyntaxStyle {
    uint8_t r{220};
    uint8_t g{220};
    uint8_t b{220};
};

class HelixTheme {
public:
    HelixTheme();
    SyntaxStyle resolve(const std::string& capture) const;

private:
    std::unordered_map<std::string, SyntaxStyle> styles;
};

class SyntaxHighlighter {
public:
    SyntaxHighlighter();
    ~SyntaxHighlighter();

    bool init_for_file(const std::string& file_path);
    void update_text(const std::vector<std::string>& lines);
    std::vector<SyntaxStyle> get_line_styles(int line_idx, const std::string& line) const;

    bool is_active() const { return active; }
    const std::string& get_language() const { return language; }

private:
    std::string language;
    bool active{false};
    HelixTheme theme;

    struct Impl;
    std::unique_ptr<Impl> pimpl;

    std::vector<SyntaxStyle> fallback_highlight(const std::string& line) const;
};

std::string detect_lang(const std::string& path);