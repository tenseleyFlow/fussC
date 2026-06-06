#include "term.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Alternate screen + cursor visibility. */
#define ALT_SCREEN_ON  "\033[?1049h"
#define ALT_SCREEN_OFF "\033[?1049l"
#define CURSOR_HIDE    "\033[?25l"
#define CURSOR_SHOW    "\033[?25h"

static struct termios saved_termios;
static bool active = false; /* true while raw mode + alt screen are in effect */

static void write_all(const char *s)
{
	size_t len = 0;
	while (s[len] != '\0')
		len++;
	/* Best-effort; nothing useful to do if the terminal write fails. */
	if (write(STDOUT_FILENO, s, len) < 0)
		(void)0;
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
	/* Re-raise with the default handler so the exit status reflects the
	 * signal, matching what a caller's shell expects. */
	signal(sig, SIG_DFL);
	raise(sig);
}

bool term_init(void)
{
	struct termios raw;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
		return false;

	if (tcgetattr(STDIN_FILENO, &saved_termios) < 0)
		return false;

	raw = saved_termios;
	/* Raw mode: no canonical line buffering, no echo, no signal-generating
	 * keys translated, no CR/NL mangling. One byte minimum per read, no
	 * inter-byte timer. */
	raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= (tcflag_t)~(OPOST);
	raw.c_cflag |= (tcflag_t)(CS8);
	raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
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

	return true;
}

int term_read_byte(void)
{
	unsigned char c;
	ssize_t n = read(STDIN_FILENO, &c, 1);
	if (n == 1)
		return (int)c;
	return -1;
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
