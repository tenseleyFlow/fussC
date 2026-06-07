#include "flatten.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

void flat_init(flat_list *f)
{
	f->rows = NULL;
	f->len = 0;
	f->cap = 0;
}

void flat_free(flat_list *f)
{
	free(f->rows);
	f->rows = NULL;
	f->len = f->cap = 0;
}

static void flat_reserve(flat_list *f, uint32_t need)
{
	if (f->cap >= need)
		return;
	while (f->cap < need)
		f->cap = f->cap ? f->cap * 2 : 64;
	f->rows = xrealloc(f->rows, f->cap * sizeof(*f->rows));
}

static void flat_push(flat_list *f, uint32_t node, uint16_t depth)
{
	flat_reserve(f, f->len + 1);
	f->rows[f->len].node = node;
	f->rows[f->len].depth = depth;
	f->len++;
}

/* "Hidden" (toggled by H) = dotfiles and gitignored paths: both are noise the
 * user usually does not want in the tree. */
static bool is_hidden(const tree *t, uint32_t idx)
{
	const node *n = &t->nodes[idx];
	return n->name[0] == '.' || (n->status & ST_GITIGNORED) != 0;
}

static void walk(flat_list *f, const tree *t, uint32_t idx, uint16_t depth,
                 bool hide_dot)
{
	if (hide_dot && is_hidden(t, idx))
		return;

	flat_push(f, idx, depth);

	const node *n = &t->nodes[idx];
	if (node_is_file(n) || !node_is_expanded(n))
		return;

	for (uint32_t c = n->first_child; c != NODE_NIL;
	     c = t->nodes[c].next_sibling)
		walk(f, t, c, (uint16_t)(depth + 1), hide_dot);
}

void flatten(flat_list *f, const tree *t, bool hide_dot)
{
	f->len = 0;
	for (uint32_t c = t->nodes[0].first_child; c != NODE_NIL;
	     c = t->nodes[c].next_sibling)
		walk(f, t, c, 0, hide_dot);
}

void flat_toggle(flat_list *f, tree *t, uint32_t row, bool hide_dot)
{
	if (row >= f->len)
		return;
	uint32_t idx = f->rows[row].node;
	node *n = &t->nodes[idx];
	if (node_is_file(n))
		return;

	uint16_t d = f->rows[row].depth;

	if (node_is_expanded(n)) {
		/* Collapse: drop the contiguous run of deeper rows. */
		uint32_t end = row + 1;
		while (end < f->len && f->rows[end].depth > d)
			end++;
		uint32_t removed = end - (row + 1);
		memmove(&f->rows[row + 1], &f->rows[end],
		        (f->len - end) * sizeof(*f->rows));
		f->len -= removed;
		n->flags &= (uint8_t)~NF_EXPANDED;
	} else {
		/* Expand: build the subtree's rows, then splice them in. */
		n->flags |= NF_EXPANDED;
		flat_list sub;
		flat_init(&sub);
		for (uint32_t c = n->first_child; c != NODE_NIL;
		     c = t->nodes[c].next_sibling)
			walk(&sub, t, c, (uint16_t)(d + 1), hide_dot);

		flat_reserve(f, f->len + sub.len);
		memmove(&f->rows[row + 1 + sub.len], &f->rows[row + 1],
		        (f->len - (row + 1)) * sizeof(*f->rows));
		/* sub.rows is NULL when the dir has no visible children (e.g.
		 * all dotfiles hidden); memcpy(_, NULL, 0) is UB, so guard it.
		 */
		if (sub.len > 0)
			memcpy(&f->rows[row + 1], sub.rows,
			       sub.len * sizeof(*f->rows));
		f->len += sub.len;
		flat_free(&sub);
	}
}
