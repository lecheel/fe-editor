# Technical Design Specification: `fe` Terminal Text Editor

## 1. Overview & Architecture

`fe` is a fast, lightweight, modal terminal text editor built in modern C++ (C++17) using Notcurses as its terminal presentation engine. It combines Vim modal navigation and commands, Kakoune/Helix-style multi-cursor support, native Git gutter change-tracking and hunk management (via `libgit2`), and Tree-sitter dynamic syntax highlighting.

### 1.1 High-Level Component Diagram

+-----------------------------------------------------------------------+
|                              main.cpp                                 |
|                     CLI options & main loop runner                    |
+-----------------------------------+-----------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|                             VimEngine                                 |
|  - Event Loop (Notcurses)         - Mode State (Normal/Insert/Visual/ |
|  - Layout & Window Manager          Visual Block/Command)             |
|  - Popups (Search, File, Hunk,    - Leader / WhichKey Dispatcher      |
|    Settings, Help)                                                    |
+---------+--------------------+-------------------+--------------------+
          |                    |                   |
          v                    v                   v
+------------------+ +------------------+ +------------------+
|    TextBuffer    | |      Window      | |  ConfigManager   |
| - Line storage   | | - Viewport & dim | | - JSON parser    |
| - Undo/Redo stack| | - Cursors list   | | - Positions cache|
| - Myers Diff     | | - Visual anchor  | | - User settings  |
| - Git status     | +------------------+ +------------------+
| - Syntax binding |
+---------+--------+
          |
    +-----+--------------------+
    |                          |
    v                          v
+------------------+   +------------------+
| SyntaxHighlighter|   |     Git/Diff     |
| - Tree-sitter ABI|   | - libgit2 status |
| - Fallback Lexer |   | - Myers diff     |
| - HelixTheme     |   | - In-memory hunks|
+------------------+   +------------------+

---

## 2. Core Subsystems

### 2.1 Engine & Lifecycle (`engine.hpp`, `engine.cpp`)
- **Initialization**:
  - Initializes logging (`fe_debug.log` when `--verbose` / `-v` is passed).
  - Loads settings and per-file cursor positions from `$XDG_CONFIG_HOME/fe/config.json` (or `~/.config/fe/config.json`).
  - Detects Git repository root (`detect_git_repo_root`) or falls back to `std::filesystem::current_path()`.
  - Initializes Notcurses with banner suppression (`NCOPTION_SUPPRESS_BANNERS`).
  - Restores active windows and buffers (or opens sample walkthrough buffers if launched without file arguments).
- **Run Loop**:
  - Computes window layouts (`layout_windows`).
  - Calls `render()` to draw buffers, gutter, popups, and status/info bars.
  - Non-blocking/timed input polling via `notcurses_get()` with timeout logic for WhichKey leader popup delay (`whichkey_delay_ms`, default 300ms).
  - Routes input based on active overlay/popup or current modal state.
- **Teardown**:
  - Saves file positions for all open windows to `config.json`.
  - Terminates Notcurses plane and closes logger.

### 2.2 Text Buffer & History (`buffer.hpp`, `buffer.cpp`)
- **Representation**:
  - In-memory lines stored as `std::vector<std::string>`.
  - CRLF endings normalized to LF on load.
- **Undo / Redo Architecture**:
  - Bounded undo and redo stacks (`std::vector<BufferState>`, cap: 100 entries).
  - State captures both full lines snapshot and cursor positions (`cursors`).
  - Any mutating action calls `push_undo(win.cursors)` before mutation and sets `modified = true`, increments `version`, and marks `hunks_dirty = true`.
- **Git & Diff Caching**:
  - Maintains `git_base_lines` against index/HEAD.
  - Caches `cached_hunks` and invalidates on buffer version mismatch (`last_diff_version != version`).

### 2.3 Window & Viewport Management (`types.hpp`, `engine.cpp`)
- **Window Struct**:
  - Stores viewport dimensions (`y, x, h, w`), buffer reference index (`buffer_idx`), scroll offsets (`scroll_y, scroll_x`), and multi-cursor list (`std::vector<Cursor> cursors`).
- **Splits**:
  - Supports horizontal (`Alt-s`, `:sp`) and vertical (`Alt-v`, `:vsp`) splits up to a maximum of 4 windows.
  - Focus cycling via `Tab` or `Alt-w`. Window closure via `Alt-x` or `:q`.
- **Scrolling**:
  - Automatically keeps the primary cursor within `[scroll_y, scroll_y + win.h)`.

