/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_HTTP_H
#define CONTROL_HTTP_H

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QPair>
#include <QString>

// Minimal HTTP/1.1 for the Control API: Content-Length bodies only, keep-alive,
// pipelining. Pure, no sockets.
namespace control::http {

struct Request
{
    QByteArray method;
    QByteArray path;  // percent-encoded as received, without the query
    QByteArray query; // raw, without '?'
    QList<QPair<QByteArray, QByteArray>> headers; // names lower-cased
    QByteArray body;
    bool keepAlive = true;

    // Case-insensitive; empty when absent.
    QByteArray header(QByteArrayView name) const;
    // Percent-decoded value of the first `key=` in the query; empty when absent.
    QString queryValue(QByteArrayView key) const;
    bool hasQuery(QByteArrayView key) const;
};

class RequestParser
{
public:
    enum class State { NeedMore, Complete, Error };

    static constexpr qsizetype kMaxHeadBytes = 16 * 1024;
    static constexpr qsizetype kMaxBodyBytes = 4 * 1024 * 1024;
    static constexpr int kMaxHeaders = 64;

    // Appends and parses. Complete: take() the request, then parse() again
    // for a pipelined one already buffered.
    State feed(QByteArrayView bytes);
    State parse();
    Request take();
    // The current request's head (method, path, headers) once parsed, before
    // its body has arrived: lets the server refuse it without reading more.
    bool hasHead() const { return m_haveHead; }
    const Request &head() const { return m_current; }

    int errorStatus() const { return m_errorStatus; }
    QByteArray errorReason() const { return m_errorReason; }
    qsizetype bufferedBytes() const { return m_buffer.size(); }

private:
    State fail(int status, const char *reason);
    State parseHead(qsizetype headEnd);

    QByteArray m_buffer;
    qsizetype m_scanFrom = 0; // where the next search for "\r\n\r\n" starts
    bool m_haveHead = false;
    qsizetype m_bodyLength = 0;
    Request m_current;
    State m_state = State::NeedMore;
    int m_errorStatus = 0;
    QByteArray m_errorReason;
};

QByteArray reasonPhrase(int status);
QByteArray response(int status, QByteArrayView contentType, QByteArrayView body, bool keepAlive,
                    QByteArrayView extraHeaders = {});
// Response head for a server-sent event stream (connection stays open).
QByteArray sseHead();
// One event; each line of `data` becomes its own `data:` field.
QByteArray sseEvent(QByteArrayView event, QByteArrayView data);

} // namespace control::http

#endif // CONTROL_HTTP_H
