#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void die_oom(void)
{
	fputs("fussy: out of memory\n", stderr);
	abort();
}

void *xmalloc(size_t n)
{
	void *p = malloc(n);
	if (p == NULL && n != 0)
		die_oom();
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n);
	if (q == NULL && n != 0)
		die_oom();
	return q;
}

char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = xmalloc(n);
	memcpy(p, s, n);
	return p;
}

uint64_t mono_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
