# NotepadAI

You shouldn't have to leave your editor to talk to an AI, run git, or open a shell. NotepadAI is a Notepad++-style code editor with those things inside the app. It is a fork of [Notepad Next](https://github.com/dail8859/NotepadNext) (a Notepad++ reimplementation in Qt/C++) and is meant to stay light and fast.

![screenshot](/screenshot.png)

## Features

### AI agents

NotepadAI speaks the Agent Client Protocol (ACP) over stdio. Claude Code and Codex ship as factory agents and can be edited or deleted in Settings. You can also add any other ACP-compatible agent, including Gemini, Auggie, or your own command. Agents read and write files, run terminal commands, and see your workspace context, so they work on the same code you do. Start a session from the AI menu or by right-clicking a folder in the file tree.

In a chat, a finished turn folds into a single "Worked for 2m14s" row; click it to bring back the tool calls and intermediate messages. Tool calls show how long they took. Claude Code background tasks and the wake-ups or cron jobs it schedules for itself appear under the transcript, with a Stop link for tasks that can be stopped. When the agent offers `/advisor`, an Advisor checkbox in the composer turns it on and off. For Claude Code, Prewalk (at the top of the model picker) switches to a cheaper model once the agent starts editing files and back when the turn ends. When an agent needs a decision, its questions appear as a form in the chat with the suggested answer preselected, and a note box at the end takes anything else you want to add.

There is also a Goal Agent: pick an ACP judge (Claude Code, Codex, Opencode) or a Custom API endpoint that speaks Anthropic's Messages API. You set success criteria and it drives the working agent until those criteria hold, or attach a goal to an existing conversation. Criteria presets can be saved globally, or into the current project's `.agents/.goals` folder so they travel with the repo; files already in that folder show up in the load list. A judge that needs a human action, or that hits the iteration cap with the criterion still unmet, stops and hands the criterion back instead of marking the goal done. With Prefix /goal checked, the first criterion is sent immediately when the agent supports `/goal`. If a reply is still running and the agent does not support `/goal`, that goal waits in the send queue so the current reply is not stopped. Later criteria wait until the previous is achieved. Send with Goal defaults to the agent's own `/goal` command instead: each criterion goes out on its own, with no separate judge. A `/goal` typed in the composer is sent while a turn is still running only when the agent supports that command; otherwise it waits in the queue. A goal is not judged, and auto compact does not run, while the agent still has background tasks running or a wake-up turn pending. When a goal is achieved, the session can compact automatically. Prompts sent while an agent is still working are queued and delivered when it finishes. AI chats group by project, with a session strip to switch conversations in the same dock. Open chats come back after a restart and resume their agent session on the first prompt. A local Claude Code chat can be moved to another agent from the agent button in the dock's title bar, keeping its transcript. The composer can ask the agent to work in a new git worktree. When you commit, AI can write the commit message from your staged diff. That generator (and other LLM calls) can talk to an OpenAI-compatible endpoint or Anthropic's Messages API. Leave the commit box empty (when not amending) and Commit lets you pick an ACP agent to do the commit. Protocol details and how to wire up a custom ACP agent are in [doc/AcpAgents.md](doc/AcpAgents.md).

An outside AI agent can also drive NotepadAI itself. AI ▸ Connect External Agent turns on a local HTTP API (off by default, token-protected) and gives you a prompt to hand to the agent; it fetches a skill from the app describing how to use it. The agent works only through the controls you see: menus, buttons, fields, tabs, editors, terminals and chats, each step highlighted on screen and listed in an activity log, with a Stop button in the status bar. Workspaces, terminals and tasks, AI sessions, goals, git, settings and pane sizes are all reachable this way.

### Git

Inline blame, gutter diff markers, commit history, staging and unstaging (including bulk actions on a status section), a branch picker, and git status decorations in the file tree. Linked worktrees show up in the repository list; you can remove one, or merge its branch into the main checkout and then remove it. Merge and rebase flows are included, with an interactive-rebase editor and a 3-way conflict viewer. Diffs and commits open as editor tabs in a GitHub-style view with a file tree, collapsible files, word-level highlights, and syntax colouring. Most operations never shell out to `git`: it diffs with xdiff and parses status, log, and blame itself.

### Terminal

A PTY terminal built on libvterm and libptyqt, with mouse reporting and a scrollback buffer. New terminals open as a bottom tab under the editor. When a workspace opens with no terminal, that area shows a placeholder with a New terminal button and the workspace's configured tasks; close it to hide it for that workspace, or turn it off under Preferences → Terminal. The tab strip follows the raised workspace: only terminals whose working directory belongs to that folder (or a nested workspace, longest match) are shown. If any open terminal sits outside every workspace, the filter turns off and every tab stays visible. Closing a workspace closes its terminals. Programs launched from a local terminal share a memory ceiling (16 GB by default, changeable under Preferences → Terminal) so a runaway process is stopped instead of exhausting the machine. A local terminal's title bar shows that process tree's CPU and RAM; hover the CPU figure for each logical core's load. Remote terminals omit those figures. It reads Justfile, Makefile, package.json, and deno.json, finds which tasks you can run, and draws clickable run icons in the editor margin. Open a terminal at the active workspace or at the current file's folder.

Terminal → PM2 Processes lists every process of your local pm2 with live status, uptime, CPU and memory. You can start, stop, restart, or delete them, follow their logs in a terminal tab, and run a new command under pm2 with its own directory, shell, and environment variables. pm2 is fetched through npx if it is not installed.


### Editor

A tabbed, splittable interface (Qt Advanced Docking System) with syntax highlighting for 80+ languages through vendored Scintilla and Lexilla. Right-click a panel tab to close it or its neighbors. Macro recording and playback, session management, and an embedded Lua scripting layer. If you are coming from Notepad++, it imports your config and sessions. There is also an editor minimap, live preview for Markdown and HTML, and a find-in-folder search that scans workspace directories with regex support.

### Other

You can connect to a remote machine over SSH and work on it as a local folder. The file tree, terminal, git, and AI agents all route through the connection. Transfers happen over SFTP with conflict detection and a progress UI.

Right-click a folder in the file tree to pack it as a zip (honors .gitignore, skips .git and node_modules) or extract a zip into it, locally or over SSH. Extracting also removes files that are no longer in the archive, so the folder stays in sync with the zip. You can also share that zip with a one-shot encrypted code-phrase via [croc](https://github.com/schollz/croc), or receive one the same way.

CSV and TSV files open in a sortable, filterable spreadsheet preview that handles large files without loading them entirely into memory.

Images, game textures, 3D models, tile maps, texture atlases, audio, video, PDF, fonts, and other binary files open in read-only preview tabs instead of as garbled text. Audio, video, and PDF load from a separate plugin only when you first open one.

You can define mini-apps (small HTML/JS tools that run in a native WebView inside the editor) and nest them in menu groups, pin Quick Browse and mini-app tabs (favicon only, like Chrome), switch those tabs between Fit, Mobile, Tablet, and PC widths, fire AI agent sessions on a cron schedule, and keep several folder-as-workspace roots open at the same time. A fuzzy picker opens a recent workspace. Another application's window can be embedded in a tab. A link that would open a new browser window opens as another tab in that same mini-app or Quick Browse view.

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
