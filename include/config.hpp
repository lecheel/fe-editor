#pragma once
#include <string>
#include <map>

enum class LineNumberMode {
    ABSOLUTE = 0,
    RELATIVE = 1,
    HYBRID = 2
};

struct EditorSettings {
    bool show_line_numbers{true};
    LineNumberMode line_number_mode{LineNumberMode::ABSOLUTE};
    int line_number_width{4}; // 0 = Auto, 3..8 = fixed width
    bool highlight_current_line{true};
    int whichkey_delay_ms{300};
    int scroll_offset{3}; // Scroll clamp offset (scrolloff margin lines: 0..10)
    bool hunk_diff_right_syntax{false}; // F5 right panel (HEAD) syntax highlighting
};

struct FilePosition {
    int y{0};
    int x{0};
    int scroll_y{0};
};

class ConfigManager {
public:
    EditorSettings settings;

    ConfigManager();

    void load();
    void save();

    bool get_position(const std::string& path, FilePosition& out) const;
    void set_position(const std::string& path, int y, int x, int scroll_y = 0);
    const std::string& get_config_dir() const { return config_dir; }

private:
    std::string config_dir;
    std::string json_path;
    std::map<std::string, FilePosition> positions;

    void load_json(const std::string& path);
    void save_json(const std::string& path);
};