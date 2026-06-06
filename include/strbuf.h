#ifndef FUSSY_STRBUF_H
#define FUSSY_STRBUF_H

#include <stddef.h>

/*
 * Growable, always-NUL-terminated byte builder used by the renderers. Zero-
 * initialise it (`strbuf s = {0};`); the buffer is heap-allocated on first
 * append and doubles as needed. The caller owns and frees `buf`.
 */
typedef struct {
	char *buf;
	size_t len;
	size_t cap;
} strbuf;

void sb_put(strbuf *s, const char *str);            /* append a C string */
void sb_putc(strbuf *s, char c);                    /* append one byte */
void sb_putn(strbuf *s, const char *str, size_t n); /* append n bytes */

#endif /* FUSSY_STRBUF_H */
