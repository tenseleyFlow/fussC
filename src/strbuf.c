#include "strbuf.h"

#include <string.h>

#include "util.h"

void sb_put(strbuf *s, const char *str)
{
	size_t n = strlen(str);
	if (s->buf == NULL || s->len + n + 1 > s->cap) {
		while (s->len + n + 1 > s->cap)
			s->cap = s->cap ? s->cap * 2 : 256;
		s->buf = xrealloc(s->buf, s->cap);
	}
	memcpy(s->buf + s->len, str, n);
	s->len += n;
	s->buf[s->len] = '\0';
}

void sb_putc(strbuf *s, char c)
{
	if (s->buf == NULL || s->len + 2 > s->cap) {
		s->cap = s->cap ? s->cap * 2 : 64;
		s->buf = xrealloc(s->buf, s->cap);
	}
	s->buf[s->len++] = c;
	s->buf[s->len] = '\0';
}

void sb_putn(strbuf *s, const char *str, size_t n)
{
	for (size_t i = 0; i < n; i++)
		sb_putc(s, str[i]);
}
