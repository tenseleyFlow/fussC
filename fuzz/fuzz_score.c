/*
 * libFuzzer harness for the fuzzy scorer. The input is split on its first NUL
 * byte into (pattern, text); both are NUL-terminated before scoring. Catches
 * out-of-bounds reads / overflows in fuzzy_score on arbitrary byte strings,
 * including multi-byte and pathological inputs.
 *
 *   make fuzz   (clang + libFuzzer; see the Makefile)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzzy.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const uint8_t *sep = memchr(data, 0, size);
	size_t plen = sep ? (size_t)(sep - data) : size;
	size_t tstart = sep ? plen + 1 : size;
	size_t tlen = size - tstart;

	char *pat = malloc(plen + 1);
	char *text = malloc(tlen + 1);
	if (plen)
		memcpy(pat, data, plen);
	pat[plen] = '\0';
	if (tlen)
		memcpy(text, data + tstart, tlen);
	text[tlen] = '\0';

	(void)fuzzy_score(pat, text);

	free(pat);
	free(text);
	return 0;
}
