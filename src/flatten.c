#include "flatten.h"

#include <stdlib.h>

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

static void flat_push(flat_list *f, uint32_t node, uint16_t depth)
{
	if (f->len == f->cap) {
		f->cap = f->cap ? f->cap * 2 : 64;
		f->rows = xrealloc(f->rows, f->cap * sizeof(*f->rows));
	}
	f->rows[f->len].node = node;
	f->rows[f->len].depth = depth;
	f->len++;
}

static void walk(flat_list *f, const tree *t, uint32_t idx, uint16_t depth)
{
	flat_push(f, idx, depth);

	const node *n = &t->nodes[idx];
	if (node_is_file(n) || !node_is_expanded(n))
		return;

	for (uint32_t c = n->first_child; c != NODE_NIL;
	     c = t->nodes[c].next_sibling)
		walk(f, t, c, (uint16_t)(depth + 1));
}

void flatten(flat_list *f, const tree *t)
{
	f->len = 0;
	for (uint32_t c = t->nodes[0].first_child; c != NODE_NIL;
	     c = t->nodes[c].next_sibling)
		walk(f, t, c, 0);
}
