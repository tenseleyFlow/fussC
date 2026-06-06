#ifndef FUSSY_WIDTH_H
#define FUSSY_WIDTH_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal, locale-independent display-width support for UTF-8 filenames.
 * Hand-rolled (not the system wcwidth, which varies by platform/locale) so the
 * interactive renderer can place the cursor and size highlights
 * deterministically.
 */

/* Decode the UTF-8 sequence at `s` (NUL-terminated); store the codepoint in
 * *cp. Returns bytes consumed (1-4). Invalid/truncated bytes consume 1 and
 * yield the raw byte as the codepoint. Never reads past the terminator. */
int utf8_decode(const char *s, uint32_t *cp);

/* Display columns for one codepoint: 0 (combining/zero-width), 2 (wide),
 * else 1. */
int cp_width(uint32_t cp);

/* Sum of cp_width over a UTF-8 string. */
size_t display_width(const char *s);

#endif /* FUSSY_WIDTH_H */
