# OSC 52 & System Clipboard Integration Specification

**Project:** `fe` Editor  
**Version:** 1.0  
**Status:** Approved / Implemented  
**Scope:** Bidirectional clipboard synchronization across local environments, terminal multiplexers, and remote SSH sessions.

---

## 1. Overview & Objectives

Traditional CLI text editors either rely on platform-dependent external CLI binaries (`xclip`, `pbcopy`, `wl-copy`) which fail completely over SSH, or depend exclusively on terminal escape codes which can trigger terminal security prompts (e.g., Kitty's OSC 52 read prompt) or fail in local desktop environments.

This specification defines a **hybrid dual-channel clipboard architecture**:
1. **OSC 52 Protocol Channel:** Enables bidirectional clipboard read/write over SSH and across nested multiplexers (`tmux`, `screen`).
2. **Native OS Platform Channel:** Direct API and CLI integration (`pbcopy`, Win32 Clipboard API, `wl-copy`, `xclip`, `xsel`) for fast, zero-prompt local usage.
3. **Bracketed Paste & Burst Suppression (DEC 2004):** Prevents per-keystroke rendering bottlenecks and auto-indent staircase degradation during large pastes.

---

## 2. Wire Protocol & Sequence Encodings

### 2.1 XTerm OSC 52 Specification
OSC 52 defines an Operating System Command (OSC) sequence that writes or queries data in the host terminal's clipboard registers.

| Property | Value |
| :--- | :--- |
| **Command Header** | `ESC ] 52 ;` (`\033]52;`) |
| **Clipboard Target** | `c` (system clipboard) or `p` (primary selection) — `fe` defaults to `c` |
| **Data Payload** | RFC 4648 Standard Base64 encoded UTF-8 byte stream |
| **Terminators** | `BEL` (`\007`, ASCII 7) or `ST` (`ESC \`, `\033\\`) — `fe` uses `BEL` for maximum terminal reach |

#### Write Sequence (Host Direct)
```text
\033]52;c;<BASE64_DATA>\007
```

#### Read Query Sequence (Host Direct)
```text
\033]52;c;?\007
```

#### Expected Terminal Responses
- **Success:** `\033]52;c;<BASE64_DATA>\007` or `\033]52;c;<BASE64_DATA>\033\\`
- **Denied / Empty:** `\033]52;c;?\007` or empty timeout.

---

### 2.2 Multiplexer DCS Passthrough

Terminal multiplexers capture raw OSC sequences and drop them unless explicitly wrapped in Device Control String (DCS) passthrough frames.

```
+----------------------------------------------------------------+
| Application (fe)                                               |
+----------------------------------------------------------------+
     |
     v  (detect TMUX / screen environment)
+----------------------------------------------------------------+
| Multiplexer Layer (tmux / GNU Screen)                          |
| - Double ESC for tmux passthrough                              |
| - DCS envelope: \033P... \033\                                 |
+----------------------------------------------------------------+
     |
     v
+----------------------------------------------------------------+
| Physical Terminal (Kitty / Alacritty / WezTerm / iTerm2)       |
+----------------------------------------------------------------+
```

#### Tmux Passthrough Format
In `tmux`, inner `ESC` characters must be doubled (`\033\033`) so tmux passes the command directly to the outer terminal emulator:

- **Write:**  
  `\033Ptmux;\033\033]52;c;<BASE64_DATA>\007\033\\`
- **Read Query:**  
  `\033Ptmux;\033\033]52;c;?\007\033\\`

#### GNU Screen Passthrough Format
- **Write:**  
  `\033P\033]52;c;<BASE64_DATA>\007\033\\`
- **Read Query:**  
  `\033P\033]52;c;?\007\033\\`

---

## 3. Data Encoding Standard (Base64)

### 3.1 Encoder Specifications
- **Chunk Size:** Strict 3-byte input chunking producing 4 ASCII characters.
- **Buffer Safety:** No unbounded bit accumulation (prevents 32-bit signed integer overflow on multi-megabyte buffers).
- **Padding:** Appends `=` or `==` to guarantee standard 4-byte block alignment.

```cpp
// 3-byte chunking without arithmetic drift
for (size_t i = 0; i < len; i += 3) {
    uint32_t b = (static_cast<uint8_t>(in[i])) << 16;
    if (i + 1 < len) b |= (static_cast<uint8_t>(in[i + 1])) << 8;
    if (i + 2 < len) b |= static_cast<uint8_t>(in[i + 2]);

    out.push_back(B64_CHARS[(b >> 18) & 0x3F]);
    out.push_back(B64_CHARS[(b >> 12) & 0x3F]);
    out.push_back((i + 1 < len) ? B64_CHARS[(b >> 6) & 0x3F] : '=');
    out.push_back((i + 2 < len) ? B64_CHARS[b & 0x3F] : '=');
}
```

### 3.2 Decoder Specifications
- **Stream Sanitization:** Ignores non-base64 characters (CR, LF, spaces, and trailing terminal escape remnants).
- **Early Stop:** Stops at the first `=` padding boundary.
- **Bit Reservoir:** Fixed 14-bit max reservoir (`uint32_t buf`) draining 8 bits immediately once accumulated.

---

## 4. Transport & Low-Level I/O

### 4.1 Write Channel: Direct `/dev/tty`
- **Issue:** Writing escape sequences to `stdout` corrupts data when output is piped, redirected, or buffered by terminal libraries (e.g., Notcurses rendering engine).
- **Resolution:** Open `/dev/tty` directly with `O_WRONLY | O_NOCTTY`. Flush immediately via iterative `write()` calls to handle non-blocking partial write returns. Fallback to `printf` + `fflush(stdout)` on Windows.

### 4.2 Read Channel: Non-blocking Polled Query
- **Descriptor:** Open `/dev/tty` in non-blocking mode (`O_RDWR | O_NOCTTY | O_NONBLOCK`).
- **Initial Timeout:** `poll(&pfd, 1, 300)` (300 ms deadline for initial terminal response).
- **Chunk Assembly Loop:** 
  - Read buffer: 64 KB (`char buf[65536]`).
  - Terminal response chunks over SSH or congested networks are concatenated until terminator `\007` or `\033\\` is encountered.
  - Inter-chunk poll: 100 ms max wait.
  - Overall hard deadline: 3 seconds max to prevent UI freezes.

---

## 5. Hybrid Fallback & Resolution Hierarchy

### 5.1 Read Pipeline (`get_system_clipboard`)

```text
[Get Clipboard Request]
       |
       v
