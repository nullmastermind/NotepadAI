/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlHttp.h"

#include <QUrl>

namespace control::http {

namespace {

bool equalsIgnoreCase(QByteArrayView a, QByteArrayView b)
{
    return a.size() == b.size() && a.compare(b, Qt::CaseInsensitive) == 0;
}

// Calls fn(part) for each part of `list` split at `sep`; stops when fn returns true.
template <typename Fn>
bool anyPart(QByteArrayView list, char sep, Fn fn)
{
    qsizetype start = 0;
    while (start <= list.size()) {
        qsizetype end = list.indexOf(sep, start);
        if (end < 0)
            end = list.size();
        if (fn(list.sliced(start, end - start)))
            return true;
        start = end + 1;
    }
    return false;
}

bool containsToken(QByteArrayView list, QByteArrayView token)
{
    return anyPart(list, ',', [token](QByteArrayView part) {
        return equalsIgnoreCase(part.trimmed(), token);
    });
}

QByteArrayView queryKey(QByteArrayView pair)
{
    const qsizetype eq = pair.indexOf('=');
    return eq < 0 ? pair : pair.first(eq);
}

} // namespace

QByteArray Request::header(QByteArrayView name) const
{
    for (const auto &h : headers) {
        if (equalsIgnoreCase(h.first, name))
            return h.second;
    }
    return {};
}

bool Request::hasQuery(QByteArrayView key) const
{
    return anyPart(query, '&', [key](QByteArrayView pair) { return queryKey(pair) == key; });
}

QString Request::queryValue(QByteArrayView key) const
{
    QString result;
    anyPart(query, '&', [key, &result](QByteArrayView pair) {
        if (queryKey(pair) != key)
            return false;
        const qsizetype eq = pair.indexOf('=');
        if (eq >= 0) {
            QByteArray value = pair.sliced(eq + 1).toByteArray();
            value.replace('+', ' ');
            result = QUrl::fromPercentEncoding(value);
        }
        return true;
    });
    return result;
}

RequestParser::State RequestParser::fail(int status, const char *reason)
{
    m_state = State::Error;
    m_errorStatus = status;
    m_errorReason = reason;
    return m_state;
}

RequestParser::State RequestParser::feed(QByteArrayView bytes)
{
    if (m_state == State::Error)
        return m_state;
    m_buffer.append(bytes);
    if (m_state == State::Complete)
        return m_state; // the caller has not taken the previous request yet
    return parse();
}

RequestParser::State RequestParser::parseHead(qsizetype headEnd)
{
    const QByteArrayView head(m_buffer.constData(), headEnd);
    qsizetype lineEnd = head.indexOf("\r\n");
    const QByteArrayView requestLine = lineEnd < 0 ? head : head.first(lineEnd);

    // METHOD SP target SP HTTP/x.y
    const qsizetype sp1 = requestLine.indexOf(' ');
    const qsizetype sp2 = sp1 < 0 ? -1 : requestLine.indexOf(' ', sp1 + 1);
    if (sp1 <= 0 || sp2 <= sp1 + 1)
        return fail(400, "malformed request line");
    const QByteArrayView version = requestLine.sliced(sp2 + 1);
    if (!version.startsWith("HTTP/1."))
        return fail(400, "unsupported HTTP version");
    const QByteArrayView target = requestLine.sliced(sp1 + 1, sp2 - sp1 - 1);
    if (!target.startsWith('/'))
        return fail(400, "request target must be a path");

    Request r;
    r.method = requestLine.first(sp1).toByteArray();
    const qsizetype q = target.indexOf('?');
    r.path = (q < 0 ? target : target.first(q)).toByteArray();
    if (q >= 0)
        r.query = target.sliced(q + 1).toByteArray();
    const bool http10 = version == "HTTP/1.0";
    r.keepAlive = !http10;

    qsizetype bodyLength = 0;
    qsizetype pos = lineEnd < 0 ? head.size() : lineEnd + 2;
    while (pos < head.size()) {
        qsizetype end = head.indexOf("\r\n", pos);
        if (end < 0)
            end = head.size();
        const QByteArrayView line = head.sliced(pos, end - pos);
        pos = end + 2;
        if (line.isEmpty())
            continue;
        if (r.headers.size() >= kMaxHeaders)
            return fail(431, "too many headers");
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            return fail(400, "malformed header");
        QByteArray name = line.first(colon).trimmed().toByteArray().toLower();
        QByteArray value = line.sliced(colon + 1).trimmed().toByteArray();
        if (name == "content-length") {
            bool ok = false;
            const qlonglong n = value.toLongLong(&ok);
            if (!ok || n < 0)
                return fail(400, "bad Content-Length");
            if (n > kMaxBodyBytes)
                return fail(413, "body too large");
            bodyLength = static_cast<qsizetype>(n);
        } else if (name == "transfer-encoding") {
            return fail(411, "chunked bodies are not supported; send Content-Length");
        } else if (name == "connection") {
            if (containsToken(value, "close"))
                r.keepAlive = false;
            else if (containsToken(value, "keep-alive"))
                r.keepAlive = true;
        }
        r.headers.append({std::move(name), std::move(value)});
    }

    m_current = std::move(r);
    m_bodyLength = bodyLength;
    m_haveHead = true;
    m_buffer.remove(0, headEnd + 4);
    m_scanFrom = 0;
    return State::NeedMore;
}

RequestParser::State RequestParser::parse()
{
    if (m_state != State::NeedMore)
        return m_state;
    if (!m_haveHead) {
        const qsizetype headEnd = m_buffer.indexOf("\r\n\r\n", m_scanFrom);
        if (headEnd < 0) {
            if (m_buffer.size() > kMaxHeadBytes)
                return fail(431, "request head too large");
            // Rescan only the tail that could still start the terminator.
            m_scanFrom = qMax<qsizetype>(0, m_buffer.size() - 3);
            return State::NeedMore;
        }
        if (headEnd > kMaxHeadBytes)
            return fail(431, "request head too large");
        if (parseHead(headEnd) == State::Error)
            return m_state;
    }
    if (m_buffer.size() < m_bodyLength)
        return State::NeedMore;
    m_current.body = m_buffer.first(m_bodyLength);
    m_buffer.remove(0, m_bodyLength);
    m_state = State::Complete;
    return m_state;
}

Request RequestParser::take()
{
    Request r = std::move(m_current);
    m_current = Request();
    m_haveHead = false;
    m_bodyLength = 0;
    m_scanFrom = 0;
    if (m_state == State::Complete)
        m_state = State::NeedMore;
    return r;
}

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Content Too Large";
    case 422: return "Unprocessable Content";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "Status";
    }
}

