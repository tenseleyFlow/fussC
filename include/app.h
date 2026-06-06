#ifndef FUSSY_APP_H
#define FUSSY_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flatten.h"
#include "tree.h"

#define FILTER_MAX 128

/*
 * Interactive view state. The tree and visible list live here; the renderer
 * derives the on-screen window from `selected` each frame (no persistent
 * scroll state). The filter buffer accumulates typed lowercase input - the
 * fuzzy engine that consumes it lands in Sprint 3.
 */
typedef struct {
	tree t;
	flat_list visible;
	uint32_t selected; /* index into visible.rows */
	bool hide_dotfiles;
	char filter[FILTER_MAX];
	size_t filter_len;
} app;

void app_init(app *a);
void app_free(app *a);

/* Re-flatten honoring hide_dotfiles, preserving the selected node when it is
 * still visible (else clamp). Call after the tree changes or dotfiles toggle.
 */
void app_reflatten(app *a);

/* The arena index of the selected node, or NODE_NIL when the list is empty. */
uint32_t app_selected_node(const app *a);

/* Navigation (see architecture.md "Navigation semantics"). */
void app_down(app *a);
void app_up(app *a);
void app_left(app *a);  /* expanded dir: collapse; else go to parent */
void app_right(app *a); /* collapsed dir: expand; expanded dir: enter child */
void app_toggle(app *a);
void app_home(app *a);
void app_end(app *a);

void app_toggle_dotfiles(app *a);

/* Filter buffer. push appends the UTF-8 of a codepoint; backspace removes one
 * whole codepoint; clear empties it. */
void app_filter_push(app *a, uint32_t cp);
void app_filter_backspace(app *a);
void app_filter_clear(app *a);

#endif /* FUSSY_APP_H */