Local Desktop Environment? ---> YES ---> [Run Desktop Tools]
       |                                   macOS:   pbpaste
       NO                                  Win32:   GetClipboardData
       |                                   Wayland: wl-paste
       v                                   X11:     xclip -> xsel
SSH Session Active? (SSH_CLIENT/SSH_TTY)
       |
      YES ---> [Query OSC 52 via /dev/tty]
       |         Valid response? ---> RETURN
       NO
       |
       v
[Fallback to Internal Register (yank_reg)]
```

### 5.2 Write Pipeline (`copy_to_system_clipboard`)
1. **OSC 52 Broadcast:** Transmit formatted OSC 52 sequence via `/dev/tty` (guarantees clipboard sync on host terminal emulator even over multiple SSH hops).
2. **Local Desktop Mirror:** Execute local clipboard tool in background (`pbcopy`, `wl-copy`, `xclip`, or native Windows API `SetClipboardData`).

---

## 6. Bracketed Paste & Burst Suppression

To prevent large pastes from running hundreds of undo passes, auto-indent loops, and screen rerenders:

1. **Terminal Mode DEC 2004:**
   - On initialization: `printf("\033[?2004h"); fflush(stdout);`
   - On termination: `printf("\033[?2004l"); fflush(stdout);`
2. **`handle_bracketed_paste_fast()`:**
   - Detects leading `\033[200~` marker.
   - Flushes stream in-memory without invoking syntax highlight or layout loops until trailing `\033[201~`.
3. **`handle_paste_burst()`:**
   - Catches input batches arriving faster than physical typing speed ($\le 2\,\text{ms}$ interval for first keystroke, $\le 30\,\text{ms}$ for continuous burst).
   - Bundles stream into `paste_text_raw()`, inserting text in a single undo transaction with one atomic syntax update.

---

## 7. Editor Interface Mapping

| Key Chord | Action ID | Scope | Semantics |
| :--- | :--- | :--- | :--- |
| `<Space> y` | `copy_clipboard` | Normal / Visual | Copies current line or visual selection to OS clipboard via OSC 52. |
| `<Space> p v` | `paste_clipboard` | Normal / Visual | Pastes clipboard contents at cursor position or replaces selection. |
| `<Space> p p` | `full_replace_paste`| Normal / Visual | Full buffer replace from system clipboard with cursor reset. |
| `<C-S-v>` | `bracket_paste` | Global | Universal desktop bracketed paste across Normal, Insert, and Command modes. |
| `:pv` | `paste_clipboard` | Command | Command alias to paste clipboard at cursor. |
| `:pp` | `full_replace_paste`| Command | Command alias to replace entire buffer from clipboard. |

---

## 8. Terminal Compatibility Matrix

| Terminal Emulator | OSC 52 Write | OSC 52 Read | Notes / Configuration Required |
| :--- | :---: | :---: | :--- |
| **Kitty** | Full | Prompt / Auth | Write is allowed by default; read requires `clipboard_control write-clipboard read-clipboard` in `kitty.conf`. `fe` avoids prompts locally by using desktop tools first. |
| **WezTerm** | Full | Full | Enabled out of the box for both directions. |
| **iTerm2** | Full | Full | Requires *"Applications in terminal may access clipboard"* checked in Settings -> General -> Selection. |
| **Windows Terminal** | Full | Full | Native support out of the box. |
| **Tmux (inner)** | Full | Full | Supported using DCS passthrough wrapper `\033Ptmux;...`. Requires `set -s set-clipboard on` in `.tmux.conf`. |
