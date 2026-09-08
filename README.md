# NotepadAI

You shouldn't have to leave your editor to talk to an AI, run git, or open a shell. NotepadAI is a Notepad++-style code editor with those things inside the app. It is a fork of [Notepad Next](https://github.com/dail8859/NotepadNext) (a Notepad++ reimplementation in Qt/C++) and is meant to stay light and fast.

![screenshot](/screenshot.png)

## Features

### AI agents

NotepadAI speaks the Agent Client Protocol (ACP) over stdio. Claude Code and Codex are built in. From Settings you can add any other ACP-compatible agent, including Gemini, Auggie, or your own command. Agents read and write files, run terminal commands, and see your workspace context, so they work on the same code you do. Start a session from the AI menu or by right-clicking a folder in the file tree.

There is also a Goal Agent: pick an ACP judge (Claude Code, Codex, Opencode) or a Custom API endpoint that speaks Anthropic's Messages API. You set success criteria and it drives the working agent until those criteria hold. When you commit, AI can write the commit message from your staged diff. That generator (and other LLM calls) can talk to an OpenAI-compatible endpoint or Anthropic's Messages API. Protocol details and how to wire up a custom ACP agent are in [doc/AcpAgents.md](doc/AcpAgents.md).

### Git

Inline blame, gutter diff markers, commit history, staging and unstaging, a branch picker, and git status decorations in the file tree. Merge and rebase flows are included, with an interactive-rebase editor and a 3-way conflict viewer. Most operations never shell out to `git`: it diffs with xdiff and parses status, log, and blame itself.

### Terminal

A PTY terminal built on libvterm and libptyqt, with mouse reporting and a scrollback buffer. New terminals open as a bottom tab under the editor. It reads Justfile, Makefile, package.json, and deno.json, finds which tasks you can run, and draws clickable run icons in the editor margin. Open a terminal at the active workspace or at the current file's folder.

### Editor

A tabbed, splittable interface (Qt Advanced Docking System) with syntax highlighting for 80+ languages through vendored Scintilla and Lexilla. Right-click a panel tab to close it or its neighbors. Macro recording and playback, session management, and an embedded Lua scripting layer. If you are coming from Notepad++, it imports your config and sessions. There is also an editor minimap, live preview for Markdown and HTML, and a find-in-folder search that scans workspace directories with regex support.

### Other

You can connect to a remote machine over SSH and work on it as a local folder. The file tree, terminal, git, and AI agents all route through the connection. Transfers happen over SFTP with conflict detection and a progress UI.

Right-click a folder in the file tree to pack it as a zip (honors .gitignore, skips .git and node_modules) or extract a zip into it, locally or over SSH. Extracting also removes files that are no longer in the archive, so the folder stays in sync with the zip. You can also share that zip with a one-shot encrypted code-phrase via [croc](https://github.com/schollz/croc), or receive one the same way.

CSV and TSV files open in a sortable, filterable spreadsheet preview that handles large files without loading them entirely into memory.

You can define mini-apps (small HTML/JS tools that run in a native WebView inside the editor), fire AI agent sessions on a cron schedule, and keep several folder-as-workspace roots open at the same time.

## Installation

Grab a binary from the [Releases](https://github.com/nullmastermind/NotepadAI/releases) page.

| Platform | Format |
|----------|--------|
| Windows  | Installer (.exe) or portable zip |
| Linux    | AppImage |
| macOS    | Disk image (.dmg) |

## Building from source

You need CMake 3.21+, Qt 6.5+, Ninja, and a C++20 compiler (MSVC, clang-cl, GCC, or Clang).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For platform-specific details and packaging on Windows, macOS, and Linux, see [doc/Building.md](doc/Building.md).

## Multi-instance and portable mode

All app data lives under `<data-dir>/NotepadAI/`: the settings INI, session backups, and ACP chat history. You can point that somewhere else. Highest priority first:

- CLI flag: `NotepadAI.exe --data-dir=D:/profiles/work`
- Environment variable: `NOTEPADAI_DATA_DIR=D:/profiles/work`
- Portable marker: an empty file named `portable` next to the exe
- Preferences UI: Settings > Data Directory > Browse

Relative paths resolve against the executable's directory. Two instances with different data dirs are fully independent: separate settings, sessions, window state, and SingleApplication identity. Two instances pointed at the same data dir act as one: the second forwards its files to the first and exits. The `portable` marker keeps everything next to the exe and writes nothing to `%APPDATA%` or any system directory, which is useful on a USB drive.

## License

[GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.txt). See the `LICENSE` file.

Based on Notepad Next by Justin Dailey. AI and Git extensions by [nullmastermind](https://github.com/nullmastermind).
