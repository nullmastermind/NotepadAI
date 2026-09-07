/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef AI_SSE_PARTIAL_PARSER_H
#define AI_SSE_PARTIAL_PARSER_H

#include <QByteArray>
#include <QString>

#include <functional>

namespace ai {

// Tolerant, bytewise Server-Sent Events parser for OpenAI-compatible
// /v1/chat/completions streams and Anthropic /v1/messages streams. Buffers raw
// bytes at the byte level and emits events at the SSE event boundary (\n\n or
// \r\n\r\n), so UTF-8 multi-byte chars never split across emitted tokens.
//
// Recognized payload shapes (in priority order, first hit wins per event):
//   - data: "[DONE]"                                        → Done
//   - JSON with "choices"[0]."delta"."content"              → Token
//   - JSON with "choices"[0]."message"."content"            → Token (fallback)
//   - JSON with type=content_block_delta, delta.text        → Token (Anthropic)
//   - JSON with "content" (string)                          → Token (fallback)
//   - JSON with "response" (string)                         → Token (Ollama-native)
//   - JSON with "error"                                     → Error
//   - JSON with finish_reason != null OR done == true       → Done
//   - JSON with type=message_stop                           → Done (Anthropic)
//
// Comment lines (": ...") and unknown event fields are ignored. Malformed JSON
// triggers no event but does NOT abort parsing of subsequent events.
class SsePartialParser
{
public:
    enum class EventKind : std::uint8_t { Token, Done, Error };

    struct Event {
        EventKind kind = EventKind::Token;
        QString   text;    // token content (Token) or error message (Error)
    };

    using EventCallback = std::function<void(const Event &)>;

    SsePartialParser() = default;

    // Feed raw bytes from the network; emits zero or more events via callback.
    // The parser is stateful — call repeatedly with successive chunks.
    void feed(QByteArrayView newBytes, const EventCallback &cb);

    // True if there are bytes accumulated but no complete event yet.
    bool hasPending() const { return !m_buffer.isEmpty(); }

    void reset() { m_buffer.clear(); }

private:
    QByteArray m_buffer;

    void processEvent(const QByteArray &eventBytes, const EventCallback &cb);
};

} // namespace ai

#endif // AI_SSE_PARTIAL_PARSER_H
