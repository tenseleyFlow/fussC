#ifndef FUSSY_APP_H
#define FUSSY_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flatten.h"
#include "overlay.h"
#include "tree.h"

#define FILTER_MAX 128

/* Idle gap after which the next typed character starts a fresh filter, so you
 * can type a new name without clearing manually. */
#define FILTER_TIMEOUT_MS 600

/*
 * Interactive view state. The tree and visible list live here; the renderer
 * derives the on-screen window from `selected` each frame (no persistent
 * scroll state). The filter buffer accumulates typed lowercase input - the
 * fuzzy engine that consumes it lands in Sprint 3.
 */
typedef struct {
	tree t;
	flat_list visible;
	uint32_t selected;  /* index into visible.rows */
	bool hide_dotfiles; /* H: hide hidden paths (dotfiles + gitignored) */
	char filter[FILTER_MAX];
	size_t filter_len;
	uint64_t
	    last_input_ns; /* monotonic time of the last filter keystroke */
	overlay ov;        /* active modal overlay, or OV_NONE */
	char status[256];  /* transient result message (git op feedback) */
	int ahead, behind; /* commits vs upstream (for the header indicator) */
} app;

void app_init(app *a);
void app_free(app *a);

/* Re-flatten honoring hide_dotfiles, preserving the selected node when it is
 * still visible (else clamp). Call after the tree changes or dotfiles toggle.
 */
void app_reflatten(app *a);

/* The arena index of the selected node, or NODE_NIL when the list is empty. */
uint32_t app_selected_node(const app *a);

/* Navigation (see architecture.md "Navigation semantics").
 * Up/Down move only among siblings at the same depth (clamped, no wrap); going
 * deeper is Right (enter), shallower is Left. */
void app_down(app *a);  /* next sibling */
void app_up(app *a);    /* previous sibling */
void app_left(app *a);  /* expanded dir: collapse; else go to parent */
void app_right(app *a); /* dir: expand-if-needed and enter first child */
void app_toggle(app *a);
void app_home(app *a);
void app_end(app *a);

void app_toggle_dotfiles(app *a);

/* Filter buffer. push appends the UTF-8 of a codepoint; backspace removes one
 * whole codepoint; clear empties it. */
void app_filter_push(app *a, uint32_t cp);
void app_filter_backspace(app *a);
void app_filter_clear(app *a);

/* Register a keystroke at monotonic time `now_ns`, resetting the buffer first
 * if it has been idle past FILTER_TIMEOUT_MS. */
void app_filter_age(app *a, uint64_t now_ns);

/* True if a non-empty buffer has gone idle past the timeout (auto-clear hook).
 */
bool app_filter_expired(const app *a, uint64_t now_ns);

/* Mark every ancestor of `node` expanded so the node becomes visible. */
void app_expand_to(app *a, uint32_t node);

/* Collect the fuzzy "barrier" set: the node indices of collapsed *ignored*
 * directories, whose subtrees fuzzy must not search (a big collapsed
 * node_modules stays out of the index until expanded). Writes up to `max`
 * indices into `out` and returns the count. Honors the H toggle: when hidden,
 * ignored subtrees are pruned here too. */
int app_fuzzy_barriers(const app *a, uint32_t *out, int max);

/*
 * Refresh helpers (used to rebuild the tree after a git mutation while keeping
 * the user's view). Snapshot the collapsed-directory paths and selected path
 * before the rebuild; reapply them after.
 */
char **app_collapsed_paths(const app *a, uint32_t *count); /* caller frees */
void app_collapse_paths(app *a, char *const *paths, uint32_t count);
/* Ignored dirs start collapsed (git_load_tree). Snapshot the ones the user has
 * expanded and reapply them after a rebuild, so opening an ignored dir survives
 * a refresh (mirror of the collapsed-path snapshot above). */
char **app_expanded_ignored_paths(const app *a, uint32_t *count); /* frees */
void app_expand_paths(app *a, char *const *paths, uint32_t count);
char *app_selected_path_dup(const app *a); /* caller frees, may be NULL */
void app_select_path(app *a, const char *path);

/* Apply a fuzzy result: NODE_NIL leaves the selection put (no match); otherwise
 * expand the path to `node`, re-flatten, and select it. The match itself is
 * computed elsewhere (the scorer single-threaded, or the worker thread) so app
 * stays decoupled. */
void app_apply_match(app *a, uint32_t node);

#endif /* FUSSY_APP_H */
