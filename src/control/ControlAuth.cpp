/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlAuth.h"

#include <QRandomGenerator>

#include <array>

namespace control::auth {

QString generateToken()
{
    std::array<quint32, 8> words{};
    QRandomGenerator::system()->fillRange(words.data(), words.size());
    const QByteArray raw(reinterpret_cast<const char *>(words.data()), sizeof(words));
    return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool bearerMatches(QByteArrayView authorization, QByteArrayView token)
{
    if (token.isEmpty())
        return false;
    const QByteArrayView value = authorization.trimmed();
    constexpr QByteArrayView scheme("bearer ");
    if (value.size() <= scheme.size() || value.first(scheme.size()).compare(scheme, Qt::CaseInsensitive) != 0)
        return false;
    const QByteArrayView given = value.sliced(scheme.size()).trimmed();
    // Length differences end early: the length of a 43-char token is no secret.
    if (given.size() != token.size())
        return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < token.size(); ++i)
        diff |= static_cast<unsigned char>(given[i] ^ token[i]);
    return diff == 0;
}

HostPolicy HostPolicy::local(quint16 port)
{
    HostPolicy p;
    p.m_port = port;
    p.addAddresses({QStringLiteral("127.0.0.1"), QStringLiteral("localhost"), QStringLiteral("::1")});
    return p;
}

void HostPolicy::addAddresses(const QStringList &addresses)
{
    const QByteArray suffix = ':' + QByteArray::number(m_port);
    for (const QString &a : addresses) {
        QByteArray host = a.toLatin1().toLower();
        if (host.contains(':'))
            host = '[' + host + ']';
        host += suffix;
        if (!m_hosts.contains(host))
            m_hosts.append(host);
    }
}

bool HostPolicy::hostAllowed(QByteArrayView host) const
{
    const QByteArrayView h = host.trimmed();
    for (const QByteArray &allowed : m_hosts) {
        if (h.size() == allowed.size() && h.compare(allowed, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool HostPolicy::originAllowed(QByteArrayView origin) const
{
    const QByteArrayView o = origin.trimmed();
    if (o.isEmpty())
        return true;
    constexpr QByteArrayView http("http://");
    if (o.size() <= http.size() || o.first(http.size()).compare(http, Qt::CaseInsensitive) != 0)
        return false;
    return hostAllowed(o.sliced(http.size()));
}

} // namespace control::auth
