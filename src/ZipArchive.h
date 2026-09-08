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

#ifndef ZIPARCHIVE_H
#define ZIPARCHIVE_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdio>

class QFile;

class ZipWriter
{
public:
    ZipWriter();
    ~ZipWriter();
    ZipWriter(const ZipWriter &) = delete;
    ZipWriter &operator=(const ZipWriter &) = delete;

    bool open(const QString &zipPath);
    bool addFile(const QString &absPath, const QString &entryName);
    bool addFromFile(QFile *src, qint64 size, const QString &entryName);
    bool finish();
    void abort();
    QString errorString() const { return m_error; }

private:
    bool addFromCallback(QFile *src, qint64 size, const QString &entryName);

    void *m_zip = nullptr;
    FILE *m_fp = nullptr;
    QString m_error;
    bool m_open = false;
};

class ZipReader
{
public:
    ZipReader();
    ~ZipReader();
    ZipReader(const ZipReader &) = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    bool open(const QString &zipPath);
    QStringList fileEntries() const;
    bool extractToFile(const QString &entryName, const QString &destPath);
    bool extractToBytes(const QString &entryName, QByteArray *out);
    QString errorString() const { return m_error; }

private:
    void *m_zip = nullptr;
    FILE *m_fp = nullptr;
    QString m_error;
    bool m_open = false;
};

#endif // ZIPARCHIVE_H
