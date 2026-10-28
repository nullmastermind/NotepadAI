---
name: notepadai-control
description: Control the NotepadAI code editor running on this machine through its Control API - open projects and files, edit text, start AI agent sessions and send them prompts, answer their questions, run terminal commands and tasks, use git, pm2, the browser, settings - with commands the user watches happen. Use when the user asks you to do something in NotepadAI.
---

# Controlling NotepadAI

NotepadAI is a desktop code editor with AI agent sessions, terminals, git tools, a browser and more. Its Control API is a set of **commands**: `files.open`, `editor.insert`, `ai.send`, `terminal.run`, `git.commit`... You call a command; NotepadAI does it with the same code its menus and buttons run, and **shows the result**: the tab, panel or chat it changed comes to the front and flashes, the status bar reads "Agent: <what you did>", and the step goes into an activity log the user reads. Read commands are silent.

You never look for buttons or click pixels. Pick the command, pass its arguments, read the JSON answer.

## Connection

`NOTEPADAI_URL` (the server's base URL) and `NOTEPADAI_TOKEN` came with the prompt that sent you here. Send the token on every request:

```sh
api() { curl -s -H "Authorization: Bearer $NOTEPADAI_TOKEN" "$@"; }
run() { api -H "Content-Type: application/json" -d "${2:-{\}}" "$NOTEPADAI_URL/commands/$1"; }

api "$NOTEPADAI_URL/instance"                       # what is open
run files.open '{"path":"src/main.cpp","line":42}'  # do something
api -G --data-urlencode "tab=3" "$NOTEPADAI_URL/commands/editor.read"   # read commands also answer GET
```

- Responses are JSON with `"ok": true|false`. A failure has `"error"`: what went wrong and what to do.
- `400`: an argument is wrong (unknown, missing, wrong type, a value not allowed); the error lists what the command takes.
- `401`: the token is wrong or was regenerated. Ask the user for a new connection prompt; never guess.
- `403`: the `Host` must be the address in `NOTEPADAI_URL`. Never send an `Origin` header.
- `404`: no such thing (command, tab, session, file). The error says which list command shows what exists.
- `405`: a command that changes something was sent as GET; POST it.
- `409`: not now, or **an answer is missing**. When a step needs a decision a user would make in a dialog ("Save changes?", "A command is still running", "The branches diverged"), the command does not open the dialog: it refuses with `409` and `"needs": "<argument>"`. Decide (or ask the user) and call again with that argument (`unsaved: "discard"`, `force: true`, `strategy: "rebase"`...).
- `422`: it was tried and failed (the error has the reason). `504`: it took too long.
- `dismissedDialogs` in an answer: NotepadAI opened a dialog of its own while the command ran (e.g. "data.csv has been modified by another program. Reload it?" when the tab a command brought forward changed on disk). It was cancelled, which changes nothing, so the command could answer. Act on it with a command if needed (`files.reload`).
- Never write the token into a file, a commit or this skill.
- `GET /skill/version` returns `skillVersion`. Fetch `/skill` again when it is higher than the one below.

Skill version: 4 (API version 2).

## Endpoints

| Method | Path | What |
|---|---|---|
| GET | `/instance` | Version, window title, open workspaces, active workspace, tab count, current editor, number of commands |
| GET | `/commands[?domain=files]` | Every command (or one domain) with its arguments: name, type, required, allowed values, description |
| POST | `/commands/<name>` | Run a command; the JSON body holds its arguments (`{}` for none) |
| GET | `/commands/<name>?arg=value` | Run a read command (one that changes nothing); arguments in the query string |
| POST | `/wait` | Block until an event arrives or a read command's answer matches (see Waiting) |
| GET | `/events` | Server-sent events (see Events) |
| GET | `/screenshot[?maxWidth=1600]` | PNG of the main window (or the dialog open on top of it) |
| GET | `/log?limit=N` | The activity log: `time`, `op` (the command), `label`, `ok`, `detail` (why it failed), `args` (secret arguments masked) |
| GET | `/skill`, `/skill/version` | This skill (with the command reference generated from the running app), its version |

## Things and their ids

- **Tabs** (editors, diffs, terminals, previews, browsers) have a number: `tab`. `tabs.list` lists them with `kind`, `title`, `path`, `modified`, `current`. A tab keeps its number while it is open. Most editor and terminal commands default to the tab in front, but pass `tab` once you know it: the user may click elsewhere.
- **Workspaces** (project folders) are named by their root path (`workspace.list`); an SSH one by `ssh://<profile id>/<remote path>`. Commands that take `workspace` default to the active one.
- **Paths**: absolute, or relative to the active workspace. Forward slashes work everywhere.
- **AI sessions** have an id: `session` (`ai.sessions`). Without it a command uses the session in front of the AI panel.
- **Editor positions** are 1-based `line` and `column`; columns count characters.
- **Settings** have keys (`settings.list`). **Menu actions** have ids (`menu.list`), for the rare thing no command covers.

## Waiting

`POST /wait` answers when the condition holds (`ok: true`, `waitedMs`) or with `408` after `timeout` seconds (default 30, max 600):

- `{"command": "<read command>", "args": {...}, "until": {"<field>": <value> | [<values>]}, "contains": {"<field>": "<text>"}, "timeout": 300}`: runs the read command every 100 ms until each `until` field equals the value (or one of the list) and each `contains` field's text contains the string. The answer has `result`, the matching answer; a `408` has `last`, the last one.
- `{"event": "<name or prefix*>", "match": {"<field>": <value>}}`: the next matching event.

```sh
# until the agent's turn ends or it asks something
api -d '{"command":"ai.state","args":{"session":"<id>"},"until":{"settled":true},"timeout":600}' "$NOTEPADAI_URL/wait"
# until a terminal prints a line
api -d '{"command":"terminal.read","args":{"tab":7},"contains":{"text":"passing"},"timeout":300}' "$NOTEPADAI_URL/wait"
# until a task terminal finished
api -d '{"command":"terminal.state","args":{"tab":7},"until":{"taskState":["succeeded","failed"]},"timeout":600}' "$NOTEPADAI_URL/wait"
```

## Events

`GET /events` streams `event: <name>` / `data: <json>` lines (each `data` has `seq` and `time`):

- `chat.turnStarted`, `chat.turnEnded`, `chat.waitingForUser` (`session`)
- `terminal.taskExited`, `terminal.exited` (`tab`, `exitCode`)
- `action`: each command an external agent ran (`op`, `label`, `ok`, `args`)

## Recipes

Open a project and a file in it:
```sh
run workspace.open '{"path":"C:/src/app"}'
run files.open '{"path":"src/main.cpp","line":42}'      # answers {"tab": 3, "kind": "editor", ...}
```

Edit a file and save it:
```sh
run editor.read '{"tab":3,"line":30,"endLine":60}'
run editor.replaceRange '{"tab":3,"line":42,"endLine":43,"text":"    return parse(input);\n"}'
run editor.replaceAll '{"tab":3,"text":"oldName","replacement":"newName","wholeWord":true}'
run files.save '{"tab":3}'
```
`editor.insert` adds text at a position, `editor.setText` replaces everything, `editor.find` / `editor.findAll` search, `editor.undo` undoes. A new file: `files.new` then `files.save {"path": ...}`, or `workspace.createFile`.

Ask an AI agent to do something and get its answer:
1. `run ai.agents` lists the agents. `run ai.open '{"agent":"<id>","prompt":"Fix the failing test in parser.cpp"}'` opens a chat in the active workspace and sends the prompt. It answers with the `session` id.
2. Wait until it is done or needs someone: `POST /wait {"command":"ai.state","args":{"session":"<id>"},"until":{"settled":true},"timeout":600}`. `settled` is true once the turn has ended (`working` false: nothing running or queued) or the session waits for an answer (`waitingForUser`). Repeat the wait on a 408; a long task can take many minutes.
3. `run ai.report '{"session":"<id>"}'` is its final answer. `ai.read` shows the whole transcript (prompts, replies, tool calls).
4. When `ai.state` shows `waitingForUser`:
   - a `permission` (what the agent wants to run, with `options`): `run ai.permission '{"session":"<id>","option":"allow_once"}'`, only for what the user allowed you to; otherwise ask the user.
   - a question `form` (`questions` with `id`, `type`, `options`, `recommended`): `run ai.answer '{"session":"<id>","answers":[{"id":"Q1","value":"b"},{"id":"Q2","value":["x","y"]},{"id":"Q3","value":"accept"}]}'`. Answer what the user told you; otherwise ask them. `ai.dismissForm` closes it unanswered.
   Then wait again.
5. More prompts: `ai.send`. Stop a turn: `ai.cancel`. Model, mode, effort: `ai.set` (choices in `ai.state`).

Run a goal (the agent keeps working until each criterion is met):
```sh
run goal.options
run goal.start '{"session":"<id>","prompt":"Add CSV export","criteria":["Export button saves a valid CSV","Tests pass"]}'
api -d '{"command":"goal.state","args":{"session":"<id>"},"until":{"goal":["achieved","cancelled","failed"]},"timeout":600}' "$NOTEPADAI_URL/wait"
```

Run a command and get its result:
```sh
run terminal.run '{"command":"npm test"}'       # a task terminal: answers {"tab": 7, ...}
api -d '{"command":"terminal.state","args":{"tab":7},"until":{"taskState":["succeeded","failed"]},"timeout":600}' "$NOTEPADAI_URL/wait"
run terminal.read '{"tab":7,"lines":80}'
```
An interactive shell: `terminal.open`, then `terminal.send {"text":"ls\n"}` and `terminal.keys {"keys":["Ctrl+C"]}`. Saved tasks: `tasks.list`, `tasks.run`.

Commit and push:
```sh
run git.status
run git.stage '{"paths":["src/parser.cpp"]}'
run git.commit '{"message":"Fix parser on empty input"}'
run git.push
```
With nothing staged, `git.commit` stages and commits every change, as the Commit button does; stage first to commit only some files. `git.pull` refuses diverged branches until you pass `strategy` (`merge` / `rebase`); `git.switch` refuses a dirty tree until you pass `dirty` (`stash` / `force`). Merge and rebase conflicts: `git.status` lists them, `git.resolve` settles them, `git.operation` continues or aborts.

Open a web page and read it: `run browser.open '{"url":"https://example.com"}'`, then `browser.read {"as":"text"}`; `browser.eval` runs JavaScript in it.

Change a setting: `run settings.list` (keys, types, values), `run settings.set '{"key":"showWhitespace","value":true}'`.

Edit a CSV as a table: a `.csv` opens as text, as for the user; its table is its preview.
```sh
run files.open '{"path":"data.csv"}'                 # {"tab": 5, "kind": "editor"}
run preview.open '{"tab":5}'                         # {"tab": 6, "kind": "preview"}
api -d '{"command":"csv.state","args":{"tab":6},"until":{"loading":false}}' "$NOTEPADAI_URL/wait"
run csv.read '{"tab":6}'
run csv.setCell '{"tab":6,"row":0,"column":1,"value":"37"}'
run csv.save '{"tab":6}'
```
`csv.insertRows` / `deleteRows` / `insertColumns` / `deleteColumns` / `renameColumn` / `clearCells` change the table; `csv.sort` and `csv.filter` change only the view. The text editor of the same file then asks to reload: `files.reload` its tab.

Binary files open in a hex view (`kind: "preview"`): `hex.read {"tab","offset","length"}`, `hex.find {"bytes":"4D 5A"} or {"text"}`, `hex.goto {"offset"}`.

Editor extras: `editor.mark` highlights every match (`bookmark: true` also bookmarks their lines), `editor.clearMarks`; `editor.columnInsert` types text or a number sequence down a column (Edit > Column Editor); `editor.styles` / `editor.setStyle` read and change the lexer's colours and fonts for one editor (not saved). `files.print` prints an editor, or writes it as a PDF with `pdf`; `files.printers` lists printers.

Macros: `macro.record {"action":"start"}`, edit with commands, `macro.record {"action":"stop"}`, `macro.save {"name"}`, `macro.run {"name","times"}`. Edit a saved one step by step: `macro.steps`, `macro.actions` (the step kinds), `macro.insertStep` / `deleteStep` / `moveStep`, `macro.rename` / `copy` / `delete`.

Files between places:
- `workspace.zip {"path","to"}` and `workspace.unzip {"zip","into"}`: an unzip over existing files refuses with `needs: "conflicts"` (and lists them) until you pass `conflicts`: `overwrite` / `skip`.
- `workspace.share {"path"}` sends a workspace folder with croc and answers the code-phrase to give the other side; `workspace.receive {"code","into"}` receives one.
- SSH: `ssh.profiles`, `ssh.addProfile {"host","username","auth","secret"}` (the secret goes to the keychain, never back to you), `ssh.connect {"profile"}`. A host key never seen before refuses with `needs: "trustHostKey"` and its `fingerprint`: show it to the user, and only when they confirm it pass `trustHostKey: "<fingerprint>"`. Then `workspace.open {"path":"ssh://<profile>/<dir>"}`, `ssh.listDir`, `workspace.upload` / `workspace.download`. `ssh.debugLog` when a connection fails.

AI agent setup: `ai.addAgent {"name","command","args","env"}` / `ai.updateAgent` / `ai.cloneAgent` / `ai.removeAgent` / `ai.moveAgent` / `ai.setDefaultAgent` edit the agent list (env values are never shown back, only `hasEnv`). In a session: `ai.draft` puts text in the composer without sending (`@path` mentions a file), `ai.send {"images":[...]}` attaches images, `ai.queue` / `ai.unqueue` show and remove queued prompts, `ai.retry` restarts an agent that exited, `ai.debugLog` reads the agent's protocol log. Goal settings: `goal.setOptions`, templates `goal.template` / `saveTemplate`, criteria presets `goal.presets` / `savePreset`.

Another app's window in a tab (Windows): `embed.list`, then `embed.window {"handle"}`; `tabs.close` gives it back.

Something no command covers: `run menu.list` lists the menu actions; `run menu.run '{"id":"actionX"}'` runs one. An action that would open a dialog is refused and names the command that does its job.

## Rules

- Do only what the user asked for. Every change shows on their screen and goes into the log.
- Read first (`tabs.list`, `git.status`, `ai.state`...), then change, one step at a time; check each answer.
- When a command answers `409` with `needs`, the decision is the user's unless they already gave it to you. Ask them rather than guess, especially before discarding changes (`unsaved: "discard"`, `git.discard`), forcing (`force: true`, `git.push {force}`), deleting, or allowing an agent's permission request.
- Never use NotepadAI to get around a confirmation, a permission or a login the user has to give.
- If something needs the user (a login, a decision you were not given), stop and ask them.
