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

#include "ZipIgnoreWalk.h"

#include "ZipPath.h"
#include "remote/GitignoreMatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace ZipIgnoreWalk {

static QString posixRel(const QString &root, const QString &abs)
{
    QString rel = QDir(root).relativeFilePath(abs);
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rel.startsWith(QLatin1String("./")))
        rel.remove(0, 2);
    return rel;
}

static void loadGitignore(remote::GitignoreMatcher &matcher, const QString &dirAbs,
                          QSet<QString> *loaded)
{
    const QString clean = QDir::cleanPath(dirAbs);
    if (loaded->contains(clean))
        return;
    loaded->insert(clean);
    QFile f(QDir(clean).filePath(QStringLiteral(".gitignore")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    matcher.addRules(clean, QString::fromUtf8(f.readAll()));
}

static void walkDir(const QString &dirAbs, const QString &root, const QString &sel,
                    remote::GitignoreMatcher &matcher, QSet<QString> *loaded,
                    QList<File> *out)
{
    loadGitignore(matcher, dirAbs, loaded);

    const QFileInfoList ents = QDir(dirAbs).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);

    for (const QFileInfo &fi : ents) {
        if (fi.isDir()) {
            if (fi.isSymLink())
                continue;
            if (ZipPath::isHardSkippedDirName(fi.fileName()))
                continue;
            const QString childAbs = QDir::cleanPath(fi.absoluteFilePath());
            const QString relWs = root.isEmpty() ? posixRel(sel, childAbs)
                                                 : posixRel(root, childAbs);
            if (!relWs.isEmpty() && relWs != QLatin1String(".")
                && matcher.isIgnored(relWs, true)) {
                continue;
            }
            walkDir(childAbs, root, sel, matcher, loaded, out);
            continue;
        }

        const QString abs = QDir::cleanPath(fi.absoluteFilePath());
        const QString relSel = posixRel(sel, abs);
        if (relSel.isEmpty() || relSel == QLatin1String("."))
            continue;
        const QString relWs = root.isEmpty() ? relSel : posixRel(root, abs);
        if (!relWs.isEmpty() && matcher.isIgnored(relWs, false))
            continue;

        File f;
        f.absPath = abs;
        f.entryName = relSel;
        out->append(std::move(f));
    }
}

QList<File> walkLocalFolder(const QString &workspaceRoot, const QString &selectedFolder)
{
    QList<File> out;
    const QString root = QDir::cleanPath(workspaceRoot);
    const QString sel = QDir::cleanPath(selectedFolder);
    if (sel.isEmpty() || !QFileInfo(sel).isDir())
        return out;

    remote::GitignoreMatcher matcher;
    QSet<QString> loaded;

    if (!root.isEmpty()) {
        QString walk = root;
        loadGitignore(matcher, walk, &loaded);
        const QString relToRoot = posixRel(root, sel);
        if (!relToRoot.isEmpty() && relToRoot != QLatin1String(".")) {
            for (const QString &seg : relToRoot.split(QLatin1Char('/'))) {
                walk += QLatin1Char('/') + seg;
                loadGitignore(matcher, walk, &loaded);
            }
        }
    } else {
        loadGitignore(matcher, sel, &loaded);
    }

    walkDir(sel, root, sel, matcher, &loaded, &out);
    return out;
}

} // namespace ZipIgnoreWalk
