#include "term.h"
#include "test.h"

#include <string.h>

/* Mock byte source: hands out a fixed buffer, then signals "nothing more". */
struct mockbuf {
	const unsigned char *data;
	size_t len;
	size_t pos;
};

static int mock_next(void *ctx, int timeout_ms)
{
	(void)timeout_ms;
	struct mockbuf *m = ctx;
	if (m->pos >= m->len)
		return -1;
	return m->data[m->pos++];
}

static int decode(int b0, const char *tail)
{
	struct mockbuf m = {(const unsigned char *)tail,
	                    tail ? strlen(tail) : 0, 0};
	return key_decode(b0, mock_next, &m);
}

void test_key_arrows(void)
{
	/* CSI form (ESC [ X) and application form (ESC O X). */
	CHECK(decode(0x1B, "[A") == KEY_UP);
	CHECK(decode(0x1B, "[B") == KEY_DOWN);
	CHECK(decode(0x1B, "[C") == KEY_RIGHT);
	CHECK(decode(0x1B, "[D") == KEY_LEFT);
	CHECK(decode(0x1B, "OA") == KEY_UP);
	CHECK(decode(0x1B, "[H") == KEY_HOME);
	CHECK(decode(0x1B, "[F") == KEY_END);
	CHECK(decode(0x1B, "[3~") == KEY_DELETE);
}

void test_key_lone_esc(void)
{
	/* ESC with no tail within the timeout is the Escape key. */
	CHECK(decode(0x1B, "") == KEY_ESC);
	/* ESC followed by an unrecognised tail is not a crash. */
	CHECK(decode(0x1B, "[Z") == KEY_UNKNOWN);
}

void test_key_controls(void)
{
	CHECK(decode(14, NULL) == KEY_CTRL('N'));
	CHECK(decode(16, NULL) == KEY_CTRL('P'));
	CHECK(decode(6, NULL) == KEY_CTRL('F'));
	CHECK(decode(2, NULL) == KEY_CTRL('B'));
	CHECK(decode(17, NULL) == KEY_CTRL('Q'));
	CHECK(decode(0x7F, NULL) == KEY_BACKSPACE);
	CHECK(decode(0x08, NULL) == KEY_BACKSPACE);
	CHECK(decode('\r', NULL) == KEY_ENTER);
	CHECK(decode('\n', NULL) == KEY_ENTER);
	CHECK(decode('\t', NULL) == KEY_TAB);
}

void test_key_printable(void)
{
	CHECK(decode('a', NULL) == 'a');
	CHECK(decode('Z', NULL) == 'Z');
	CHECK(decode('.', NULL) == '.');
	/* UTF-8: é (C3 A9) -> U+00E9, 中 (E4 B8 AD) -> U+4E2D. */
	CHECK(decode(0xC3, "\251") == 0xE9);
	CHECK(decode(0xE4, "\270\255") == 0x4E2D);
	/* Truncated multibyte does not run off the buffer. */
	CHECK(decode(0xE4, "\270") == KEY_UNKNOWN);
}
