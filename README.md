# fe

> **A terminal modal editor blending classic Vim mechanics with old-school Alt-key muscle memory—built for developers who prioritize workflow over dogmatic compatibility.**

`fe` is a fast, keyboard-centric text editor built on [Notcurses](https://github.com/dankamongstuff/notcurses) and Tree-sitter. It combines the structured grammar of Vim modal editing with instant Alt-key finger memory and native developer tooling—freeing you from bloated plugin ecosystems and excessive mode toggling.

---

## Motivation & Philosophy

### Not Another Vim Clone
`fe` is **deliberately not 100% Vim-compatible**. It does not attempt to reproduce every esoteric Ex command, legacy quirk, or Vimscript runtime idiosyncrasy. 

Instead, it treats Vim’s modal grammar (`h/j/k/l`, `d`, `c`, `y`, `/`, `n`, `.`) as a proven foundation for text manipulation, while fearlessly redesigning everything else around modern developer ergonomics.

### Built for Dedicated Developers Refining Their Workflow
In standard Vim/Neovim setups, achieving a productive engineering environment requires stitching together dozens of plugins (Fugitive, Telescope, Gitsigns, Spectre, WhichKey, etc.), each introducing configuration fragility, startup latency, and conflicting keymaps.

`fe` eliminates this plugin sprawl by building the core tools developers reach for every day directly into the engine—implemented in native C++ with zero overhead:

- **First-Class Interactive Side-by-Side Diff (`F5` / `:diff` / `:delta`)**:  
  Full-screen, two-pane diff against Git `HEAD` or between arbitrary files. Review changes side-by-side, pull/push individual hunks (`a`/`A`), set custom manual merge boundaries (`m`/`M`), and yank/paste lines across diff panes without spinning up external merge tools.
- **Ripgrep with Live Batch Replacement (`F11` / `:vg`)**:  
  Interactive project-wide search results grouped by file. Inspect matches, navigate with `{`/`}`, toggle files or lines on/off with `Space`, and switch to Replace Mode (`Tab`) to execute instantaneous multi-file refactors with visual before/after diffing.
- **Dedicated Git Cockpit (`F1`)**:  
  Inspect staged, unstaged, and untracked files with real-time unified diff preview on the right pane. Stage/unstage files (`s`), stash/pop/drop changes (`z`), and switch branches (`Enter`) without ever dropping into a terminal shell.
- **Old-School Finger Memory (Alt-Key Chords)**:  
  Sometimes switching modes just to delete a line or undo is unnecessary friction. Dedicated Alt chords (`Alt-u`, `Alt-d`, `Alt-s`, `Alt-b`, `Alt-e`) let you perform micro-corrections and window management instantly from any mode.
- **Zero-Friction Discovery**:  
  Instant execution for fast muscle memory, with an automatic WhichKey discovery popup (`Space`) appearing only when you hesitate.

---

## Highlights

- **Vim Modal Foundation**: Normal, Insert, Visual, Visual Block, and Command modes with dot-repeat (`.`) and text objects.
- **Alt-Key Quick Chords**: Direct, single-press Alt combos for window splits, buffer switching, line deletions, and undo directly inside Insert mode.
- **Live Incremental Search & Replace**: `/` search with real-time screen highlighting, `n`/`N` navigation and live visual match preview during `:'<,'>s/find/repl/g`.
- **Seamless OSC 52 & Bracketed Paste**: True clipboard sync across SSH, `tmux`, and desktop environments, paired with DEC 2004 burst suppression to eliminate paste staircasing and rendering lag.
- **Live In-Memory Git Gutter**: Real-time diff indicators (`+`, `~`, `-`) calculated in memory as you type—no disk saving required.
- **Helix TOML Themes**: Full support for Helix color schemes with live interactive preview (`F6`).

---

## Quick Start

### Build & Install

```bash
# Dependencies: cmake, g++/clang, notcurses, libgit2, tree-sitter
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Run
./fe
```

### Command Line Usage

```bash
fe file.cpp                # Open file
fe file.cpp +42:10         # Open at line 42, column 10
fe file.cpp:42             # Alternate line syntax
fe -d file1.cpp file2.cpp  # Side-by-side delta diff
```

---

## Finger Memory & Keybindings

### 1. Alt-Key Quick Chords (Zero Mode-Switch Friction)

Designed for instant tactile finger memory without leaving your current mode:

| Shortcut | Context | Action |
| :--- | :--- | :--- |
| `Alt - e` | Global | Fuzzy file picker across repository |
| `Alt - b` | Global | Fuzzy buffer list popup |
| `Alt - -` / `Alt - =` | Global | Switch to previous / next buffer |
| `Alt - s` | Global | Split window horizontally |
| `Alt - v` | Global | Split window vertically |
| `Alt - x` | Global | Close active window split |
| `Alt - w` / `Tab` | Global | Cycle focus between split windows |
| `Alt - 0` | Global | Open Workspace Slots manager (0–4) |
| `Alt - u` | Insert Mode | Undo last edit without leaving Insert mode |
| `Alt - d` | Insert Mode | Delete current line without leaving Insert mode |
| `Alt - /` | Insert Mode | Trigger manual word autocomplete |
| `Alt - k` | Normal Mode | Add multi-cursor on the line above |
| `Alt - q` | Global | Emergency quick quit |

---

### 2. Leader & WhichKey Menu (`Space`)

Press `Space` to activate the leader menu. A WhichKey popup appears if you pause:

| Shortcut | Action |
| :--- | :--- |
| `Space f` | Fuzzy file finder |
| `Space g` | Ripgrep word under cursor (grouped popup) |
| `Space y` | Copy selection / line to system clipboard (OSC 52) |
| `Space p v` | Paste from system clipboard at cursor |
| `Space p p` | Full buffer replace from system clipboard |
| `Space w` | Save active buffer (`:w`) |
| `Space q` | Quit editor (`:q`) |
| `Space d` | Open full side-by-side Git diff (`F5`) |
| `Space h` | Open Git hunk popup (`F4`) |
| `Space j` / `k` | Jump to next / previous Git hunk |
| `Space l` | Open rounded Settings popup (`F9`) |
| `Space u` | Undo |

---

### 3. Navigation & Editing (Vim Core)

- **Motions**: `h` `j` `k` `l`, `0` (line start), `$` (line end), `gg` (file top), `G` (file bottom).
- **Mutations**: 
  - `dd`, `dw`, `db`, `de`, `d^`, `d0`, `d$`, `dG`, `dj`, `dk`
  - `x` (delete char), `X` (delete char backward)
  - `r<char>` (replace single char under cursor or across visual selection)
  - `yy` (yank line), `p` (paste after), `P` (paste before)
  - `.` (repeat last change)
  - `u` / `U` (undo / redo)
  - `C` (add multi-cursor on line below), `Esc` (clear multi-cursors)

---

### 4. Search & Replace

| Shortcut / Command | Action |
| :--- | :--- |
| `/pattern` | Live incremental search with real-time screen highlighting |
| `n` / `N` | Jump to next / previous match (circular wrap configurable via `F9` or `:ws`) |
| `:noh` / `:nohl` | Clear search highlight |
| `:s/old/new/g` | Substitute on current line |
| `:%s/old/new/g` | Substitute throughout entire file |
| `:'<,'>s/old/new/g` | Substitute within visual selection (live match highlighting while typing) |
| `F11` / `:vg <pat>` | Full-screen interactive ripgrep popup with inline batch replace |

---

### 5. Built-in Workflow Cockpits (Function Keys)

| Key | View / Tool | Description |
| :--- | :--- | :--- |
| `F1` | **Git Cockpit** | Full interactive status: stage/unstage (`s`), stash/pop/drop (`z`), branch checkout (`Enter`), and real-time diff preview. |
| `F2` / `F3` | **Hunk Jumps** | Jump to previous / next in-memory Git hunk in the buffer. |
| `F4` | **Hunk Popup** | Floating diff preview for the active hunk; press `r` inside to instantly revert. |
| `F5` | **DiffView / Delta** | Full-screen side-by-side diff against `HEAD` or arbitrary files; merge hunks with `a`/`A` and mark custom ranges with `m`/`M`. |
| `F6` | **Theme Studio** | Live interactive theme picker supporting Helix TOML color schemes. |
| `F9` | **Settings Panel** | Live editor options: line numbers, gutter width, scroll margins, and search wrap. |
| `F11` | **Ripgrep Studio** | Grouped repository search with match exclusion (`Space`) and batch replacement (`Tab`). |
| `F12` | **Quick Help** | Instant on-screen keybinding reference overlay. |

---

## Configuration

Settings and mappings are stored in standard XDG paths (`~/.config/fe/`):

- `config.json`: Persistent options (line numbers, scrolloff, theme, search wrap, cursor positions).
- `keymap.json`: Custom key chords for Normal, Insert, and Visual modes.
- `themes/`: Custom TOML themes (Helix compatible).
- `cmd_history`: Persistent command-line history.

To export all available bindable action IDs for custom keymaps:
```vim
:export_func
```

---

## License

MIT
