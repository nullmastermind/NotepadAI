# AI Agents (ACP)

## What is ACP?

The Agent Client Protocol (ACP) is a JSON-RPC 2.0 stdio protocol used by Claude, Auggie, Gemini, and other agent runtimes. The agent is a child process; the editor (this host) sends `initialize`, `session/new`, `session/prompt`, etc., over stdin and receives `session/update` notifications and incoming `fs/*` / `terminal/*` / `request_permission` requests on stdout. Newline-delimited JSON frames carry the wire payload.

## Why we don't link the Rust crate

We speak ACP's JSON-RPC 2.0 wire from C++ via `QProcess` + newline-delimited `QJsonDocument` framing. From `design.md` (D1):

> The JSON-RPC surface we need is small (about a dozen request methods + a handful of notification variants). Implementing it in Qt is cheaper than maintaining an FFI bridge.

The reference implementation itself spawns the agent as a child and talks to it over stdio — the Rust crate is one JSON-RPC client, not part of the wire format. A C ABI for the crate does not exist upstream; inventing one would mean owning a cargo + cbindgen + per-platform `.dylib`/`.so`/`.dll` toolchain in CMake. See `openspec/changes/add-ai-agent-acp/design.md` for the full rationale.

**If you came here looking for a Rust dependency, there isn't one — and that is by design.**

## Adding a new agent

1. Open **Settings → AI Agents…**
2. Click **Add**.
3. Fill in:
   - **Name** (display label)
   - **Command** (executable; resolved via PATH at spawn time)
   - **Args** (one per row)
   - **Env** (optional key/value pairs merged with the inherited environment)
   - **Icon** (optional)
4. Optionally set the new agent as the **Default Agent** for the `AI → Open AI Agent in …` menu actions.
5. Built-in agents (e.g. `builtin:claude-code`) cannot be edited or deleted; you can clone their definition into a custom entry instead.

The default built-in agent is **Claude Code** via `npx -y @zed-industries/claude-agent-acp`.

## Runtime requirements

- **Default Claude Code agent**: Node 18+ and `npx` on the user's `PATH`. The first launch downloads `@zed-industries/claude-agent-acp` (2–5 s spawn delay). Authentication uses `claude login` in a terminal; the agent surfaces "auth required" via stderr, which we classify as `AuthRequired` and display in the dock banner.
- **Other agents**: whatever the agent's own runtime requires (e.g. `uvx` for Python agents, a native binary for Go agents).

## Debugging

Enable per-category logging via `QT_LOGGING_RULES`:

```sh
# Everything
QT_LOGGING_RULES="notepadnext.acp.*=true" ./NotepadNext

# Finer scope
QT_LOGGING_RULES="notepadnext.acp.manager=true;notepadnext.acp.connection=true"
```

Available categories:

- `notepadnext.acp.manager` — `AcpAgentManager` lifecycle (open/close/reap)
- `notepadnext.acp.connection` — JSON-RPC frame in/out, spawn diagnostics
- `notepadnext.acp.history` — `AcpHistoryStore` writes and flushes
- `notepadnext.acp.session` — `AcpSessionModel` state transitions

Stderr from the child agent is captured and logged with a `[<sessionId>]` prefix; tail the log to see the agent's own messages alongside RPC traffic.

## File layout

