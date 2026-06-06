/*
 * libFuzzer harness for the key/escape-sequence decoder. data[0] is the first
 * byte; the rest is the follow-on stream the decoder pulls via its callback
 * (escape-sequence tails, UTF-8 continuations). Exercises the ESC/CSI state
 * machine and the multi-byte UTF-8 gathering against arbitrary bytes.
 */
#include <stddef.h>
#include <stdint.h>

#include "term.h"

struct cursor {
	const uint8_t *data;
	size_t len;
	size_t pos;
};

static int next_byte(void *ctx, int timeout_ms)
{
	(void)timeout_ms;
	struct cursor *c = ctx;
	if (c->pos >= c->len)
		return -1;
	return c->data[c->pos++];
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size == 0)
		return 0;
	struct cursor c = {data + 1, size - 1, 0};
	(void)key_decode(data[0], next_byte, &c);
	return 0;
}
