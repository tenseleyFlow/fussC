#ifndef FUSSY_FLATTEN_H
#define FUSSY_FLATTEN_H

#include <stdbool.h>
#include <stdint.h>

#include "tree.h"

/*
 * The visible list: the tree projected to the rows currently on screen. Each
 * row is just a node index plus its display depth - no copies of name/path/
 * status (those stay in the arena). `is_last` (last child of its parent) is
 * derivable from the node, so it is not stored; the renderer reconstructs tree
 * gutters from depth + the arena.
 */
typedef struct {
	uint32_t node;
	uint16_t depth; /* 0 = top level (a child of the synthetic root) */
} flat_row;

typedef struct {
	flat_row *rows;
	uint32_t len;
	uint32_t cap;
} flat_list;

void flat_init(flat_list *f);
void flat_free(flat_list *f);

/*
 * Rebuild the visible list: pre-order DFS over the tree, descending into a
 * directory only when it is expanded. The synthetic root is not emitted; its
 * children are depth 0. When `hide_dot` is true, hidden paths - names beginning
 * with '.' and gitignored paths (with their subtrees) - are skipped.
 */
void flatten(flat_list *f, const tree *t, bool hide_dot);

/*
 * Expand or collapse the directory at visible row `row`, updating both the
 * node's flag and the list in place by splicing the affected run (no full
 * rebuild). No-op if the row is not a directory. `hide_dot` matches the value
 * passed to flatten so expanded children stay consistent.
 */
void flat_toggle(flat_list *f, tree *t, uint32_t row, bool hide_dot);

#endif /* FUSSY_FLATTEN_H */