| Path | Purpose |
|------|---------|
| `src/AcpAgentDefinition.h` | POD struct + JSON (de)serializer for one agent config row |
| `src/AcpAgentRegistry.{h,cpp}` | In-memory list of agents, persisted under `Ai/Agents` |
| `src/AcpProtocol.{h,cpp}` | Wire-protocol constants, POD payloads, framing/serialization helpers |
| `src/AcpConnection.{h,cpp}` | Owns one `QProcess` + JSON-RPC dispatch for one session |
| `src/AcpErrorClassifier.{h,cpp}` | Pure functions classifying spawn / auth / init failures |
| `src/AcpSessionModel.{h,cpp}` | Per-session view-model — messages, tool calls, plan, usage |
| `src/AcpHistoryStore.{h,cpp}` | Debounced JSON writer on a dedicated worker thread |
| `src/AcpAgentManager.{h,cpp}` | App-level owner of registry + history thread + live connections |
| `src/docks/AiAgentDock.{h,cpp}` | `QDockWidget` host for the chat UI |
| `src/widgets/AcpSessionView.{h,cpp}` | Top-level chat composite (transcript, selectors, input) |
| `src/widgets/AcpMessageWidget.{h,cpp}` | Per-message widget (markdown for assistant, plain for user) |
| `src/widgets/AcpToolCallCard.{h,cpp}` | Collapsible tool-call card with status icon |
| `src/widgets/AcpPlanWidget.{h,cpp}` | Plan-entry list view |
| `src/widgets/AcpUsageIndicator.{h,cpp}` | Token usage label + context-window progress bar |
| `src/widgets/AcpImageAttachmentList.{h,cpp}` | Image attachment queue (paste/drop/Attach) |
| `src/widgets/AcpPermissionPrompt.{h,cpp}` | Inline Allow/Deny prompt for `request_permission` in manual mode |
| `src/dialogs/AcpAgentSettingsDialog.{h,cpp,ui}` | Agent CRUD + default + auto-approve dialog |

## Wire surface

**Outbound requests** (host → agent):

- `initialize` — protocol-version handshake, capabilities exchange
- `session/new` — create a fresh session against a working directory
- `session/prompt` — send a user turn (text + optional image content blocks)
- `session/cancel` — cancel the in-flight prompt
- `session/set_mode`, `session/set_model`, `session/set_config_option` — pick mode/model/option

**Inbound requests** (agent → host, host replies):

- `fs/read_text_file`, `fs/write_text_file` — path-sandboxed file I/O
- `terminal/create`, `terminal/output`, `terminal/wait_for_exit`, `terminal/kill`, `terminal/release` — per-session subprocess
- `request_permission` — interactive consent (auto-approved in `allowAll` mode)
- `ext_method` — unknown extension probes; we reply with `{}` so probes don't error

**Inbound notifications**: `session/update` carries `agent_message_chunk`, `agent_thought_chunk`, `tool_call`, `tool_call_update`, `plan`, `available_commands_update`, `current_mode_update`, `session_info_update`, `prompt_start`, `prompt_end`. Unknown variants are silently dropped.

## History persistence

Per-session JSON file at `<QStandardPaths::AppDataLocation>/acp-history/<sessionId>.json`. Schema fields:

- `projectId` — string or null
- `messages` — array of `{ role, content[], timestamp, command, exitCode }`
- `toolCalls` — array of `{ toolCallId, title, status, content[], groupId }`
- `timeline` — array of `{ type: "message"|"tool_call", … }` ordering hints
- `usage` — `{ inputTokens, outputTokens, maxTokens }` or null
- `updatedAt` — epoch milliseconds

Writes are debounced at 500 ms per session by a `QTimer` on the worker thread; pending writes are flushed before the worker quits. Writes are atomic (`.tmp` + `QFile::rename`).

## Bumping the protocol version

Single point of change: `AcpProtocol::kProtocolVersion` in `src/AcpProtocol.h`. The constant is sent verbatim during `initialize`. Bump in lockstep with the upstream `agent-client-protocol` crate's wire version.

## Known limitations (MVP)

Out of scope for this change (deferred to future work):

- `session/load` / `session/resume` recovery — history is written to disk, but reopening a workspace does not auto-rehydrate the prior session.
- Markdown advanced rendering — no Mermaid, math (KaTeX), footnotes, or GitHub-style alerts.
- Remote agent-registry browser / installable agent catalog.
- Telegram or other external bridges.
- Notepad++ session-file migration of AI history.
- User-visible idle-reaper or active-connections panel (the reaper runs silently every 5 min, destroying connections whose dock has been gone >1 h).
- Per-session or per-tool overrides of the auto-approve policy.
