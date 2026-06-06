#ifndef FUSSY_RENDER_H
#define FUSSY_RENDER_H

#include <stdbool.h>
#include <stdio.h>

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

#endif /* FUSSY_RENDER_H */
