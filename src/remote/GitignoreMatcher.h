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

#ifndef REMOTE_GITIGNORE_MATCHER_H
#define REMOTE_GITIGNORE_MATCHER_H

#include <QString>
#include <QStringList>
#include <QList>

namespace remote {

// A single parsed gitignore rule.
struct GitignoreRule
{
    QString pattern;    // The raw pattern string (after stripping leading '!')
    bool negated;       // True if the rule started with '!'
    bool dirOnly;       // True if the pattern ended with '/'
    bool anchored;      // True if the pattern contains '/' (after stripping trailing '/')
    bool wildstar;      // True if pattern contains '**'
    QString dir;        // The directory this rule belongs to (absolute POSIX path)
};

/*
 * GitignoreMatcher — Parses gitignore-style rules and tests paths against them.
 *
 * Usage:
 *   GitignoreMatcher m;
 *   m.addRules("/home/user/project", rulesText);   // parse .gitignore content
 *   if (m.isIgnored("build/output.o", false)) { ... }
 *
 * Supported gitignore features:
 *   - Comments (#) and blank lines ignored
 *   - Negation (!pattern)
 *   - Trailing '/' means directory-only match
 *   - '**' anywhere matches zero or more path components
 *   - Anchored patterns (contain '/') match from the root of the workspace
 *   - Unanchored patterns match the basename at any depth
 *   - Nested .gitignore files (child rules added with their own dirPath)
 *
 * Not supported:
 *   - .git/info/exclude, global gitignore, core.excludesFile
 *   - Character classes ([...]) in patterns (delegated to wildmatch which handles them)
 */
class GitignoreMatcher
{
public:
    GitignoreMatcher() = default;

    // Parse and store all rules from `rulesText` (the content of a .gitignore file).
    // `dirPath` is the absolute POSIX path of the directory containing that .gitignore.
    // May be called multiple times with different directories — rules accumulate and
    // are evaluated in order (later rules override earlier ones, per gitignore semantics).
    void addRules(const QString &dirPath, const QString &rulesText);

    // Test whether a relative POSIX path (relative to the workspace root) should be
    // ignored. `isDir` must be true if the path is a directory (for dirOnly rules).
    // Returns true if the path is ignored (not excluded by a negation rule).
    bool isIgnored(const QString &relPath, bool isDir) const;

    // Remove all rules.
    void clear();

    // Returns true if no rules have been added.
    bool isEmpty() const { return m_rules.isEmpty(); }

private:
    QList<GitignoreRule> m_rules;

    // Match a single rule against the relative path.
    bool matchRule(const GitignoreRule &rule, const QString &relPath, bool isDir) const;
};

} // namespace remote

#endif // REMOTE_GITIGNORE_MATCHER_H
