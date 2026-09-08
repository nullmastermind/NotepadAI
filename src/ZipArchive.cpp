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

#include "ZipArchive.h"

#include "miniz.h"

#include <QDir>
#include <QFile>
#include <cstdio>

#ifdef Q_OS_WIN
#include <io.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif

namespace {

FILE *openUtfPath(const QString &path, const QString &mode)
{
#ifdef Q_OS_WIN
    QString native = QDir::toNativeSeparators(path);
    if (native.size() >= 248 && !native.startsWith(QLatin1String("\\\\?\\"))) {
        if (native.startsWith(QLatin1String("\\\\")))
            native = QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
        else
            native = QStringLiteral("\\\\?\\") + native;
    }
    return _wfopen(reinterpret_cast<const wchar_t *>(native.utf16()),
                   reinterpret_cast<const wchar_t *>(mode.utf16()));
#else
    return fopen(QFile::encodeName(path).constData(), mode.toLatin1().constData());
#endif
}

size_t qfileWriteCb(void *opaque, mz_uint64 fileOfs, const void *buf, size_t n)
{
    auto *f = static_cast<QFile *>(opaque);
    if (!f->seek(qint64(fileOfs)))
        return 0;
    const qint64 put = f->write(static_cast<const char *>(buf), qint64(n));
    return put < 0 ? 0 : size_t(put);
}

} // namespace

static mz_zip_archive *z(void *p) { return static_cast<mz_zip_archive *>(p); }

ZipWriter::ZipWriter() = default;

ZipWriter::~ZipWriter()
{
    abort();
}

bool ZipWriter::open(const QString &zipPath)
{
    abort();
    m_fp = openUtfPath(zipPath, QStringLiteral("wb+"));
    if (!m_fp) {
        m_error = QStringLiteral("could not create zip file");
        return false;
    }
    m_zip = new mz_zip_archive();
    mz_zip_zero_struct(z(m_zip));
    if (!mz_zip_writer_init_cfile(z(m_zip), m_fp, 0)) {
        m_error = QStringLiteral("zip writer init failed");
        fclose(m_fp);
        m_fp = nullptr;
        delete z(m_zip);
        m_zip = nullptr;
        return false;
    }
    m_open = true;
    return true;
}

bool ZipWriter::addFromCallback(QFile *src, qint64 size, const QString &entryName)
{
    if (!m_open || !src)
        return false;
    const QByteArray name = entryName.toUtf8();
    if (size <= 0) {
        if (!mz_zip_writer_add_mem(z(m_zip), name.constData(), "", 0, MZ_BEST_SPEED)) {
            m_error = QStringLiteral("failed to add %1").arg(entryName);
            return false;
        }
        return true;
    }

    src->flush();
    FILE *fp = nullptr;
#ifdef Q_OS_WIN
    const int fd = _dup(int(src->handle()));
    if (fd != -1)
        fp = _fdopen(fd, "rb");
#else
    const int fd = ::dup(int(src->handle()));
    if (fd != -1)
        fp = fdopen(fd, "rb");
#endif
    if (!fp) {
        const QString path = src->fileName();
        if (!path.isEmpty())
            return addFile(path, entryName);
        m_error = QStringLiteral("could not stream %1 without loading it into RAM").arg(entryName);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        m_error = QStringLiteral("could not read %1").arg(entryName);
        return false;
    }
    const mz_bool ok = mz_zip_writer_add_cfile(z(m_zip), name.constData(), fp, mz_uint64(size),
                                               nullptr, nullptr, 0, MZ_BEST_SPEED,
                                               nullptr, 0, nullptr, 0);
    fclose(fp);
    if (!ok) {
        m_error = QStringLiteral("failed to add %1").arg(entryName);
        return false;
    }
    return true;
}