QByteArray response(int status, QByteArrayView contentType, QByteArrayView body, bool keepAlive,
                    QByteArrayView extraHeaders)
{
    QByteArray out;
    out.reserve(160 + contentType.size() + extraHeaders.size() + body.size());
    out += "HTTP/1.1 ";
    out += QByteArray::number(status);
    out += ' ';
    out += reasonPhrase(status);
    out += "\r\nContent-Type: ";
    out += contentType;
    out += "\r\nContent-Length: ";
    out += QByteArray::number(body.size());
    out += "\r\nCache-Control: no-store\r\nConnection: ";
    out += keepAlive ? "keep-alive" : "close";
    out += "\r\n";
    out += extraHeaders;
    out += "\r\n";
    out += body;
    return out;
}

QByteArray sseHead()
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                             "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n");
}

QByteArray sseEvent(QByteArrayView event, QByteArrayView data)
{
    QByteArray out;
    out.reserve(16 + event.size() + data.size() + 8);
    if (!event.isEmpty()) {
        out += "event: ";
        out += event;
        out += '\n';
    }
    anyPart(data, '\n', [&out](QByteArrayView line) {
        out += "data: ";
        out += line;
        out += '\n';
        return false;
    });
    out += '\n';
    return out;
}

} // namespace control::http
