#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

struct ColorRGB {
    uint8_t r{220};
    uint8_t g{220};
    uint8_t b{220};

    ColorRGB() = default;
    ColorRGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
};

struct UIStyle {
    ColorRGB fg{212, 212, 212};
    ColorRGB bg{30, 30, 30};
    bool has_fg{false};
    bool has_bg{false};
    bool bold{false};
    bool italic{false};
    bool underline{false};
};

struct SyntaxStyle {
    uint8_t r{220};
    uint8_t g{220};
    uint8_t b{220};

    SyntaxStyle() = default;
    SyntaxStyle(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
    SyntaxStyle(const ColorRGB& c) : r(c.r), g(c.g), b(c.b) {}
    SyntaxStyle& operator=(const ColorRGB& c) {
        r = c.r;
        g = c.g;
        b = c.b;
        return *this;
    }
};

class HelixTheme {
public:
    HelixTheme();
    static HelixTheme& instance();

    bool load_theme(const std::string& theme_name, const std::string& custom_dir = "");
    bool load_from_file(const std::string& path);

    SyntaxStyle resolve(const std::string& capture) const;
    UIStyle get_ui_style(const std::string& scope, const UIStyle& default_style = {}) const;
    ColorRGB get_color(const std::string& name_or_hex, ColorRGB default_col = {30, 30, 30}) const;

    const std::string& get_name() const { return current_theme_name; }

private:
    std::string current_theme_name{"dark_plus"};
    std::unordered_map<std::string, ColorRGB> palette;
    std::unordered_map<std::string, SyntaxStyle> styles;
    std::unordered_map<std::string, UIStyle> ui_styles;

    void init_defaults();
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