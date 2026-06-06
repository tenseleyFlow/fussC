#ifndef FUSSY_TERM_H
#define FUSSY_TERM_H

#include <stdbool.h>

/*
 * Terminal control: raw mode + alternate screen. A later sprint adds the
 * full escape-sequence key parser and an incremental diff renderer; for now
 * this is enough to enter, read a key, and leave cleanly on every exit path.
 */

/* Enter raw mode and the alternate screen. Returns false if stdin/stdout is
 * not a TTY (caller should fall back to non-interactive output). Installs
 * atexit and signal handlers so the terminal is always restored. */
bool term_init(void);

/* Restore the terminal to its original state. Idempotent. */
void term_restore(void);

/* Block for one input byte. Returns the byte, or -1 on EOF/error. This is a
 * placeholder; the escape-sequence decoder lands in the navigation sprint. */
int term_read_byte(void);

/* Current terminal size in character cells. */
void term_size(int *rows, int *cols);

#endif /* FUSSY_TERM_H */
