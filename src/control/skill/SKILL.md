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

Skill version: 3 (API version 1).

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
| GET | `/instance` | App version, window title, workspaces, active workspace, `workspacePanels` (`root`, panel `id`, `active`), `workspaceTasks` (the active workspace's terminal tasks: `name`, `command`, `cwd`), active editor |
| GET | `/skill`, `/skill/version` | This skill, its version |
| GET | `/actions[?menu=<id>]` | Every action (or one menu): `id`, `label`, `menu` path, `shortcut`, `enabled`, `checked`, `location`, `value` (what a direct command takes). Menus filled on opening are filled first |
| POST | `/actions/trigger` | `{"id", "value"?}`: click the action |
| GET | `/ui[?root=<id>&depth=8&parts=50&hidden=1]` | The window tree: `{ok, nodes: [...], focus, modal, popup}`. `nodes` holds the root(s); each node has `id`, `role`, `label`, `value`/`checked`/`items`, `enabled`, `focus`, `parts`, `children` (`more: true` where `depth` cut it) |
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
| GET | `/log?limit=N` | The activity log: `time`, `op`, `target`, `label`, `ok`, `detail` (why it failed), `args` (what the step was given: `text`, `value`, `index`, `op` + `args`, ...; long strings cut, password fields `••••`) |

### Commands and effects

Every command (`/actions/trigger`, `/ui/click`, `type`, `key`, ...) answers once its work is done, or as soon as it opened a modal dialog or a menu:

```json
{"ok": true, "completed": false, "action": "actionPreferences", "label": "Settings ▸ Preferences…",
 "effects": {"opened": [{"id": "PreferencesDialog", "kind": "dialog", "title": "Preferences"}],
             "closed": [], "focus": "...", "modal": "PreferencesDialog"}}
```

- `completed: false`: a dialog or menu is waiting for input (`effects.modal`, `effects.popup`). Answer it with `/ui/*` on its ids, or `POST /ui/close`.
- While a menu is open (`popup`), only its own items answer; anything else is refused with `409`, except a click elsewhere, which closes the menu first, as it would for a user. `POST /ui/key {"keys": "Escape"}` closes it.
- `effects.opened` / `effects.closed`: windows, dialogs, tabs (`kind: dock`), panels (`kind: panel`, the workspace tree) and chats that appeared or went away. Clicking a dialog's OK answers after the dialog has done its work, so the tab it opened is listed there.

### Direct commands (pickers)

A user picks files, folders, fonts and colours in a system picker you cannot operate. Pass the value instead: `{"id": "actionOpen", "value": "C:/src/app/main.cpp"}` (or a list: `"value": ["a.txt", "b.txt"]`). Works on `/actions/trigger` and `/ui/click` (a "Browse…" button). `GET /actions` shows which actions open one (`value`). Confirmations ("Replace it?", "Save changes?") still appear; answer them.

## Widgets

| Role | Read (`/ui/read`) | Do |
|---|---|---|
| `dock` (a tab: `kind` editor, preview, tool, panel; the workspace panels too) | its title, `open`, `current` | `/ui/select` brings it to front (for a workspace panel: makes it the active workspace), `/ui/close` closes it (unsaved editors ask; a workspace panel closes its workspace), `/ui/do op=float` |
| `editor` | lines `from` (1-based, default 1), `count` (default 500): `text`, `path`, `modified`, `language`, `line`/`column`, `selection`; part `@selection` | `/ui/type` (multi-line text is inserted like a paste), `/ui/set` replaces all, `/ui/do op=goto args={"line", "column"?}`, `op=select args={"line","column"?,"toLine"?,"toColumn"?}` or `{"all":true}`; `/ui/key` for shortcuts |
| `terminal` | `rows` (no `count`: the screen; `from` < 0 counts back), `cursor`, `cwd`, `running`, `exited`; part `@selection` | `/ui/type` types (a newline is Enter), `/ui/key` sends keys (`Ctrl+C`) |
| `chat` (an AI agent session) | `entries` (messages and tool calls, last 30; `from`/`count`), `working`, `queued`, `waitingForUser`, `draft`; part `@report`: the last finished answer | `/ui/do op=send args={"text"}` types the prompt and presses Send; its widgets: `chatInput`, `chatSend`, `chatCancel`, `chatModel`, `chatMode`, `chatEffort` |
| `list`/`tree`/`table` | `rows` with `key`, `text`, `cells`, `expanded`, `selected` | click or select a row part; `/ui/do op=expand`/`collapse` |
| `tabs` | `current`, tabs as parts | click a tab part; `/ui/close` a tab part |

Any pane (a panel, a tab, an editor, a chat, a splitter child): `POST /ui/do {"id", "op": "resize", "args": {"width"?, "height"?}}` (pixels) drags the nearest edge that sets that size: a splitter handle, a panel's separator, a floating window's edge. The neighbour gives or takes the difference, within the minimum sizes. `GET /ui/node` gives its `rect`. A `422` says no edge sets that size; try the pane around it.

A question form or permission prompt from an agent is made of ordinary widgets inside the chat: read it with `GET /ui?root=chat:<id>`, answer with `/ui/click` and `/ui/type` (ids under Recipes).

## Events

`GET /events` streams `event: <name>` / `data: <json>` lines (each `data` has `seq` and `time`):

- `window.opened`, `window.closed` (`id`, `kind`, `title`, `modal`); `dock.shown`, `dock.hidden` (`id`, `title`, `open`: false once closed, true when another tab of its area became current; `current`); `focus.changed`
- `chat.turnStarted`, `chat.turnEnded`, `chat.waitingForUser` (`id` = `chat:<session>`)
- `terminal.taskExited`, `terminal.exited` (`id`, `exitCode`)
- `action`: each step an external agent took (`op`, `id`, `label`, `ok`, `args`)

## Waiting

`POST /wait` answers when the condition holds (`ok: true`, `waitedMs`) or with `408` after `timeout` seconds (default 30, max 600):

- `{"event": "chat.turnEnded", "match": {"id": "chat:abc"}}`: the next matching event (`"event": "window.*"` matches a prefix).
- `{"id": "<node>", "state": "visible"|"hidden"|"exists"|"gone"|"enabled"|"disabled"|"idle"|"waitingForUser"}`. `idle` is a chat or other widget not working, with nothing queued and not waiting for the user. `"state"` may be a list: `["idle", "waitingForUser"]` answers when either holds, with `state` naming which.
- `{"id": "<node>", "text": "Done"}`: its `/ui/read` contains the text (a terminal printing a line, a label changing).

## Recipes

Open a project folder (shows it in the workspace tree):
```sh
api -H "Content-Type: application/json" -d '{"id":"actionOpenFolderAsWorkspaceNew","value":"C:/src/app"}' "$NOTEPADAI_URL/actions/trigger"
```
The File menu shows `actionOpenFolderAsWorkspaceNew` (in its Open Folder as Workspace submenu) once there are recent folders, and `actionOpenFolderasWorkspace` before that; a `409` for the hidden one names the other. A folder that is already open becomes the active workspace.

Switch or close a workspace: `GET /instance` lists `workspacePanels`. `POST /ui/select {"id":"<panel id>"}` makes it the active workspace (like clicking its tab); `POST /ui/close {"id":"<panel id>"}` closes it (its close button). The View menu's entry for a panel (`menuView/toggle:<panel>`) shows or hides it: hiding a workspace panel closes the workspace.

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

Run a goal (the agent keeps working until success criteria are met):
1. Optionally type the task into the chat's `chatInput`; clicking the chat's `chatSendWithGoal` ("Goal") opens `SendWithGoalDialog` and sends that text when the goal starts.
2. Inside `SendWithGoalDialog/goalConfig`: the criteria are `criterion:1`, `criterion:2`, ... (`/ui/type`), `addCriterionButton` / `removeCriterionButton`, `goalAgentCombo`, `promptTemplateCombo`, `maxIterationsSpin`. `loadPresetButton` opens the saved presets as a popup (`presetMenu`): click `preset:<name>` (a project's own: `preset:project:<name>`) to fill the criteria, `deletePreset:<name>` to delete one (ids escape `/`, `@`, `#`, `%` in names; take them from the tree). `savePresetButton` saves the criteria as a preset. The footer has `autoCompactCheck`, `useNativeGoalCheck`, `prefixGoalCheck`, `startGoalButton`, `cancelButton`.
3. Wait on the chat as for any prompt.
To list the saved presets without starting a goal: open the dialog, click `loadPresetButton`, read the popup (`GET /ui`, its `popup`), press Escape, click `cancelButton`. Goal defaults (goal agent, max iterations, judge prompt templates) are in AI ▸ AI Agents… (`actionAiAgents`), tab "Goal agent": `goalAgentCombo`, `goalMaxIterationsSpin`, `goalTemplateCombo`, `goalTemplateEdit`. A scheduled task's goal: its edit dialog's Configure… opens `GoalAgentConfigDialog` with the same `goalConfig` widgets.

Run a command in a terminal:
1. Trigger `actionOpenTerminalInWorkspace`. `effects.opened` names its tab; the terminal is `dock:<uuid>/terminalDock/terminal` (or `GET /ui/find?role=terminal`).
2. `POST /ui/type {"id":"<terminal>","text":"npm test\n"}`
3. `POST /wait {"id":"<terminal>","text":"passing","timeout":300}` or poll `GET /ui/read?id=<terminal>`.
4. Close it with `POST /ui/close {"id":"dock:<uuid>"}` (a running command asks first: answer the `QMessageBox`).

Run a workspace task: `GET /instance` lists `workspaceTasks`. Trigger `menuTasks/task:<name>` (Terminal ▸ Tasks), or click the same `task:<name>` row in the empty terminal panel. The task opens its own terminal tab; it reports its exit through the `terminal.taskExited` event. Edit tasks: trigger `actionEditTasks` (dialog `EditTasksDialog`: `taskList` rows, `addTaskButton`, `removeTaskButton`, `moveTaskUpButton` / `moveTaskDownButton`, `taskNameEdit`, `taskCommandEdit`, `taskCwdEdit` (+ `browseCwdButton`, takes a `value`), `taskEnvEdit`, then `buttonBox/button:ok`).

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
