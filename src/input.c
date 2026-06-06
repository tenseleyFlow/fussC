#include "input.h"

#include "term.h"

action input_classify(int key)
{
	action a = {ACT_NONE, 0};

	switch (key) {
	case KEY_EOF:
		a.kind = ACT_QUIT;
		return a;
	case KEY_RESIZE:
		a.kind = ACT_REDRAW;
		return a;
	case KEY_UP:
	case KEY_CTRL('P'):
		a.kind = ACT_UP;
		return a;
	case KEY_DOWN:
	case KEY_CTRL('N'):
		a.kind = ACT_DOWN;
		return a;
	case KEY_LEFT:
	case KEY_CTRL('B'):
		a.kind = ACT_LEFT;
		return a;
	case KEY_RIGHT:
	case KEY_CTRL('F'):
		a.kind = ACT_RIGHT;
		return a;
	case KEY_HOME:
		a.kind = ACT_HOME;
		return a;
	case KEY_END:
		a.kind = ACT_END;
		return a;
	case KEY_CTRL('Q'):
		a.kind = ACT_QUIT;
		return a;
	case KEY_ESC:
		a.kind = ACT_FILTER_CLEAR;
		return a;
	case KEY_BACKSPACE:
		a.kind = ACT_FILTER_BACKSPACE;
		return a;
	case KEY_ENTER:
	case KEY_TAB:
	case KEY_DELETE:
	case KEY_UNKNOWN:
		return a; /* reserved / ignored */
	}

	if (key == ' ') {
		a.kind = ACT_TOGGLE;
		return a;
	}
	if (key == 'Q') {
		a.kind = ACT_QUIT;
		return a;
	}
	if (key == 'H') { /* Hidden: toggle dotfiles (off '.', which filters) */
		a.kind = ACT_TOGGLE_DOTFILES;
		return a;
	}
	if (key >= 'A' && key <= 'Z') {
		a.kind = ACT_COMMAND; /* dispatched in Sprint 4 */
		a.cp = (uint32_t)key;
		return a;
	}
	/* Any other printable codepoint (lowercase, digits, punctuation, UTF-8)
	 * feeds the filter. */
	if (key >= 0x20 && key < KEY_SPECIAL_BASE) {
		a.kind = ACT_FILTER_PUSH;
		a.cp = (uint32_t)key;
		return a;
	}
	return a;
}
