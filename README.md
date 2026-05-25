# NotepadAI

A fast, cross-platform code editor with built-in AI agents, Git integration, and an embedded terminal. Built on [Notepad Next](https://github.com/dail8859/NotepadNext) (a Notepad++ reimplementation in Qt/C++).

![screenshot](/doc/screenshot.png)

## Features

- **AI Agent Integration** — Chat with Claude Code, Gemini, or any ACP-compatible agent directly inside the editor. Agents can read/write files, run terminal commands, and understand your workspace context.
- **Git Integration** — Inline blame, gutter diff indicators, commit history viewer, staging/unstaging, branch picker, and file-tree decorations. No external Git GUI needed.
- **Embedded Terminal** — Full PTY terminal with mouse reporting, image passthrough, and persistent task registry. Open terminals in workspace or file directory.
- **Notepad++ Compatibility** — Familiar tabbed interface, syntax highlighting for 80+ languages via Scintilla/Lexilla, macro recording, session management, and Notepad++ config import.
- **Lightweight & Fast** — Native C++20/Qt 6 with zero-copy hot paths, precompiled headers, and Control Flow Guard. Starts in milliseconds.
- **Cross-platform** — Windows, Linux (AppImage), and macOS (dmg).

## Installation

Download binaries from the [Releases](https://github.com/nullmastermind/NotepadAI/releases) page.

| Platform | Format |
|----------|--------|
| Windows  | Installer (.exe) or portable zip |
| Linux    | AppImage |
| macOS    | Disk image (.dmg) |

## Building from Source

Requirements: CMake 3.21+, Qt 6.5+, Ninja, a C++20 compiler (MSVC, GCC, or Clang).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

See [doc/Building.md](doc/Building.md) for platform-specific details.

## AI Agents

NotepadAI speaks the [Agent Client Protocol (ACP)](doc/AcpAgents.md) over stdio. The default built-in agent is Claude Code. Add custom agents via Settings with any command that implements ACP's JSON-RPC 2.0 wire format.

## License

[GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.txt)

Based on Notepad Next by Justin Dailey. AI and Git extensions by [nullmastermind](https://github.com/nullmastermind).
