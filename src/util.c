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

/* A 0-size request is bumped to 1 so the result is always a unique, non-NULL,
 * freeable pointer. That makes the `returns_nonnull` contract sound for every
 * input (malloc(0)/realloc(_,0) may legally return NULL, which would otherwise
 * break it) and spares callers a size==0 special case. */
void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (p == NULL)
		die_oom();
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (q == NULL)
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

int utf8_encode(uint32_t cp, char out[4])
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

char **str_split_lines(const char *text, int *n_out)
{
	char **lines = NULL;
	int n = 0, cap = 0;
	const char *p = text;
	while (*p != '\0') {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		if (len > 0 && p[len - 1] == '\r')
			len--;
		if (n == cap) {
			cap = cap ? cap * 2 : 16;
			lines = xrealloc(lines, (size_t)cap * sizeof(*lines));
		}
		char *line = xmalloc(len + 1);
		memcpy(line, p, len);
		line[len] = '\0';
		lines[n++] = line;
		if (!nl)
			break;
		p = nl + 1;
	}
	*n_out = n;
	return lines;
}

void str_free_lines(char **lines, int n)
{
	for (int i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
}
