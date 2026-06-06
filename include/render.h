#ifndef FUSSY_RENDER_H
#define FUSSY_RENDER_H

#include <stdbool.h>
#include <stdio.h>

#include "app.h"
#include "flatten.h"
#include "tree.h"

/*
 * Non-interactive tree output, matching the `tree`/fussr layout: a leading ".",
 * box-drawing gutters, and trailing color-coded status glyphs. When `color` is
 * false, ANSI codes are omitted (for pipes / NO_COLOR).
 */
void render_tree(FILE *out, const tree *t, const flat_list *f, bool color);

/* Same output returned as a heap string (caller frees). Used by tests. */
char *render_tree_string(const tree *t, const flat_list *f, bool color);

/*
 * Interactive rendering. A frame is exactly `rows` terminal lines (header, tree
 * viewport centered on the selection, footer), each already clipped to `cols`
 * display columns. render_frame is pure and testable; screen_draw diffs the new
 * frame against the previous one and writes only the changed lines to stdout.
 */
char **render_frame(const app *a, const char *repo, const char *branch,
                    bool color, int rows, int cols);
void free_frame(char **lines, int rows);

/* Emit to `out` the minimal escape sequences turning frame `oldf` into `newf`
 * (oldf may be NULL for a full paint). Returns the number of lines changed. */
int frame_diff(char *const *oldf, char *const *newf, int rows, FILE *out);

typedef struct {
	char **prev;
	int rows;
} screen;

void screen_init(screen *s);
void screen_free(screen *s);
/* Drop the cached frame so the next screen_draw fully repaints (after a pager).
 */
void screen_invalidate(screen *s);
void screen_draw(screen *s, const app *a, const char *repo, const char *branch,
                 bool color);

#endif /* FUSSY_RENDER_H */
