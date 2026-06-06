#ifndef FUSSY_TERM_H
#define FUSSY_TERM_H

#include <stdbool.h>

/*
 * Terminal control: raw mode, alternate screen, and key input. Keys are decoded
 * from raw bytes (including escape sequences) into the codes below.
 */

/* Enter raw mode and the alternate screen. Returns false if stdin/stdout is
 * not a TTY (caller should fall back to non-interactive output). Installs
 * atexit and signal handlers so the terminal is always restored. */
bool term_init(void);

/* Restore the terminal to its original state. Idempotent. */
void term_restore(void);

/* Current terminal size in character cells. */
void term_size(int *rows, int *cols);

/*
 * Key codes. Printable input is returned as its Unicode codepoint
 * (0..0x10FFFF). Ctrl-<letter> is the raw control value 1..26 (use
 * KEY_CTRL('N')). Everything else uses the special values below.
 */
enum {
	KEY_EOF = -1,
	KEY_SPECIAL_BASE = 0x110000, /* just past the last Unicode codepoint */
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_HOME,
	KEY_END,
	KEY_DELETE,
	KEY_ENTER,
	KEY_BACKSPACE,
	KEY_TAB,
	KEY_ESC,
	KEY_RESIZE, /* terminal was resized (SIGWINCH) */
	KEY_UNKNOWN,
};

#define KEY_CTRL(ch) ((ch) - '@') /* KEY_CTRL('N') == 14 */

/* Byte source for the decoder: returns 0..255, or -1 if none arrives within
 * timeout_ms. Lets key_decode be unit-tested against a fixed buffer. */
typedef int (*key_byte_fn)(void *ctx, int timeout_ms);

/* Decode one key given its first byte b0 and a reader for any follow-on bytes
 * (escape-sequence tails, UTF-8 continuations). Pure: no global state. */
int key_decode(int b0, key_byte_fn next, void *ctx);

/* Block for and return the next key from stdin. Returns KEY_RESIZE when a
 * SIGWINCH interrupts the wait, KEY_EOF on end of input. */
int term_read_key(void);

#endif /* FUSSY_TERM_H */
