#ifndef FUSSY_UTIL_H
#define FUSSY_UTIL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Allocation wrappers that abort on out-of-memory. fussy is a short-lived
 * interactive tool; on a failed allocation there is nothing useful to do but
 * report and exit, so callers never have to null-check these.
 *
 * RETURNS_NONNULL makes that contract machine-checkable: the compiler and the
 * static analyzer know the result is never NULL, which kills false-positive
 * null-dereference reports at every call site. It is a no-op where unsupported.
 */
#if defined(__GNUC__) || defined(__clang__)
#define RETURNS_NONNULL __attribute__((returns_nonnull))
#else
#define RETURNS_NONNULL
#endif

RETURNS_NONNULL void *xmalloc(size_t n);
RETURNS_NONNULL void *xrealloc(void *p, size_t n);
RETURNS_NONNULL char *xstrdup(const char *s);

/* Monotonic clock in nanoseconds (CLOCK_MONOTONIC), for the filter timeout. */
uint64_t mono_ns(void);

/* Encode a codepoint as UTF-8 into out[0..3]; returns the byte count (1-4). */
int utf8_encode(uint32_t cp, char out[4]);

/* Split `text` into heap line strings on '\n' (a trailing '\r' is trimmed);
 * *n_out gets the count. An empty input yields zero lines. Free with
 * str_free_lines. */
char **str_split_lines(const char *text, int *n_out);
void str_free_lines(char **lines, int n);

#endif /* FUSSY_UTIL_H */