### 2.4 Editing, Multi-Cursor & Dot Repeat (`engine_edit.cpp`)
- **Modal Editing**:
  - **Normal Mode**: Standard `h/j/k/l` movement, `0/$`, word navigation, and line-based actions.
  - **Insert Mode**: Insertion across all active cursors simultaneously, handling offset shifts for multiple cursors on the same line.
  - **Visual & Visual Block Modes**: Single/multi-character selection or rectangular block visual mode (`Ctrl-v`). Pressing `I` or `A` in Visual Block spawns multi-cursors across each selected line.
  - **Command Mode**: Activated via `:`, parsing shell-like editor commands.
- **Multi-Cursor Operations**:
  - `C`: Spawn cursor on line below.
  - `Alt-k`: Spawn cursor on line above.
  - `Esc`: Deduplicate and collapse back to single primary cursor.
- **Repeat Mechanism (Dot Command `.`)**:
  - Tracks `s_last_dot_cmd` for repeatable deletions: `dd`, `dw`, `d^`, `d0`, `d$`, `dG`.
  - Supports pending key chord detection (`d` followed by motion).

### 2.5 In-Buffer Autocomplete (`autocomplete.hpp`, `engine_edit.cpp`)
- **Trigger**:
  - **Automatic**: When prefix length reaches >= 5 word characters (`isalnum` or `_`).
  - **Manual**: Via `Alt-/` with prefix length >= 3 characters.
- **Candidate Search**:
  - Radiates outward from `cursor_y` up and down through the buffer lines to rank candidates by local proximity.
  - Separates exact prefix matches and case-insensitive matches (capped at 50 candidates).
- **UX / Ghost Text**:
  - Inlines candidate ghost suffix directly into buffer rendering using italic, dimmed styling.
  - `Up` / `Down` arrows cycle through candidate list.
  - `Right` arrow accepts completion and commits text into buffer.

### 2.6 Diff & Myers Algorithm (`diff.hpp`, `diff.cpp`)
- **Implementation**:
  - Native Myers O(ND) greedy diff algorithm generating trace matrices and backtracking shortest edit paths.
  - Produces structured `GitHunk` representations categorized into `HunkType::ADDED`, `HunkType::DELETED`, or `HunkType::MODIFIED`.
- **Gutter Signs**:
  - Added lines: `+`
  - Modified lines: `~`
  - Deleted lines: `-`
  - Alternate style (`g_hunk_marker_style = 1`): `|` bars.
- **Interactive Hunk Diff Popup (`F4`)**:
  - Unified diff view with colored lines (`+` green, `-` red).
  - Jump between hunks using `F2` / `F3` or `j` / `k`.
  - Instant revert via `r`, pushing an undo step to permit restoration via `u`.

### 2.7 Search & Replace Subsystem (`engine_search.cpp`)
- **Ripgrep Integration**:
  - Executes `rg -n --column --no-heading --hidden -g '!.git'` via `popen`.
  - Transparent fallback to `grep -rnI --exclude-dir=.git` if `rg` is unavailable.
  - Groups results hierarchically by file.
  - Persists query and results in `$XDG_CONFIG_HOME/fe/rg_search.json` for fast reopen via `:vg` or `F11`.
- **Batch Interactive Replacement**:
  - Pressing `TAB` in the search popup toggles into Replace Mode.
  - Strikethrough preview in red for original text alongside green replacement string.
  - `Space` toggles exclusion/inclusion for whole files or individual lines.
  - Pressing `Enter` applies modifications across all selected files simultaneously and saves buffers to disk.
- **File Finder (`Alt-e` / `Space-f`)**:
  - Enumerates project files via `libgit2` index and status list.
  - Falls back to `std::filesystem::recursive_directory_iterator` with a depth limit of 3 and blacklist ignore list for un-versioned directories (`node_modules`, `build`, `target`, `vendor`, etc.).
  - Real-time case-insensitive substring filtering.

### 2.8 Syntax Highlighting & Helix Theme (`syntax.hpp`, `syntax.cpp`)
- **Dynamic Tree-sitter Runtime**:
  - Tree-sitter C ABI functions resolved dynamically via `dlopen`/`dlsym` against `libtree-sitter.so`.
  - Searches standard system paths and user directories (`~/.config/fe/runtime/grammars/`) for language parser shared libraries (`tree_sitter_<lang>.so`).
  - Parses buffer AST and executes highlight queries loaded from `highlights.scm`.
- **Regex/Lexical Fallback**:
  - Built-in token scanner providing keywords, primitives, strings, numbers, function calls, and line comments when Tree-sitter grammars are not installed.
- **Theme**:
  - `HelixTheme` captures scopes: `keyword.*`, `function.*`, `type.*`, `variable.*`, `string.*`, `comment.*`, `constant.*`, `operator`, `punctuation.*`, `diff.*`.

