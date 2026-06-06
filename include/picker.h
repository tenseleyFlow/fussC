#ifndef FUSSY_PICKER_H
#define FUSSY_PICKER_H

#include <stdbool.h>

#include "render.h" /* screen */

/*
 * A modal, full-screen fuzzy list - the bespoke fzf-equivalent every browser is
 * built on. Items are borrowed display strings (the caller owns them for the
 * duration of the call). Filtering reuses fuzzy_score; drawing reuses the
 * incremental renderer via screen_present. No worker thread: lists are bounded
 * and scored single-threaded per keystroke.
 *
 * The pure pieces (picker_filter, render_picker_frame) are split out so they
 * can be unit-tested without a terminal; picker_run is the thin input-loop
 * wrapper.
 */

typedef struct {
	const char *title;
	char *const *items;
	int count;

	/*
	 * Optional preview: given the index of the selected item, return a heap
	 * string (the picker frees it) to show in a right-hand pane, or NULL
	 * for none. Called only when the selection changes - the result is
	 * cached - so a slowish command (e.g. `git show`) is fine. With no
	 * preview fn, the list uses the full width.
	 */
	char *(*preview)(void *ctx, int item);
	void *preview_ctx;
} picker_spec;

/*
 * Rank items[0..count) against `query` and return a heap array of matching item
 * indices (caller frees), best first; *n_out gets the match count. An empty
 * query matches everything in original order. Matching/scoring is the same
 * fuzzy_score the tree view uses, case-insensitive.
 */
int *picker_filter(char *const *items, int count, const char *query,
                   int *n_out);

/* Renderable picker state - everything render_picker_frame needs, nothing more.
 */
typedef struct {
	const char *title;
	char *const *items; /* all items */
	const int *matches; /* ranked item indices (length match_count) */
	int match_count;
	int total;         /* count of all items, for the "m/n" counter */
	int sel;           /* selected position within matches[] */
	const char *query; /* current query text (shown on the prompt line) */

	/* Preview pane: lines of the selected item's preview (already split on
	 * newlines), or NULL for no preview. When set and the terminal is wide
	 * enough, the body splits into list | preview. */
	char *const *preview_lines;
	int preview_count;
} picker_view;

/*
 * Build an exactly `rows`-line frame (each clipped to `cols`): a title+counter
 * row, a "> query" prompt, the windowed match list (selection reverse-video),
 * and a key hint when there is room. Pure and testable; caller frees via
 * free_frame.
 */
char **render_picker_frame(const picker_view *v, int rows, int cols,
                           bool color);

/*
 * Run the picker modally: take over `s`, loop on input (type to filter,
 * Up/Down or Ctrl-P/Ctrl-N to move, Enter to choose, Esc to cancel), and return
 * the chosen index into spec->items, or -1 if cancelled. Forces a full repaint
 * on entry and exit so it composes with the main view's renderer.
 */
int picker_run(screen *s, const picker_spec *spec, bool color);

#endif /* FUSSY_PICKER_H */
