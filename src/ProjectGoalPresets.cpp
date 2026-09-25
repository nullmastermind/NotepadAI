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

#include "ProjectGoalPresets.h"

#include "GoalAgentSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <limits>

namespace ProjectGoalPresets {
namespace {

constexpr qint64 kMaxFileBytes = 256 * 1024;

QString trError(const char *text)
{
    return QCoreApplication::translate("ProjectGoalPresets", text);
}

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

bool isDeviceName(const QString &name)
{
    if (name.compare(QLatin1String("CON"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("PRN"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("AUX"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("NUL"), Qt::CaseInsensitive) == 0)
        return true;
    if (name.size() != 4)
        return false;
    const QChar last = name.at(3);
    if (last < QLatin1Char('1') || last > QLatin1Char('9'))
        return false;
    const QString head = name.left(3);
    return head.compare(QLatin1String("COM"), Qt::CaseInsensitive) == 0
        || head.compare(QLatin1String("LPT"), Qt::CaseInsensitive) == 0;
}

bool hasIllegalChar(const QString &name)
{
    for (const QChar c : name) {
        const char16_t u = c.unicode();
        if (u < 32)
            return true;
        switch (u) {
        case u'\\':
        case u'/':
        case u':':
        case u'*':
        case u'?':
        case u'"':
        case u'<':
        case u'>':
        case u'|':
            return true;
        default:
            break;
        }
    }
    return false;
}

bool isMdFile(const QFileInfo &info)
{
    return info.isFile()
        && info.suffix().compare(QLatin1String("md"), Qt::CaseInsensitive) == 0;
}

// Positive integer, no leading zero. 1, 2, 10. Not 0, 01, or 1.5.
bool parsePositiveNumber(const QString &stem, int *out)
{
    if (stem.isEmpty() || stem.at(0) == QLatin1Char('0'))
        return false;
    int n = 0;
    const int len = stem.size();
    for (int i = 0; i < len; ++i) {
        const char16_t u = stem.at(i).unicode();
        if (u < '0' || u > '9')
            return false;
        const int digit = static_cast<int>(u - '0');
        if (n > (std::numeric_limits<int>::max() - digit) / 10)
            return false;
        n = n * 10 + digit;
    }
    if (n < 1)
        return false;
    *out = n;
    return true;
}

QString goalsPath(const QString &projectRoot)
{
    return QDir(projectRoot).filePath(QStringLiteral(".agents/.goals"));
}

Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool samePath(const QString &a, const QString &b)
{
    return QFileInfo(a).absoluteFilePath().compare(
               QFileInfo(b).absoluteFilePath(), pathCaseSensitivity())
        == 0;
}

QString normalizeBody(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text = text.trimmed();
    if (text.size() > GoalAgentSettings::kMaxCriterionChars)
        text.truncate(GoalAgentSettings::kMaxCriterionChars);
    return text;
}

// Stat first. A file over the cap is not a criterion and is not read.
QString readBody(const QFileInfo &info, bool *accepted)
{
    *accepted = false;
    if (!info.isFile() || info.size() <= 0 || info.size() > kMaxFileBytes)
        return {};
    QFile in(info.filePath());
    if (!in.open(QIODevice::ReadOnly))
        return {};
    QString body = normalizeBody(QString::fromUtf8(in.readAll()));
    if (body.isEmpty())
        return {};
    *accepted = true;
    return body;
}

struct NumberedFile {
    int number = 0;
    QString path;
    QString name;
};

QList<NumberedFile> numberedFiles(const QString &dirPath)
{
    QList<NumberedFile> found;
    const QDir dir(dirPath);
    if (!dir.exists())
        return found;
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    found.reserve(entries.size());
    for (const QFileInfo &info : entries) {
        int number = 0;
        if (!isMdFile(info) || !parsePositiveNumber(info.completeBaseName(), &number))
            continue;
        found.append(NumberedFile{number, info.absoluteFilePath(), info.fileName()});
    }
    std::sort(found.begin(), found.end(), [](const NumberedFile &a, const NumberedFile &b) {
        if (a.number != b.number)
            return a.number < b.number;
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return a.name < b.name;
    });
    return found;
}

QStringList readNumbered(const QString &dirPath)
{
    QStringList criteria;
    int lastNumber = 0;
    bool haveNumber = false;
    const QList<NumberedFile> files = numberedFiles(dirPath);
    for (const NumberedFile &file : files) {
        if (haveNumber && file.number == lastNumber)
            continue;
        haveNumber = true;
        lastNumber = file.number;
        if (criteria.size() >= GoalAgentSettings::kMaxCriteriaRows)
            break;
        bool accepted = false;
        const QString body = readBody(QFileInfo(file.path), &accepted);
        if (!accepted)
            continue;
        criteria.append(body);
    }
    return criteria;
}

struct Located {
    QString filePath;
    QString fileStem;
    QString dirPath;
    QString dirStem;
};

Located locate(const QString &goals, const QString &name)
{
    Located loc;
    const QDir dir(goals);
    if (!dir.exists())
        return loc;
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (info.isDir()) {
            if (info.fileName().compare(name, Qt::CaseInsensitive) == 0) {
                loc.dirPath = info.absoluteFilePath();
                loc.dirStem = info.fileName();
            }
            continue;
        }
        if (isMdFile(info)
            && info.completeBaseName().compare(name, Qt::CaseInsensitive) == 0) {
            loc.filePath = info.absoluteFilePath();
            loc.fileStem = info.completeBaseName();
        }
    }
    return loc;
}

QString winningStem(const Located &loc, const QString &typed)
{
    if (!loc.dirStem.isEmpty())
        return loc.dirStem;
    if (!loc.fileStem.isEmpty())
        return loc.fileStem;
    return typed;
}

bool writeUtf8(const QString &path, const QString &text, QString *error)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        setError(error, out.errorString());
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (out.write(bytes) != bytes.size() || !out.commit()) {
        setError(error, out.errorString());
        return false;
    }
    return true;
}

void removeNumberedExcept(const QString &dirPath, const QStringList &keep)
{
    const QList<NumberedFile> files = numberedFiles(dirPath);
    for (const NumberedFile &file : files) {
        bool kept = false;
        for (const QString &path : keep) {
            if (samePath(file.path, path)) {
                kept = true;
                break;
            }
        }
        if (!kept)
            QFile::remove(file.path);
    }
}

void rmdirIfEmpty(const QString &dirPath)
{
    const QDir dir(dirPath);
    if (!dir.exists())
        return;
    if (dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty())
        QDir().rmdir(dirPath);
}

QStringList normalizeCriteria(const QStringList &criteria)
{
    QStringList out;
    const qsizetype cap = GoalAgentSettings::kMaxCriteriaRows;
    out.reserve(criteria.size() < cap ? criteria.size() : cap);
    for (const QString &raw : criteria) {
        const QString body = normalizeBody(raw);
        if (body.isEmpty())
            continue;
        out.append(body);
        if (out.size() >= GoalAgentSettings::kMaxCriteriaRows)
            break;
    }
    return out;
}

} // namespace

bool isValidName(const QString &name)
{
    if (name.isEmpty() || name != name.trimmed() || name.size() > 100)
        return false;
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    if (name.endsWith(QLatin1Char('.')))
        return false;
    if (isDeviceName(name) || hasIllegalChar(name))
        return false;
    return true;
}

bool projectScopeAvailable(const QString &workingDirectory, bool sessionIsRemote)
{
    return !sessionIsRemote && !workingDirectory.isEmpty()
        && QFileInfo(workingDirectory).isDir();
}

bool save(const QString &projectRoot, const QString &name,
          const QStringList &criteria, QString *error)
{
    if (!isValidName(name)) {
        setError(error, trError("Preset name cannot be used as a file name."));
        return false;
    }
    const QStringList normalized = normalizeCriteria(criteria);
    if (normalized.isEmpty()) {
        setError(error, trError("No criteria to save."));
        return false;
    }
    if (!QFileInfo(projectRoot).isDir()) {
        setError(error, trError("Project folder is not available."));
        return false;
    }

    const QString goals = goalsPath(projectRoot);
    if (!QDir().mkpath(goals)) {
        setError(error, trError("Could not create the goals folder."));
        return false;
    }

    const Located loc = locate(goals, name);
    const QString stem = winningStem(loc, name);

    if (normalized.size() == 1) {
        const QString filePath = QDir(goals).filePath(stem + QStringLiteral(".md"));
        if (!writeUtf8(filePath, normalized.at(0), error))
            return false;
        if (!loc.dirPath.isEmpty()) {
            removeNumberedExcept(loc.dirPath, {});
            rmdirIfEmpty(loc.dirPath);
        }
        if (!loc.filePath.isEmpty() && !samePath(loc.filePath, filePath))
            QFile::remove(loc.filePath);
        return true;
    }

    const QString dirPath = loc.dirPath.isEmpty()
        ? QDir(goals).filePath(stem)
        : loc.dirPath;
    if (!QDir().mkpath(dirPath)) {
        setError(error, trError("Could not create the preset folder."));
        return false;
    }

    // One listing. Later writes must not hide a failed commit behind a rescan.
    const QList<NumberedFile> existing = numberedFiles(dirPath);
    QStringList written;
    written.reserve(normalized.size());
    for (int i = 0; i < normalized.size(); ++i) {
        const int number = i + 1;
        QString path;
        for (const NumberedFile &file : existing) {
            if (file.number == number) {
                path = file.path;
                break;
            }
        }
        if (path.isEmpty())
            path = QDir(dirPath).filePath(QString::number(number) + QStringLiteral(".md"));
        if (!writeUtf8(path, normalized.at(i), error))
            return false;
        written.append(path);
    }

    if (!loc.filePath.isEmpty())
        QFile::remove(loc.filePath);
    removeNumberedExcept(dirPath, written);
    return true;
}

bool read(const QString &projectRoot, const QString &name,
          QStringList *criteria, QString *error)
{
    if (criteria)
        criteria->clear();
    const Located loc = locate(goalsPath(projectRoot), name);
    if (loc.dirPath.isEmpty() && loc.filePath.isEmpty()) {
        setError(error, trError("Preset file is missing."));
        return false;
    }

    QStringList found;
    if (!loc.dirPath.isEmpty()) {
        found = readNumbered(loc.dirPath);
    } else {
        bool accepted = false;
        const QString body = readBody(QFileInfo(loc.filePath), &accepted);
        if (accepted)
            found.append(body);
    }
    if (found.isEmpty()) {
        setError(error, trError("Preset has no criteria."));
        return false;
    }
    if (criteria)
        *criteria = found;
    return true;
}

QList<Listed> list(const QString &projectRoot)
{
    QList<Listed> out;
    const QDir goals(goalsPath(projectRoot));
    if (!goals.exists())
        return out;

    const QFileInfoList entries = goals.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);

