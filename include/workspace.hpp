#pragma once
#include <string>
#include <vector>

struct WorkspaceWindowSnapshot {
    std::string file;
    int scroll_y{0};
    int scroll_x{0};
};

struct WorkspaceSnapshot {
    std::string description;
    std::vector<std::string> buffers;
    std::vector<WorkspaceWindowSnapshot> windows;
    int active_idx{0};

    bool is_empty() const {
        return buffers.empty();
    }
};

class WorkspaceStorage {
public:
    explicit WorkspaceStorage(std::string config_dir);

    std::string get_slot_path(int slot) const;
    std::string get_state_path() const;

    bool load_snapshot(int slot, WorkspaceSnapshot& out) const;
    bool save_snapshot(int slot, const WorkspaceSnapshot& snapshot);
    bool delete_snapshot(int slot);

    int get_last_active() const;
    void set_last_active(int slot);

    std::string get_description(int slot) const;
    bool set_description(int slot, const std::string& desc);

private:
    std::string config_dir;
};