### 2.9 UI Rendering & HUD (`engine_render.cpp`, `engine_ui.cpp`)
- **Notcurses Plane Architecture**:
  - Renders all components into `stdplane`, flushes via `notcurses_render(nc)`.
  - Software positioning and synchronization of hardware terminal cursor (`notcurses_cursor_enable/disable`).
- **Layout Layers**:
  1. Window grid & text viewports (Gutter + line numbers + Git markers + text lines + ghost autocomplete).
  2. Status bar (Mode pill, Git branch with `+A ~M -D` statistics, relative file path, language, cursor line:col, active buffer index).
  3. Info / Command input bar (`:` prompt and transient messages).
  4. Floating overlays:
     - **Mini Help (`F12`)**: Quick key-strip bar.
     - **WhichKey Leader (`Space`)**: Delayed popup previewing leader actions.
     - **File Picker (`Alt-e`)**: Fuzzy file switcher.
     - **Ripgrep Results & Replacer (`F11` / `:vg`)**: Search & batch replace.
     - **Git Hunk Viewer (`F4`)**: Unified diff inspection & hunk reverter.
     - **Settings Popup (`F9`)**: Rounded configuration panel (line number styles, gutter width, active line highlight, hunk marker style).

---

## 3. Configuration & State Persistence (`config.hpp`, `config.cpp`)

### 3.1 JSON Format Specification
Stored at `~/.config/fe/config.json`:
{
  "settings": {
    "show_line_numbers": 1,
    "line_number_mode": 0,
    "whichkey_delay_ms": 300,
    "line_number_width": 0,
    "highlight_current_line": 1,
    "hunk_marker_style": 0
  },
  "positions": {
    "/path/to/project/main.cpp": {
      "line": 42,
      "col": 12,
      "row": 42,
      "y": 41,
      "x": 11,
      "scroll_y": 25
    }
  }
}

### 3.2 Parser Implementation
- Built-in, zero-dependency tokenizing JSON parser (`tokenize_json`) handling string literals, escapes, numeric properties, and nested dictionaries.
- Automatic absolute and filename-based path lookup normalization when restoring file coordinates.

---

## 4. Keybinding & Command Reference

| Context | Key | Action |
|---|---|---|
| **Normal** | `h` / `j` / `k` / `l` | Move cursor Left / Down / Up / Right |
| **Normal** | `0` / `$` | Move cursor to start / end of line |
| **Normal** | `i` / `a` / `o` | Insert before cursor / after cursor / new line below |
| **Normal** | `dd` / `dw` / `d^` / `d$` / `d0` / `dG` | Delete line, word, start, end, top of file, bottom of file |
| **Normal** | `.` | Repeat last delete operation |
| **Normal** | `u` / `U` | Undo / Redo |
| **Normal** | `v` / `Ctrl-v` | Character Visual Mode / Visual Block Mode |
| **Normal** | `C` / `Alt-k` | Add multi-cursor below / above |
| **Normal** | `b` / `B` | Switch to next / previous buffer |
| **Normal** | `Space` | Leader key (WhichKey menu after delay) |
| **Insert** | `Alt-u` | Undo from within Insert Mode |
| **Insert** | `Alt-d` | Delete current line from within Insert Mode |
| **Insert** | `Alt-/` | Manually trigger autocomplete |
| **Insert** | `▲` / `▼` / `▶` | Cycle previous / next / accept autocomplete candidate |
| **Visual Block** | `I` / `A` | Spawn multi-cursors across selection at start / end |
| **Visual Block** | `d` / `x` | Delete rectangular block |
| **Global** | `F2` / `F3` | Jump to previous / next Git hunk |
| **Global** | `F4` | Open Git hunk diff and revert popup |
| **Global** | `F9` | Open Editor & Gutter Settings popup |
| **Global** | `F11` | Open / Reopen Ripgrep search popup |
| **Global** | `F12` | Toggle Mini Help bar |
| **Global** | `Alt-e` | Project File Finder |
| **Global** | `Alt-s` / `Alt-v` | Horizontal / Vertical window split |
| **Global** | `Tab` / `Alt-w` | Cycle focused window |
| **Global** | `Alt-x` | Close focused window |
| **Command** | `:w [path]` | Save current buffer |
| **Command** | `:q` / `:q!` | Quit / Force quit |
| **Command** | `:wq` / `:x` | Save and quit |
| **Command** | `:e <path>` | Open or create file in new buffer |
| **Command** | `:b <n>` / `:bn` / `:bp` | Switch buffer by index / next / previous |
| **Command** | `:vg <pattern>` | Search repository with Ripgrep |
| **Command** | `:pwd` / `:proj` | Display active project root path |
| **Command** | `:cd <path>` | Change active working directory & project root |