#include "flatten.h"
#include "test.h"
#include "tree.h"

#include <string.h>

/* Helper: does row i name a node with this name at this depth? */
static bool row_is(const flat_list *f, const tree *t, uint32_t i,
                   const char *name, uint16_t depth)
{
	if (i >= f->len)
		return false;
	const node *n = &t->nodes[f->rows[i].node];
	return f->rows[i].depth == depth && strcmp(n->name, name) == 0;
}

void test_flatten_expanded(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, "a", ST_UNSTAGED);
	tree_add(&t, "b/c", ST_UNSTAGED);
	tree_add(&t, "b/d", ST_STAGED);
	tree_add(&t, "e", ST_UNTRACKED);

	flat_list f;
	flat_init(&f);
	flatten(&f, &t);

	/* All dirs expanded: a, b, c, d, e in pre-order with depths. */
	CHECK(f.len == 5);
	CHECK(row_is(&f, &t, 0, "a", 0));
	CHECK(row_is(&f, &t, 1, "b", 0));
	CHECK(row_is(&f, &t, 2, "c", 1));
	CHECK(row_is(&f, &t, 3, "d", 1));
	CHECK(row_is(&f, &t, 4, "e", 0));

	flat_free(&f);
	tree_free(&t);
}

void test_flatten_collapsed(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, "a", ST_UNSTAGED);
	tree_add(&t, "b/c", ST_UNSTAGED);
	tree_add(&t, "b/d", ST_STAGED);
	tree_add(&t, "e", ST_UNTRACKED);

	/* Collapse b: its children must disappear from the visible list. */
	uint32_t b = tree_find(&t, "b");
	t.nodes[b].flags &= (uint8_t)~NF_EXPANDED;

	flat_list f;
	flat_init(&f);
	flatten(&f, &t);

	CHECK(f.len == 3);
	CHECK(row_is(&f, &t, 0, "a", 0));
	CHECK(row_is(&f, &t, 1, "b", 0));
	CHECK(row_is(&f, &t, 2, "e", 0));

	/* Re-expanding restores them; flatten is a pure rebuild. */
	t.nodes[b].flags |= NF_EXPANDED;
	flatten(&f, &t);
	CHECK(f.len == 5);

	flat_free(&f);
	tree_free(&t);
}

void test_flatten_empty(void)
{
	tree t;
	tree_init(&t);

	flat_list f;
	flat_init(&f);
	flatten(&f, &t);
	CHECK(f.len == 0);

	flat_free(&f);
	tree_free(&t);
}
