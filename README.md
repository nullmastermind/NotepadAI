# NotepadAI

You shouldn't have to leave your editor to talk to an AI, run git, or open a shell. NotepadAI is a Notepad++-style code editor that has those things built in, not bolted on the side. It's a fork of [Notepad Next](https://github.com/dail8859/NotepadNext) (itself a Notepad++ reimplementation in Qt/C++), and it stays light and fast while doing a lot more.

![screenshot](/screenshot.png)

## Features

### AI agents

NotepadAI speaks the Agent Client Protocol (ACP) over stdio. Claude Code and Codex are built in. Add any other ACP-compatible agent — Gemini, Auggie, or your own command — from Settings. Agents read and write files, run terminal commands, and see your workspace context, so they work on the same code you do.

There's also a separate Goal Agent built in. It isn't ACP: you describe an intent and it plans and executes the steps itself. And when you commit, AI can write the commit message from your staged diff. The protocol details and how to wire up a custom agent are in [doc/AcpAgents.md](doc/AcpAgents.md).

### Git, built in

Inline blame, gutter diff markers, commit history, staging and unstaging, a branch picker, and git status decorations in the file tree. Merge and rebase flows are here too, including an interactive-rebase editor and a 3-way conflict viewer. Most operations never shell out to `git` — it diffs with xdiff and parses status, log, and blame itself. You don't need a separate Git GUI.

### A real terminal

A full PTY terminal built on libvterm and libptyqt, with mouse reporting and a scrollback buffer. It reads your project files — Justfile, Makefile, package.json, deno.json — figures out which tasks you can run, and draws clickable run icons in the editor margin. Open a terminal rooted at the active workspace or at the current file's folder.

### The editor itself

It's still Notepad++ at heart: a tabbed, splittable interface (Qt Advanced Docking System) with syntax highlighting for 80+ languages through vendored Scintilla and Lexilla. Macro recording and playback, session management, and an embedded Lua scripting layer are all here. If you're coming from Notepad++, it imports your config and sessions. There's also an editor minimap, live preview for Markdown and HTML, and a find-in-folder search that scans workspace directories with regex support.

### Extras worth knowing

You can connect to a remote machine over SSH and work on it like a local folder — the file tree, terminal, git, and AI agents all route through the connection. Transfers happen over SFTP with conflict detection and a progress UI.

CSV and TSV files open in a sortable, filterable spreadsheet preview that handles large files without loading them entirely into memory.

You can define mini-apps — small HTML/JS tools that run in a native WebView inside the editor. Scheduled tasks fire AI agent sessions on a cron schedule. And you can keep several folder-as-workspace roots open at the same time.

## Installation

Download from the [Releases](https://github.com/nullmastermind/NotepadAI/releases) page.

### Windows

Download the installer (.exe) or the portable zip. The installer registers file associations and adds a Start Menu shortcut. The portable zip runs from any folder with no install step.

### macOS

Download the disk image (.dmg), open it, and drag NotepadAI to Applications.

### Linux

#### AppImage (all distributions)

AppImage runs on any x86_64 Linux distribution without installation.

Requirements: FUSE 2 (`libfuse2`). Ubuntu 22.04+ and Fedora 38+ include it. On Ubuntu 24.04:

```bash
sudo apt-get install libfuse2
```

Run:

```bash
chmod +x NotepadAI-*.AppImage
./NotepadAI-*.AppImage
```

To integrate with your desktop, move to `~/.local/bin/` and use a tool like `appimaged`.

#### DEB package (Ubuntu 24.04+ / Debian 12+)

```bash
sudo apt install ./NotepadAI-*-amd64.deb
```

Uninstall: `sudo apt remove notepadai`

#### RPM package (Fedora 38+ / openSUSE Tumbleweed)

Fedora:

```bash
sudo dnf install ./NotepadAI-*-x86_64.rpm
```

openSUSE:

```bash
sudo zypper install ./NotepadAI-*-x86_64.rpm
```

Uninstall: `sudo dnf remove notepadai` or `sudo zypper remove notepadai`

### Building from source

You need CMake 3.21+, Qt 6.5+, Ninja, and a C++20 compiler.

Ubuntu/Debian build deps:

```bash
sudo apt-get install cmake ninja-build \
  qt6-base-dev qt6-base-private-dev libqt6core5compat6-dev \
  libxkbcommon-dev libxcb-cursor-dev libcups2-dev libcurl4-openssl-dev
```

Fedora build deps:

```bash
sudo dnf install cmake ninja-build \
  qt6-qtbase-devel qt6-qt5compat-devel \
  libxkbcommon-devel libxcb-devel
```

Build:

```bash
git clone https://github.com/nullmastermind/NotepadAI.git
cd NotepadAI
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build --prefix /usr/local
```

For packaging details see [doc/Building.md](doc/Building.md).

## Multi-instance and portable mode

All app data lives under `<data-dir>/NotepadAI/` — the settings INI, session backups, and ACP chat history. You can point that somewhere else. Highest priority first:

- CLI flag: `NotepadAI.exe --data-dir=D:/profiles/work`
- Environment variable: `NOTEPADAI_DATA_DIR=D:/profiles/work`
- Portable marker: an empty file named `portable` next to the exe
- Preferences UI: Settings > Data Directory > Browse

Relative paths resolve against the executable's directory. Two instances with different data dirs are fully independent — separate settings, sessions, window state, and SingleApplication identity. Two instances pointed at the same data dir act as one: the second forwards its files to the first and exits. The `portable` marker keeps everything next to the exe and writes nothing to `%APPDATA%` or any system directory, which is what you want on a USB drive.

## License

[GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.txt). See the `LICENSE` file.

Based on Notepad Next by Justin Dailey. AI and Git extensions by [nullmastermind](https://github.com/nullmastermind).
