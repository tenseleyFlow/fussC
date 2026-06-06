#include "term.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Alternate screen + cursor visibility. */
#define ALT_SCREEN_ON  "\033[?1049h"
#define ALT_SCREEN_OFF "\033[?1049l"
#define CURSOR_HIDE    "\033[?25l"
#define CURSOR_SHOW    "\033[?25h"

/* How long to wait for the tail of an escape sequence before deciding a lone
 * ESC was pressed. */
#define ESC_TIMEOUT_MS 30

static struct termios saved_termios;
static bool active = false; /* true while raw mode + alt screen are in effect */
static volatile sig_atomic_t resized = 0;

static void write_all(const char *s)
{
	size_t len = 0;
	while (s[len] != '\0')
		len++;
	if (write(STDOUT_FILENO, s, len) < 0)
		(void)0; /* best effort */
}

void term_restore(void)
{
	if (!active)
		return;
	active = false;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
	write_all(CURSOR_SHOW);
	write_all(ALT_SCREEN_OFF);
}

static void on_signal(int sig)
{
	term_restore();
	signal(sig, SIG_DFL);
	raise(sig);
}

static void on_winch(int sig)
{
	(void)sig;
	resized = 1;
}

bool term_init(void)
{
	struct termios raw;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
		return false;

	if (tcgetattr(STDIN_FILENO, &saved_termios) < 0)
		return false;

	raw = saved_termios;
	raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= (tcflag_t) ~(OPOST);
	raw.c_cflag |= (tcflag_t)(CS8);
	raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
		return false;

	active = true;
	write_all(ALT_SCREEN_ON);
	write_all(CURSOR_HIDE);

	atexit(term_restore);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);

	/* WINCH via sigaction without SA_RESTART so a resize interrupts poll().
	 */
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = on_winch;
	sigaction(SIGWINCH, &sa, NULL);

	return true;
}

void term_resume(void)
{
	if (active)
		return;
	struct termios raw = saved_termios;
	raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= (tcflag_t) ~(OPOST);
	raw.c_cflag |= (tcflag_t)(CS8);
	raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	active = true;
	write_all(ALT_SCREEN_ON);
	write_all(CURSOR_HIDE);
}

bool term_take_resize(void)
{
	if (resized) {
		resized = 0;
		return true;
	}
	return false;
}

void term_size(int *rows, int *cols)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
		*rows = ws.ws_row;
		*cols = ws.ws_col;
	} else {
		*rows = 24;
		*cols = 80;
	}
}

int key_decode(int b0, key_byte_fn next, void *ctx)
{
	if (b0 < 0)
		return KEY_EOF;

	if (b0 == 0x1B) { /* ESC: lone key or sequence introducer */
		int b1 = next(ctx, ESC_TIMEOUT_MS);
		if (b1 < 0)
			return KEY_ESC;
		if (b1 == '[' || b1 == 'O') {
			int b2 = next(ctx, ESC_TIMEOUT_MS);
			switch (b2) {
			case 'A':
				return KEY_UP;
			case 'B':
				return KEY_DOWN;
			case 'C':
				return KEY_RIGHT;
			case 'D':
				return KEY_LEFT;
			case 'H':
				return KEY_HOME;
			case 'F':
				return KEY_END;
			case '3': {
				int b3 = next(ctx, ESC_TIMEOUT_MS);
				return b3 == '~' ? KEY_DELETE : KEY_UNKNOWN;
			}
			default:
				return KEY_UNKNOWN;
			}
		}
		return KEY_UNKNOWN; /* Alt-<b1>: unused for now */
	}

	if (b0 == 0x7F || b0 == 0x08)
		return KEY_BACKSPACE;
	if (b0 == '\r' || b0 == '\n')
		return KEY_ENTER;
	if (b0 == '\t')
		return KEY_TAB;
	if (b0 < 0x80)
		return b0; /* printable ASCII, or Ctrl-<letter> as raw 1..26 */

	/* UTF-8 multibyte: gather continuation bytes into a codepoint. */
	int need;
	uint32_t cp;
	if ((b0 & 0xE0) == 0xC0) {
		need = 1;
		cp = (uint32_t)(b0 & 0x1F);
	} else if ((b0 & 0xF0) == 0xE0) {
		need = 2;
		cp = (uint32_t)(b0 & 0x0F);
	} else if ((b0 & 0xF8) == 0xF0) {
		need = 3;
		cp = (uint32_t)(b0 & 0x07);
	} else {
		return KEY_UNKNOWN;
	}
	for (int i = 0; i < need; i++) {
		int c = next(ctx, ESC_TIMEOUT_MS);
		if (c < 0 || (c & 0xC0) != 0x80)
			return KEY_UNKNOWN;
		cp = (cp << 6) | (uint32_t)(c & 0x3F);
	}
	return (int)cp;
}

/* Byte reader backed by stdin, with a poll timeout for sequence tails. */
static int stdin_byte(void *ctx, int timeout_ms)
{
	(void)ctx;
	struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
	if (poll(&p, 1, timeout_ms) <= 0)
		return -1;
	unsigned char c;
	return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}

int term_read_key(void)
{
	unsigned char c;
	ssize_t n = read(STDIN_FILENO, &c, 1);
	if (n != 1) {
		if (n < 0 && errno == EINTR && resized) {
			resized = 0;
			return KEY_RESIZE;
		}
		return KEY_EOF;
	}
	return key_decode(c, stdin_byte, NULL);
}
