/*
 * wildmatch.c — BSD-licensed glob matching with ** support.
 *
 * Derived from davvid/wildmatch (https://github.com/davvid/wildmatch),
 * which is itself derived from the wildmatch implementation in git.git.
 *
 * Copyright (c) 2014 David Aguilar
 * Copyright (c) 2008, 2009 Apple Inc.
 * Copyright (c) 2000 the Portable OpenSSH contributors
 * Copyright (c) 1989, 1993 The Regents of the University of California
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "wildmatch.h"

#include <string.h>
#include <ctype.h>

#define WILDSEP '/'

/* Lower-case a byte for case-insensitive comparison. */
static int wm_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

/* Compare two chars respecting CASEFOLD flag. */
static int wm_charcmp(char a, char b, unsigned int flags)
{
    if (flags & WM_CASEFOLD)
        return wm_tolower((unsigned char)a) - wm_tolower((unsigned char)b);
    return (unsigned char)a - (unsigned char)b;
}

/*
 * Match a bracket expression [...] at `*pp` against char `c`.
 * Advances *pp past the closing ']'.
 * Returns 1 on match, 0 on no match, -1 on malformed expression (treat as literal).
 */
static int match_bracket(const char **pp, char c, unsigned int flags)
{
    const char *p = *pp;
    int negate = 0;
    int matched = 0;

    if (*p == '!' || *p == '^') {
        negate = 1;
        p++;
    }

    /* Empty bracket is a literal — treat '[' as literal if no ']' found */
    if (*p == ']') {
        /* ']' as first char after '['/'!' is a literal member */
        matched |= (wm_charcmp(c, ']', flags) == 0);
        p++;
    }

    while (*p && *p != ']') {
        if (p[1] == '-' && p[2] != ']' && p[2] != '\0') {
            /* Range a-z */
            char lo = p[0], hi = p[2];
            if (flags & WM_CASEFOLD) {
                lo = (char)wm_tolower((unsigned char)lo);
                hi = (char)wm_tolower((unsigned char)hi);
                char cc = (char)wm_tolower((unsigned char)c);
                if (cc >= lo && cc <= hi) matched = 1;
            } else {
                if ((unsigned char)c >= (unsigned char)lo &&
                    (unsigned char)c <= (unsigned char)hi)
                    matched = 1;
            }
            p += 3;
        } else {
            if (wm_charcmp(c, *p, flags) == 0) matched = 1;
            p++;
        }
    }

    if (*p != ']') {
        /* Malformed: no closing bracket — caller treats '[' as literal */
        return -1;
    }
    *pp = p + 1; /* skip past ']' */
    return negate ? !matched : matched;
}

/*
 * Core recursive matching function.
 * `pattern` and `text` are positioned at the start of what remains to match.
 * `text_start` is the original start of the text (for anchoring checks).
 */
static int dowild(const char *pattern, const char *text, unsigned int flags)
{
    char p, t;

    for ( ; (p = *pattern) != '\0'; pattern++, text++) {
        t = *text;

        /* With WM_PATHNAME, '/' in text must be matched by '/' in pattern. */
        if ((flags & WM_PATHNAME) && t == WILDSEP && p != WILDSEP
            && !(p == '*' && (flags & WM_WILDSTAR)))
            return WM_NOMATCH;

        switch (p) {
        case '?':
            if (t == '\0') return WM_NOMATCH;
            if ((flags & WM_PATHNAME) && t == WILDSEP) return WM_NOMATCH;
            break;

        case '*':
            /* Check for ** wildstar */
            if ((flags & WM_WILDSTAR) && pattern[1] == '*') {
                /* Skip all consecutive *'s */
                const char *pp = pattern;
                while (*pp == '*') pp++;

                /* If ** is at the end of pattern, match everything remaining */
                if (*pp == '\0') return WM_MATCH;

                /* If ** is followed by '/', try matching zero or more path components */
                if (*pp == WILDSEP) {
                    const char *next_pat = pp + 1;
                    /* Try matching the remaining text at every '/' boundary */
                    const char *tp = text;
                    while (1) {
                        if (dowild(next_pat, tp, flags) == WM_MATCH)
                            return WM_MATCH;
                        if (*tp == '\0') break;
                        /* Advance to next '/' or end */
                        while (*tp != '\0' && *tp != WILDSEP) tp++;
                        if (*tp == WILDSEP) tp++;
                    }
                    return WM_NOMATCH;
                }
                /* ** not followed by '/' — treat like single * */
                pattern = pp - 1; /* fall through to single-* handling */
            }

            /* Single '*': match zero or more non-'/' chars (with WM_PATHNAME) */
            {
                const char *next_pat = pattern + 1;
                /* Skip adjacent *'s */
                while (*next_pat == '*') next_pat++;

                if (*next_pat == '\0') {
                    /* * at end of pattern: match remaining text */
                    if (flags & WM_PATHNAME) {
                        /* Cannot match '/' */
                        if (strchr(text, WILDSEP)) return WM_NOMATCH;
                    }
                    return WM_MATCH;
                }

                /* Try matching at each position */
                while (1) {
                    if (dowild(next_pat, text, flags) == WM_MATCH)
                        return WM_MATCH;
                    if (*text == '\0') break;
                    if ((flags & WM_PATHNAME) && *text == WILDSEP) break;
                    text++;
                }
                return WM_NOMATCH;
            }

        case '[': {
            const char *bracket_start = pattern + 1;
            int result = match_bracket(&bracket_start, t, flags);
            if (result < 0) {
                /* Malformed bracket — treat '[' as literal */
                if (wm_charcmp(p, t, flags) != 0) return WM_NOMATCH;
            } else {
                if (!result) return WM_NOMATCH;
                pattern = bracket_start - 1; /* -1 because loop does pattern++ */
            }
            break;
        }

        case '\\':
            /* Escape: match the next character literally */
            p = *++pattern;
            if (p == '\0') return WM_NOMATCH;
            if (wm_charcmp(p, t, flags) != 0) return WM_NOMATCH;
            break;

        default:
            if (wm_charcmp(p, t, flags) != 0) return WM_NOMATCH;
            break;
        }

        if (t == '\0') return WM_NOMATCH;
    }

    return (*text == '\0') ? WM_MATCH : WM_NOMATCH;
}

int wildmatch(const char *pattern, const char *text, unsigned int flags)
{
    if (!pattern || !text) return WM_NOMATCH;
    return dowild(pattern, text, flags);
}
