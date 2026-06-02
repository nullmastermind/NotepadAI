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
    const QStringList lines = rulesText.split(QLatin1Char('\n'));
    for (QString line : lines) {
        // Strip trailing CR (Windows line endings).
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        // Skip empty lines and comments.
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        GitignoreRule rule;
        rule.dir = dirPath;

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

    // Build the flags for wildmatch.
    unsigned int flags = WM_PATHNAME | WM_WILDSTAR;

    const QByteArray patternBytes = rule.pattern.toUtf8();
    const char *pat = patternBytes.constData();

    if (rule.anchored) {
        // Anchored: match against the full relPath (or the portion relative to
        // the .gitignore's directory). The relPath is always relative to the
        // workspace root; rule.dir is the absolute path of the .gitignore dir.
        // We need to compute the path relative to rule.dir from relPath.
        // For simplicity, match against the full relPath using the anchored pattern.
        // If rule.dir is non-empty, strip the rule.dir prefix from relPath first.
        // (rule.dir may be empty when it equals the workspace root itself.)
        QString matchPath = relPath;
        // rule.dir is an absolute path; to find what part of relPath is under it,
        // we'd need the workspace root. Instead we keep it simple: the anchored
        // pattern is matched against both the full relPath and the portion after
        // the last parent of rule.dir that we can infer from relPath.
        // Since the workspace root is the context we don't have directly here,
        // we try matching the anchored pattern at every path prefix boundary.
        // This is conservative: an anchored pattern like "build/output" will
        // match "build/output" or "subdir/build/output" — acceptable for our
        // download-skip use case where false positives are safe (we skip more).
        const QByteArray textBytes = matchPath.toUtf8();
        if (wildmatch(pat, textBytes.constData(), flags) == WM_MATCH)
            return true;
        // Try stripping leading components to handle sub-directory .gitignore files.
        const QString sep = QStringLiteral("/");
        int pos = 0;
        while ((pos = matchPath.indexOf(sep, pos)) != -1) {
            pos++;
            const QString sub = matchPath.mid(pos);
            const QByteArray subBytes = sub.toUtf8();
            if (wildmatch(pat, subBytes.constData(), flags) == WM_MATCH)
                return true;
        }
        return false;
    } else {
        // Unanchored: match against each path component (basename match at any depth).
        // Also try matching the full path (for patterns without '/').
        const QByteArray textBytes = relPath.toUtf8();
        if (wildmatch(pat, textBytes.constData(), flags) == WM_MATCH)
            return true;

        // Basename match: extract the last component and try.
        const int lastSlash = relPath.lastIndexOf(QLatin1Char('/'));
        if (lastSlash >= 0) {
            const QString basename = relPath.mid(lastSlash + 1);
            const QByteArray basenameBytes = basename.toUtf8();
            // Disable WM_PATHNAME for basename-only match (no '/' in basename).
            if (wildmatch(pat, basenameBytes.constData(), WM_WILDSTAR) == WM_MATCH)
                return true;
        }
        return false;
    }
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