bool ZipWriter::addFile(const QString &absPath, const QString &entryName)
{
    FILE *fp = openUtfPath(absPath, QStringLiteral("rb"));
    if (!fp) {
        m_error = QStringLiteral("could not read %1 (locked, missing, or path too long)")
                      .arg(absPath);
        return false;
    }
#ifdef Q_OS_WIN
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
#else
    if (fseeko(fp, 0, SEEK_END) != 0) {
#endif
        fclose(fp);
        m_error = QStringLiteral("could not read %1").arg(absPath);
        return false;
    }
#ifdef Q_OS_WIN
    const __int64 sz = _ftelli64(fp);
#else
    const off_t sz = ftello(fp);
#endif
    if (sz < 0) {
        fclose(fp);
        m_error = QStringLiteral("could not read %1").arg(absPath);
        return false;
    }
#ifdef Q_OS_WIN
    _fseeki64(fp, 0, SEEK_SET);
#else
    fseeko(fp, 0, SEEK_SET);
#endif
    const QByteArray name = entryName.toUtf8();
    mz_bool ok;
    if (sz == 0) {
        ok = mz_zip_writer_add_mem(z(m_zip), name.constData(), "", 0, MZ_BEST_SPEED);
    } else {
        ok = mz_zip_writer_add_cfile(z(m_zip), name.constData(), fp, mz_uint64(sz),
                                     nullptr, nullptr, 0, MZ_BEST_SPEED,
                                     nullptr, 0, nullptr, 0);
    }
    fclose(fp);
    if (!ok) {
        m_error = QStringLiteral("failed to add %1").arg(absPath);
        return false;
    }
    return true;
}

bool ZipWriter::addFromFile(QFile *src, qint64 size, const QString &entryName)
{
    if (!src)
        return false;
    src->seek(0);
    return addFromCallback(src, size, entryName);
}

bool ZipWriter::finish()
{
    if (!m_open)
        return false;
    const bool ok = mz_zip_writer_finalize_archive(z(m_zip)) != 0
                    && mz_zip_writer_end(z(m_zip)) != 0;
    delete z(m_zip);
    m_zip = nullptr;
    if (m_fp) {
        fclose(m_fp);
        m_fp = nullptr;
    }
    m_open = false;
    if (!ok)
        m_error = QStringLiteral("failed to finalize zip");
    return ok;
}

void ZipWriter::abort()
{
    if (m_zip) {
        mz_zip_writer_end(z(m_zip));
        delete z(m_zip);
        m_zip = nullptr;
    }
    if (m_fp) {
        fclose(m_fp);
        m_fp = nullptr;
    }
    m_open = false;
}

ZipReader::ZipReader() = default;

ZipReader::~ZipReader()
{
    if (m_zip) {
        mz_zip_reader_end(z(m_zip));
        delete z(m_zip);
        m_zip = nullptr;
    }
    if (m_fp) {
        fclose(m_fp);
        m_fp = nullptr;
    }
}

bool ZipReader::open(const QString &zipPath)
{
    m_fp = openUtfPath(zipPath, QStringLiteral("rb"));
    if (!m_fp) {
        m_error = QStringLiteral("could not open zip");
        return false;
    }
    m_zip = new mz_zip_archive();
    mz_zip_zero_struct(z(m_zip));
    if (!mz_zip_reader_init_cfile(z(m_zip), m_fp, 0, 0)) {
        m_error = QStringLiteral("invalid ZIP");
        fclose(m_fp);
        m_fp = nullptr;
        delete z(m_zip);
        m_zip = nullptr;
        return false;
    }
    m_open = true;
    return true;
}

QStringList ZipReader::fileEntries() const
{
    QStringList names;
    if (!m_open)
        return names;
    const mz_uint n = mz_zip_reader_get_num_files(z(m_zip));
    names.reserve(int(n));
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(z(m_zip), i, &st))
            continue;
        if (st.m_is_directory)
            continue;
        names.append(QString::fromUtf8(st.m_filename));
    }
    return names;
}

bool ZipReader::extractToFile(const QString &entryName, const QString &destPath)
{
    QFile dest(destPath);
    if (!dest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = QStringLiteral("could not write %1").arg(destPath);
        return false;
    }
    const QByteArray name = entryName.toUtf8();
    if (!mz_zip_reader_extract_file_to_callback(z(m_zip), name.constData(), qfileWriteCb, &dest, 0)) {
        m_error = QStringLiteral("failed to extract %1").arg(entryName);
        dest.remove();
        return false;
    }
    return true;
}

bool ZipReader::extractToBytes(const QString &entryName, QByteArray *out)
{
    if (!out)
        return false;
    const QByteArray name = entryName.toUtf8();
    size_t size = 0;
    void *mem = mz_zip_reader_extract_file_to_heap(z(m_zip), name.constData(), &size, 0);
    if (!mem) {
        m_error = QStringLiteral("failed to extract %1").arg(entryName);
        return false;
    }
    *out = QByteArray(static_cast<const char *>(mem), int(size));
    mz_free(mem);
    return true;
}
