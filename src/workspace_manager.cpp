#include "engine.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace Keymap;

void VimEngine::open_workspace_list() {
    show_filepicker = false;
    show_settings_popup = false;
    show_git_hunk_popup = false;
    show_whichkey_popup = false;
    show_buffer_list = false;
    show_rg_popup = false;
    show_hunk_diff = false;
    show_git_status = false;
    close_cmd_completion();
    leader_pending = false;

    workspace_cursor = (workspace_slot >= 0 && workspace_slot < 5) ? workspace_slot : 0;
    workspace_editing = false;
    workspace_edit_draft.clear();
    workspace_status_msg.clear();
    show_workspace_list = true;
}

void VimEngine::close_workspace_list() {
    show_workspace_list = false;
    workspace_editing = false;
    workspace_edit_draft.clear();
    workspace_status_msg.clear();
    set_info_msg("");
}

bool VimEngine::load_workspace(int slot, bool show_msg) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceStorage storage(config.get_config_dir());
    WorkspaceSnapshot snap;
    if (!storage.load_snapshot(slot, snap) || snap.buffers.empty()) {
        if (show_msg) {
            set_info_msg("Workspace " + std::to_string(slot) + " is empty");
        }
        return false;
    }

    show_filepicker = false;
    show_settings_popup = false;
    show_git_hunk_popup = false;
    show_whichkey_popup = false;
    show_buffer_list = false;
    show_rg_popup = false;
    show_hunk_diff = false;
    show_git_status = false;
    show_mini_help = false;
    show_cmd_completion = false;
    show_workspace_list = false;
    workspace_editing = false;
    mode = Mode::NORMAL;

    buffers.clear();
    for (const auto& path : snap.buffers) {
        buffers.push_back(TextBuffer::from_file(path));
    }
    if (buffers.empty()) {
        buffers.push_back(std::make_shared<TextBuffer>("untitled", std::vector<std::string>{""}));
    }

    windows.clear();
    for (size_t wi = 0; wi < snap.windows.size(); ++wi) {
        const auto& wsnap = snap.windows[wi];
        size_t b_idx = 0;
        for (size_t bi = 0; bi < buffers.size(); ++bi) {
            if (buffers[bi]->file_path == wsnap.file || buffers[bi]->name == wsnap.file) {
                b_idx = bi;
                break;
            }
        }
        Window w;
        w.id = next_win_id++;
        w.buffer_idx = b_idx;
        restore_window_position(w, *buffers[b_idx]);
        w.scroll_y = wsnap.scroll_y;
        w.scroll_x = wsnap.scroll_x;
        w.clamp_all_cursors(*buffers[b_idx], mode);
        windows.push_back(w);
    }

    if (windows.empty()) {
        Window w;
        w.id = next_win_id++;
        w.buffer_idx = 0;
        windows.push_back(w);
    }

    active_win_idx = std::clamp(snap.active_idx, 0, static_cast<int>(windows.size()) - 1);
    split_mode = (windows.size() > 1) ? SplitType::VERTICAL : SplitType::NONE;
    layout_windows();

    workspace_slot = slot;
    workspace_active = true;
    storage.set_last_active(slot);

    if (show_msg) {
        set_info_msg("Loaded workspace " + std::to_string(slot) +
                     (snap.description.empty() ? "" : (" (" + snap.description + ")")));
    }
    return true;
}

bool VimEngine::save_workspace(int slot, bool show_msg) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceStorage storage(config.get_config_dir());

    WorkspaceSnapshot snap;
    snap.description = storage.get_description(slot);
    snap.active_idx = static_cast<int>(active_win_idx);

    for (const auto& b : buffers) {
        std::string p = !b->file_path.empty() ? b->file_path : b->name;
        if (!p.empty()) {
            snap.buffers.push_back(p);
        }
    }

    for (const auto& w : windows) {
        if (w.buffer_idx < buffers.size()) {
            const auto& b = buffers[w.buffer_idx];
            save_window_position(w, *b);
            WorkspaceWindowSnapshot wsnap;
            wsnap.file = !b->file_path.empty() ? b->file_path : b->name;
            wsnap.scroll_y = w.scroll_y;
            wsnap.scroll_x = w.scroll_x;
            snap.windows.push_back(wsnap);
        }
    }
    save_positions();
    config.save();

    bool ok = storage.save_snapshot(slot, snap);
    if (ok) {
        workspace_slot = slot;
        workspace_active = true;
        storage.set_last_active(slot);
        if (show_msg) {
            set_info_msg("Saved workspace " + std::to_string(slot));
        }
    }
    return ok;
}

