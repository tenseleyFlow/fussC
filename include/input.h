#ifndef FUSSY_INPUT_H
#define FUSSY_INPUT_H

#include <stdint.h>

/*
 * Key -> action mapping for the main tree view, implementing the case-is-the-
 * mode model (see architecture.md): lowercase/printable filters, UPPERCASE is a
 * command, arrows/Ctrl navigate. Pure and testable; main applies the actions.
 */
typedef enum {
	ACT_NONE,
	ACT_QUIT,
	ACT_UP,
	ACT_DOWN,
	ACT_LEFT,
	ACT_RIGHT,
	ACT_TOGGLE,
	ACT_HOME,
	ACT_END,
	ACT_FILTER_PUSH, /* cp holds the codepoint to append */
	ACT_FILTER_BACKSPACE,
	ACT_FILTER_CLEAR,
	ACT_TOGGLE_DOTFILES,
	ACT_COMMAND, /* cp holds the uppercase command letter (Sprint 4) */
	ACT_REDRAW,
} action_kind;

typedef struct {
	action_kind kind;
	uint32_t cp;
} action;

action input_classify(int key);

#endif /* FUSSY_INPUT_H */