    QStringList dirNames;
    dirNames.reserve(entries.size());
    for (const QFileInfo &info : entries) {
        if (!info.isDir() || !isValidName(info.fileName()))
            continue;
        const QStringList criteria = readNumbered(info.absoluteFilePath());
        if (criteria.isEmpty())
            continue;
        out.append(Listed{info.fileName(), static_cast<int>(criteria.size())});
        dirNames.append(info.fileName());
    }

    for (const QFileInfo &info : entries) {
        if (!isMdFile(info) || !isValidName(info.completeBaseName()))
            continue;
        bool coveredByDir = false;
        for (const QString &dirName : dirNames) {
            if (dirName.compare(info.completeBaseName(), Qt::CaseInsensitive) == 0) {
                coveredByDir = true;
                break;
            }
        }
        if (coveredByDir)
            continue;
        bool accepted = false;
        readBody(info, &accepted);
        if (!accepted)
            continue;
        out.append(Listed{info.completeBaseName(), 1});
    }

    std::sort(out.begin(), out.end(), [](const Listed &a, const Listed &b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return a.name < b.name;
    });
    return out;
}

bool remove(const QString &projectRoot, const QString &name, QString *error)
{
    const Located loc = locate(goalsPath(projectRoot), name);
    if (loc.dirPath.isEmpty() && loc.filePath.isEmpty()) {
        setError(error, trError("Preset file is missing."));
        return false;
    }
    if (!loc.dirPath.isEmpty()) {
        removeNumberedExcept(loc.dirPath, {});
        rmdirIfEmpty(loc.dirPath);
        return true;
    }
    if (!QFile::remove(loc.filePath)) {
        setError(error, trError("Could not delete the preset."));
        return false;
    }
    return true;
}

} // namespace ProjectGoalPresets
