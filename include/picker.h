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

/*
 * A browser-defined key the picker surfaces instead of treating as filter
 * input. Use a non-printable key (Ctrl-letter, Delete, F-key) so it does not
 * collide with the always-on fuzzy filter. The label is shown in the hint row.
 */
typedef struct {
	int key;
	const char *label;
} picker_binding;

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

	/* Optional extra keys the browser handles (delete, new, apply, ...). */
	const picker_binding *bindings;
	int binding_count;
} picker_spec;

/*
 * How a picker_run ended: `key` is KEY_ENTER on a normal accept, one of the
 * spec's binding keys when such a key was pressed, or KEY_ESC/KEY_EOF on
 * cancel. `index` is the selected item index for accept/binding (-1 when the
 * list was empty or on cancel).
 */
typedef struct {
	int index;
	int key;
} picker_result;

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
	int preview_col; /* horizontal scroll offset (display columns) */
	const char
	    *extra; /* optional extra hint text (binding labels), or NULL */
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
 * Up/Down or Ctrl-P/Ctrl-N to move, Left/Right to scroll the preview, Enter to
 * choose, Esc to cancel, plus any spec bindings). Returns the outcome (see
 * picker_result). Forces a full repaint on entry and exit so it composes with
 * the main view's renderer.
 */
picker_result picker_run(screen *s, const picker_spec *spec, bool color);

/*
 * One-line modal text prompt (e.g. a new branch name): take over `s`, show
 * "title> input", edit with type/Backspace, Enter accepts, Esc cancels. On
 * accept, copies the entry into out[0..outsz) and returns true; on cancel (or
 * empty entry) returns false. Repaints fully on exit.
 */
bool prompt_line(screen *s, const char *title, char *out, size_t outsz,
                 bool color);

#endif /* FUSSY_PICKER_H */
