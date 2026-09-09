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

#include "GitignoreMatcher.h"
#include "wildmatch.h"

#include <QStringList>
#include <QByteArray>

namespace remote {

void GitignoreMatcher::addRules(const QString &dirPath, const QString &rulesText)
{
    QString giDir = dirPath;
    giDir.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (giDir.endsWith(QLatin1Char('/')))
        giDir.chop(1);
    if (giDir == QLatin1String("."))
        giDir.clear();

    const QStringList lines = rulesText.split(QLatin1Char('\n'));
    for (QString line : lines) {
        // Strip trailing CR (Windows line endings).
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        // Skip empty lines and comments.
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        GitignoreRule rule;
        rule.dir = giDir;

        // Negation.
        if (line.startsWith(QLatin1Char('!'))) {
            rule.negated = true;
            line = line.mid(1);
        } else {
            rule.negated = false;
        }

        // Trailing slash means directory-only match. Strip it for matching.
        if (line.endsWith(QLatin1Char('/'))) {
            rule.dirOnly = true;
            line.chop(1);
        } else {
            rule.dirOnly = false;
        }

        if (line.isEmpty()) continue;

        // Anchored: pattern contains '/' (after the leading '!' was stripped and
        // before the trailing '/' was stripped). An anchored pattern is relative
        // to the directory containing the .gitignore, not basename-matched.
        // A leading '/' anchors to the .gitignore directory too — strip it.
        if (line.startsWith(QLatin1Char('/'))) {
            rule.anchored = true;
            line = line.mid(1);
        } else {
            rule.anchored = line.contains(QLatin1Char('/'));
        }

        rule.wildstar = line.contains(QStringLiteral("**"));
        rule.pattern = line;

        m_rules.append(rule);
    }
}

bool GitignoreMatcher::matchRule(const GitignoreRule &rule, const QString &relPath, bool isDir) const
{
    // Directory-only rules never match files.
    if (rule.dirOnly && !isDir)
        return false;

    QString owned;
    QStringView path = relPath;
    if (path.contains(QLatin1Char('\\'))) {
        owned = relPath;
        owned.replace(QLatin1Char('\\'), QLatin1Char('/'));
        path = owned;
    }

    if (!rule.dir.isEmpty()) {
        const int dirLen = rule.dir.size();
        if (path.size() <= dirLen || path.left(dirLen) != rule.dir
            || path.at(dirLen) != QLatin1Char('/')) {
            return false;
        }
        path = path.mid(dirLen + 1);
        if (path.isEmpty())
            return false;
    }

    const unsigned int flags = WM_PATHNAME | WM_WILDSTAR;
    const QByteArray patternBytes = rule.pattern.toUtf8();
    const char *pat = patternBytes.constData();
    const QByteArray textBytes = path.toUtf8();

    if (rule.anchored)
        return wildmatch(pat, textBytes.constData(), flags) == WM_MATCH;

    if (wildmatch(pat, textBytes.constData(), flags) == WM_MATCH)
        return true;

    const int lastSlash = path.lastIndexOf(QLatin1Char('/'));
    if (lastSlash >= 0) {
        const QByteArray basenameBytes = path.mid(lastSlash + 1).toUtf8();
        if (wildmatch(pat, basenameBytes.constData(), WM_WILDSTAR) == WM_MATCH)
            return true;
    }
    return false;
}

bool GitignoreMatcher::isIgnored(const QString &relPath, bool isDir) const
{
    // Walk rules in order. Later rules override earlier ones.
    // Final state after all rules = ignored if last matching rule is non-negated.
    bool ignored = false;
    for (const GitignoreRule &rule : m_rules) {
        if (matchRule(rule, relPath, isDir)) {
            ignored = !rule.negated;
        }
    }
    return ignored;
}

void GitignoreMatcher::clear()
{
    m_rules.clear();
}

} // namespace remote
