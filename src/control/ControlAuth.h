/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_AUTH_H
#define CONTROL_AUTH_H

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringList>

// Who may talk to the Control API: a bearer token, and Host/Origin headers
// that name this server (a web page cannot reach it through DNS rebinding or
// a cross-site request).
namespace control::auth {

// 32 random bytes from the system generator, base64url without padding.
QString generateToken();

// `Authorization` header value "Bearer <token>" (scheme case-insensitive).
// Constant time in the token length.
bool bearerMatches(QByteArrayView authorization, QByteArrayView token);

class HostPolicy
{
public:
    // 127.0.0.1, localhost and [::1] on `port`.
    static HostPolicy local(quint16 port);
    // Interface addresses a LAN client may name in Host (IPv6 without brackets).
    void addAddresses(const QStringList &addresses);

    quint16 port() const { return m_port; }
    bool hostAllowed(QByteArrayView host) const;
    // Absent is fine (non-browser clients); otherwise http://<allowed host>.
    bool originAllowed(QByteArrayView origin) const;

private:
    quint16 m_port = 0;
    QList<QByteArray> m_hosts; // lower-case "name:port" / "[v6]:port"
};

} // namespace control::auth

#endif // CONTROL_AUTH_H