bool VimEngine::delete_workspace(int slot) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceStorage storage(config.get_config_dir());
    bool ok = storage.delete_snapshot(slot);
    if (workspace_slot == slot) {
        workspace_slot = -1;
        workspace_active = false;
        storage.set_last_active(-1);
    }
    return ok;
}

bool VimEngine::clear_active_workspace() {
    WorkspaceStorage storage(config.get_config_dir());
    workspace_slot = -1;
    workspace_active = false;
    storage.set_last_active(-1);
    set_info_msg("Cleared active workspace");
    return true;
}

bool VimEngine::rename_workspace(int slot, const std::string& desc) {
    if (slot < 0 || slot >= 5) return false;
    WorkspaceStorage storage(config.get_config_dir());
    return storage.set_description(slot, desc);
}

std::string VimEngine::get_workspace_description(int slot) {
    if (slot < 0 || slot >= 5) return "";
    WorkspaceStorage storage(config.get_config_dir());
    return storage.get_description(slot);
}

void VimEngine::handle_workspace_list_input(const ncinput& ni, uint32_t key) {
    if (workspace_editing) {
        if (is_esc(ni, key)) {
            workspace_editing = false;
            workspace_edit_draft.clear();
            workspace_status_msg = "Rename cancelled";
            return;
        }
        if (is_enter(ni, key)) {
            rename_workspace(workspace_cursor, workspace_edit_draft);
            workspace_editing = false;
            workspace_status_msg = "Renamed workspace " + std::to_string(workspace_cursor);
            workspace_edit_draft.clear();
            return;
        }
        if (is_backspace(ni, key)) {
            if (!workspace_edit_draft.empty()) {
                workspace_edit_draft.pop_back();
            }
            return;
        }
        std::string ins = Keymap::get_input_text(ni, key);
        if (!ins.empty()) {
            workspace_edit_draft += ins;
            return;
        }
        return;
    }

    if (is_esc(ni, key) || key == 'q' || key == 'Q') {
        close_workspace_list();
        return;
    }

    if (key == 'j' || key == NCKEY_DOWN || is_ctrl(ni, key, 'n')) {
        workspace_cursor = (workspace_cursor + 1) % 5;
        workspace_status_msg.clear();
        return;
    }

    if (key == 'k' || key == NCKEY_UP || is_ctrl(ni, key, 'p')) {
        workspace_cursor = (workspace_cursor + 4) % 5;
        workspace_status_msg.clear();
        return;
    }

    if (key == 'g' || key == NCKEY_HOME) {
        workspace_cursor = 0;
        workspace_status_msg.clear();
        return;
    }

    if (key == 'G' || key == NCKEY_END) {
        workspace_cursor = 4;
        workspace_status_msg.clear();
        return;
    }

    if (is_enter(ni, key)) {
        WorkspaceStorage storage(config.get_config_dir());
        WorkspaceSnapshot snap;
        if (!storage.load_snapshot(workspace_cursor, snap) || snap.buffers.empty()) {
            workspace_status_msg = "Workspace " + std::to_string(workspace_cursor) + " is empty";
            return;
        }
        load_workspace(workspace_cursor);
        return;
    }

    if (key == 's' || key == 'S') {
        save_workspace(workspace_cursor);
        workspace_status_msg = "Saved workspace " + std::to_string(workspace_cursor);
        return;
    }

    if (key == 'c' || key == 'C') {
        if (workspace_cursor != workspace_slot) {
            workspace_status_msg = "Cursor must be on active workspace (" +
                                  (workspace_slot >= 0 ? std::to_string(workspace_slot) : "none") + ") to clear";
            return;
        }
        clear_active_workspace();
        workspace_status_msg = "Cleared active workspace";
        return;
    }

    if (key == 'd' || key == 'D') {
        delete_workspace(workspace_cursor);
        workspace_status_msg = "Deleted workspace " + std::to_string(workspace_cursor);
        return;
    }

    if (key == 'e' || key == 'E') {
        workspace_editing = true;
        workspace_edit_draft = get_workspace_description(workspace_cursor);
        workspace_status_msg.clear();
        return;
    }
}