#ifndef FUSSY_UTIL_H
#define FUSSY_UTIL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Allocation wrappers that abort on out-of-memory. fussy is a short-lived
 * interactive tool; on a failed allocation there is nothing useful to do but
 * report and exit, so callers never have to null-check these.
 */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Monotonic clock in nanoseconds (CLOCK_MONOTONIC), for the filter timeout. */
uint64_t mono_ns(void);

#endif /* FUSSY_UTIL_H */
