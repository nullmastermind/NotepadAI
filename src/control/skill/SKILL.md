---
name: notepadai-control
description: Operate the NotepadAI code editor running on this machine through its Control API (open projects and files, start AI agent sessions and send them prompts, run terminal commands, open the browser, change settings) the way its user would. Use when the user asks you to do something in NotepadAI.
---

# Operating NotepadAI

NotepadAI is a desktop code editor with AI agent sessions, terminals, git tools and a browser. Its Control API lets you operate it **the way its user does**: every change goes through a menu item, button, field, tab or key the user can see. It flashes on screen and is written to an activity log the user reads. There are no hidden commands. Reading what is on screen is silent.

## Connection

`NOTEPADAI_URL` (the server's base URL) and `NOTEPADAI_TOKEN` came with the prompt that sent you here. Send the token on every request:

```sh
api() { curl -s -H "Authorization: Bearer $NOTEPADAI_TOKEN" "$@"; }
api "$NOTEPADAI_URL/instance"
api -H "Content-Type: application/json" -d '{"id":"actionAiAgents"}' "$NOTEPADAI_URL/actions/trigger"
api -G --data-urlencode "id=MainWindow/statusBar/labelDocPosition" "$NOTEPADAI_URL/ui/read"
```

- Ids contain `/`, `@`, `:` and spaces: send query values URL-encoded (`-G --data-urlencode`).

- Bodies are JSON objects. Responses are JSON with `"ok": true|false`; an error carries `"error"` with the reason and what to do.
- `401`: the token is wrong or was regenerated. Ask the user for a new connection prompt; never guess.
- `403`: the `Host` must be the address in `NOTEPADAI_URL`. Never send an `Origin` header.
- `404`: no such id; list again (`GET /ui`, `GET /actions`).
- `409`: a user could not do it right now (hidden, disabled, or a modal dialog is open). The error says which; deal with that first.
- Never write the token into a file, a commit or this skill.
- `GET /skill/version` returns `skillVersion`. Fetch `/skill` again when it is higher than the one below.

Skill version: 1 (API version 1).

## Ids

Everything is addressed by an **id**, never by the text shown (the UI may be translated).

- **Actions** (menu items, toolbar buttons): the object name, for example `actionNewAiTab`. Items of menus built on the fly are `<menu id>/<key>`, for example `menuOpenAiAgentInWorkspace/agent:<agent id>`, `menuLanguage/group:P/lang:Python`.
- **Widgets**: a path from a root, for example `MainWindow/mainToolBar/button:actionSave`.
  - Roots are windows and dialogs (`PreferencesDialog`), docked tabs (`dock:<uuid>`) and chat sessions (`chat:<session id>`).
  - Segments are object names. A toolbar button is `button:<its action>`. A dialog's standard button is `button:ok`, `button:cancel`, `button:yes`, `button:save`, `button:discard`...
  - An unnamed widget is `<Class>#<n>`.
- **Parts** of a widget (list rows, tabs): `<widget id>@<key>`, for example `.../treeView@path:C:/src/app/main.cpp`, `.../tabBar@tab:2`.
- Anything listed with `"stable": false` (no object name; id from text or position) works right after you list it. Never store it.

## Endpoints

| Method | Path | What |
|---|---|---|
| GET | `/instance` | App version, window title, workspaces, active workspace, active editor |
| GET | `/skill`, `/skill/version` | This skill, its version |
| GET | `/actions[?menu=<id>]` | Every action (or one menu): `id`, `label`, `menu` path, `shortcut`, `enabled`, `checked`, `location`, `value` (what a direct command takes). Menus filled on opening are filled first |
| POST | `/actions/trigger` | `{"id", "value"?}`: click the action |
| GET | `/ui[?root=<id>&depth=8&parts=50&hidden=1]` | The window tree: `id`, `role`, `label`, `value`/`checked`/`items`, `enabled`, `focus`, `parts`, `children`; also `modal`, `popup`, `focus` at the top |
| GET | `/ui/node?id=` | One node in detail (rect, tooltip, adapter summary) |
| GET | `/ui/read?id=[&from=&count=]` | The content: text, value, items, rows, transcript (see Widgets) |
| GET | `/ui/find?text=&role=&limit=` | Visible nodes whose label or text contains `text` |
| POST | `/ui/click` | `{"id", "button"?: "left"\|"right"\|"middle", "double"?, "value"?}` |
| POST | `/ui/type` | `{"id", "text", "replace"?}`: type into a field, editor, terminal |
| POST | `/ui/key` | `{"id"?, "keys": "Ctrl+S"}` or `"keys": ["Ctrl+K", "Ctrl+C"]`; no `id`: the focused widget |
| POST | `/ui/select` | `{"id", "index"\|"text"\|"data"}` (combo, tabs) or a part id (row, tab) |
| POST | `/ui/set` | `{"id", "value"}`: check box (true/false), spin box, combo, field, slider |
| POST | `/ui/scroll` | `{"id", "to": "top"\|"bottom"}` or `{"pages"\|"dy"\|"dx": n}`; a part id scrolls it into view |
| POST | `/ui/close` | `{"id"}`: a dialog, a tab (`dock:...`), a tab part |
| POST | `/ui/focus` | `{"id"}` |
| POST | `/ui/do` | `{"id", "op", "args"}`: a widget's own operation (see Widgets) |
| GET | `/events` | Server-sent events (see Events) |
| POST | `/wait` | Block until something happens (see Waiting) |
| GET | `/screenshot[?id=&maxWidth=1600]` | PNG of a window or widget (default: the open dialog, else the main window) |
| GET | `/log?limit=N` | The activity log |

### Commands and effects

Every command (`/actions/trigger`, `/ui/click`, `type`, `key`, ...) answers once its work is done, or as soon as it opened a modal dialog or a menu:

```json
{"ok": true, "completed": false, "action": "actionPreferences", "label": "Settings ▸ Preferences…",
 "effects": {"opened": [{"id": "PreferencesDialog", "kind": "dialog", "title": "Preferences"}],
             "closed": [], "focus": "...", "modal": "PreferencesDialog"}}
```

- `completed: false`: a dialog or menu is waiting for input (`effects.modal`, `effects.popup`). Answer it with `/ui/*` on its ids, or `POST /ui/close`.
- `effects.opened` / `effects.closed`: windows, dialogs, tabs (`kind: dock`), panels (`kind: panel`, the workspace tree) and chats that appeared or went away. Clicking a dialog's OK answers after the dialog has done its work, so the tab it opened is listed there.

### Direct commands (pickers)

A user picks files, folders, fonts and colours in a system picker you cannot operate. Pass the value instead: `{"id": "actionOpen", "value": "C:/src/app/main.cpp"}` (or a list: `"value": ["a.txt", "b.txt"]`). Works on `/actions/trigger` and `/ui/click` (a "Browse…" button). `GET /actions` shows which actions open one (`value`). Confirmations ("Replace it?", "Save changes?") still appear; answer them.

## Widgets

| Role | Read (`/ui/read`) | Do |
|---|---|---|
| `dock` (a tab: `kind` editor, preview, tool, panel) | its title, `open`, `current` | `/ui/select` brings it to front, `/ui/close` closes it (unsaved editors ask), `/ui/do op=float` |
| `editor` | lines `from` (1-based, default 1), `count` (default 500): `text`, `path`, `modified`, `language`, `line`/`column`, `selection`; part `@selection` | `/ui/type` (multi-line text is inserted like a paste), `/ui/set` replaces all, `/ui/do op=goto args={"line", "column"?}`, `op=select args={"line","column"?,"toLine"?,"toColumn"?}` or `{"all":true}`; `/ui/key` for shortcuts |
| `terminal` | `rows` (no `count`: the screen; `from` < 0 counts back), `cursor`, `cwd`, `running`, `exited`; part `@selection` | `/ui/type` types (a newline is Enter), `/ui/key` sends keys (`Ctrl+C`) |
| `chat` (an AI agent session) | `entries` (messages and tool calls, last 30; `from`/`count`), `working`, `queued`, `waitingForUser`, `draft`; part `@report`: the last finished answer | `/ui/do op=send args={"text"}` types the prompt and presses Send; its widgets: `chatInput`, `chatSend`, `chatCancel`, `chatModel`, `chatMode`, `chatEffort` |
| `list`/`tree`/`table` | `rows` with `key`, `text`, `cells`, `expanded`, `selected` | click or select a row part; `/ui/do op=expand`/`collapse` |
| `tabs` | `current`, tabs as parts | click a tab part; `/ui/close` a tab part |

A question form or permission prompt from an agent is made of ordinary widgets inside the chat: read it with `GET /ui?root=chat:<id>`, answer with `/ui/click` and `/ui/type` (ids under Recipes).

## Events

`GET /events` streams `event: <name>` / `data: <json>` lines (each `data` has `seq` and `time`):

- `window.opened`, `window.closed` (`id`, `kind`, `title`, `modal`); `dock.shown`, `dock.hidden` (`id`, `title`, `open`: false once closed, true when another tab of its area became current; `current`); `focus.changed`
- `chat.turnStarted`, `chat.turnEnded`, `chat.waitingForUser` (`id` = `chat:<session>`)
- `terminal.taskExited`, `terminal.exited` (`id`, `exitCode`)
- `action`: each step an external agent took (`op`, `id`, `label`, `ok`)

## Waiting

`POST /wait` answers when the condition holds (`ok: true`, `waitedMs`) or with `408` after `timeout` seconds (default 30, max 600):

- `{"event": "chat.turnEnded", "match": {"id": "chat:abc"}}`: the next matching event (`"event": "window.*"` matches a prefix).
- `{"id": "<node>", "state": "visible"|"hidden"|"exists"|"gone"|"enabled"|"disabled"|"idle"|"waitingForUser"}`. `idle` is a chat or other widget not working, with nothing queued and not waiting for the user. `"state"` may be a list: `["idle", "waitingForUser"]` answers when either holds, with `state` naming which.
- `{"id": "<node>", "text": "Done"}`: its `/ui/read` contains the text (a terminal printing a line, a label changing).

## Recipes

Open a project folder (shows it in the workspace tree):
```sh
api -H "Content-Type: application/json" -d '{"id":"actionOpenFolderasWorkspace","value":"C:/src/app"}' "$NOTEPADAI_URL/actions/trigger"
```

Open a file: `{"id":"actionOpen","value":"C:/src/app/main.cpp"}`. A new tab: `actionNew`. Save: `actionSave` (`actionSaveAs` with a `value`).

Open a file from the workspace tree, as the user would: `GET /ui/find?role=tree` gives the tree (`.../filesTab/treeView`); its rows are parts `@path:<absolute path>`. `POST /ui/do {"id":"<tree>@path:C:/src/app/src","op":"expand"}` opens a folder, `POST /ui/click {"id":"<tree>@path:C:/src/app/src/main.cpp","double":true}` opens the file.

Start an AI agent session in the active workspace and send it a prompt:
1. `GET /actions?menu=menuOpenAiAgentInWorkspace` lists the agents (`menuOpenAiAgentInWorkspace/agent:<id>`). Trigger one. `effects.opened` names the new chat (or read `GET /ui` for `chat:` roots).
2. `POST /ui/do {"id":"chat:<id>","op":"send","args":{"text":"Fix the failing test in parser.cpp"}}`
3. `POST /wait {"id":"chat:<id>","state":["idle","waitingForUser"],"timeout":600}`. On `idle`, `GET /ui/read?id=chat:<id>@report` is the answer.
4. On `waitingForUser` the agent asks something (`GET /ui?root=chat:<id>`):
   - A permission prompt: `chat:<id>/permissionPrompt` (the labels inside it say what for), with buttons named by kind: `allow_once`, `allow_always`, `reject_once`, `reject_always`. Click the one the user allowed you to; otherwise ask the user.
   - A question form: `chat:<id>/grillMeForm` (find its path with `GET /ui/find?text=Submit`). Each question is `q:<question id>` holding: `option:<value>/choice` (click to pick), `other/choice` + `other/text`, `answer` (a text, number or dropdown answer), `yes`/`no` (or `accept`/`change` + `changeText`), `clarify`, `note` + `noteText`. Then click the form's `submit` (or `dismiss`). Answer only what the user told you; otherwise ask them.
   Then wait again (step 3).

Run a command in a terminal:
1. Trigger `actionOpenTerminalInWorkspace`. `effects.opened` names its tab; the terminal is `dock:<uuid>/terminalDock/terminal` (or `GET /ui/find?role=terminal`).
2. `POST /ui/type {"id":"<terminal>","text":"npm test\n"}`
3. `POST /wait {"id":"<terminal>","text":"passing","timeout":300}` or poll `GET /ui/read?id=<terminal>`.

Commit with git: the workspace panel (role `dock`, the folder's name) has a Files and a Git tab (`.../tabs/tabBar@tab:1` is Git). Under `.../tabs/gitTab`: `treeChanges` (rows are the changed files), `buttonStage`, `buttonStageAll`, `buttonUnstage`, `buttonUnstageAll`, `editCommitMessage` (type the message), `checkAmend`, `buttonCommit`; `buttonBranch` opens the branch picker, `buttonPull` / `buttonPush`, `buttonHistory` the log. `GET /ui/find?text=Stage All` gives the full ids.

Open a web page: trigger `actionQuickBrowser` (a dialog opens), then `POST /ui/type {"id":"QuickBrowseDialog/editUrl","text":"https://example.com"}` and `POST /ui/click {"id":"QuickBrowseDialog/buttonBox/button:ok"}`.

Change a setting: trigger `actionPreferences`, `GET /ui?root=PreferencesDialog`, change fields with `/ui/set`, then click its `button:save` (the id is in the tree; `button:cancel` discards).

Edit text in the open file: `GET /ui/find?role=editor` → `POST /ui/do {"id":"<editor>","op":"goto","args":{"line":42}}` → `POST /ui/type {"id":"<editor>","text":"// TODO\n"}`.

## Rules

- Do only what the user asked for. Everything you do is shown to them and logged.
- Read before acting (`GET /ui`, `GET /actions?menu=...`), act one step at a time, and check each response's `effects`.
- Use ids, never labels. Re-list rather than reuse an id marked `"stable": false`.
- Never skip a confirmation by other means; answer the dialog the way the user asked, or ask the user.
- If something needs the user (a login, a decision you were not given), stop and ask them.
