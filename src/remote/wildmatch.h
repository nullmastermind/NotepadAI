/*
 * wildmatch.h — BSD-licensed glob matching with ** support.
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

#ifndef REMOTE_WILDMATCH_H
#define REMOTE_WILDMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Return value when the pattern does NOT match. */
#define WM_NOMATCH 1
/* Return value when the pattern DOES match. */
#define WM_MATCH   0

/* Flags */
#define WM_CASEFOLD  0x01   /* Case-insensitive matching */
#define WM_PATHNAME  0x02   /* '/' in text must be matched by '/' in pattern (not '*' or '?') */
#define WM_WILDSTAR  0x04   /* Enable '**' to match across path separators */

/*
 * wildmatch() — match `text` against shell glob `pattern`.
 *
 * Returns WM_MATCH (0) on match, WM_NOMATCH (1) on no match.
 *
 * Supports:
 *   *       any sequence of non-'/' characters (with WM_PATHNAME) or any chars
 *   ?       any single character
 *   [...]   character class
 *   **      (with WM_WILDSTAR) matches any sequence including '/'
 */
int wildmatch(const char *pattern, const char *text, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_WILDMATCH_H */
