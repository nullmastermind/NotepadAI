/*
 * Minimal git-compat-util.h shim for vendored xdiff.
 *
 * Replaces the full git-compat-util.h from git/git, providing only what xdiff
 * itself uses: standard C headers, the x*alloc family, and a BUG() macro.
 *
 * Everything else from git's compat layer (signal handling, errno wrappers,
 * trace2, strbuf, hashmap, etc.) is intentionally absent — xdiff doesn't
 * reach for any of it.
 */
#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* xdiff declares `regex_t **ignore_regex` in xpparam_t. The full POSIX
 * <regex.h> isn't available on Windows MSVC, and we never populate
 * ignore_regex (we don't ship the -G/--ignore-regex equivalent), so opaque
 * forward declarations + stub of regexec_buf are enough to make the code
 * type-check and link. The runtime branch is dead under our usage:
 * record_matches_regex() is only called when ignore_regex_nr > 0. */
typedef struct xdl_regex_opaque regex_t;
typedef struct { int rm_so, rm_eo; } regmatch_t;
static inline int regexec_buf(const regex_t *preg, const char *buf,
                              size_t size, size_t nmatch,
                              regmatch_t *pmatch, int eflags)
{
    (void)preg; (void)buf; (void)size;
    (void)nmatch; (void)pmatch; (void)eflags;
    return 1; /* never-matches; xdiff treats non-zero as "no match" */
}

/* xdiff tags some parameters with UNUSED (git's attribute spelling). */
#if defined(__GNUC__) || defined(__clang__)
#  define UNUSED __attribute__((unused))
#else
#  define UNUSED
#endif

/* xdiff allocates via these wrappers. We forward to the C runtime; OOM is
 * propagated to callers by xdiff via the XDL_ALLOC_ARRAY / XDL_CALLOC_ARRAY
 * NULL checks (see xmacros.h). The runtime path here is already O(1). */
static inline void *xmalloc(size_t sz)            { return malloc(sz); }
static inline void *xrealloc(void *p, size_t sz)  { return realloc(p, sz); }
static inline void *xcalloc(size_t n, size_t sz)  { return calloc(n, sz); }

/* xdiff's BUG() fires only on internal invariant violations (group-sync
 * sequencing in xdiffi.c). Crash loudly so a CI failure pinpoints the line —
 * we never want to silently corrupt diff output. */
#define BUG(fmt, ...)                                                      \
    do {                                                                   \
        fprintf(stderr, "xdiff BUG: " fmt "\n", ##__VA_ARGS__);            \
        abort();                                                           \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* GIT_COMPAT_UTIL_H */
