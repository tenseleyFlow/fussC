#include "app.h"
#include "test.h"
#include "tree.h"

#include <string.h>

static const char *sel_name(const app *a)
{
	uint32_t n = app_selected_node(a);
	return n == NODE_NIL ? "" : a->t.nodes[n].name;
}

/* a, b/{c,d}, e  -- all dirs expanded by default. */
static void build(app *a)
{
	app_init(a);
	tree_add(&a->t, "a", ST_UNSTAGED);
	tree_add(&a->t, "b/c", ST_UNSTAGED);
	tree_add(&a->t, "b/d", ST_STAGED);
	tree_add(&a->t, "e", ST_UNTRACKED);
	app_reflatten(a);
}

void test_nav_siblings(void)
{
	app a;
	build(&a); /* visible: a, b, c, d, e (b expanded with c,d) */
	CHECK(a.visible.len == 5);
	CHECK_STR_EQ(sel_name(&a), "a");

	/* Down moves among depth-0 siblings, skipping b's children c,d. */
	app_down(&a);
	CHECK_STR_EQ(sel_name(&a), "b");
	app_down(&a);
	CHECK_STR_EQ(sel_name(&a), "e"); /* skips c and d */
	app_down(&a);                    /* clamp: e is the last sibling */
	CHECK_STR_EQ(sel_name(&a), "e");

	app_up(&a);
	CHECK_STR_EQ(sel_name(&a), "b");
	app_up(&a);
	CHECK_STR_EQ(sel_name(&a), "a");
	app_up(&a); /* clamp at first sibling */
	CHECK_STR_EQ(sel_name(&a), "a");

	app_free(&a);
}

void test_nav_enter_and_back(void)
{
	app a;
	build(&a);

	/* Right on a file is a no-op. */
	app_right(&a);
	CHECK_STR_EQ(sel_name(&a), "a");

	/* Right enters b (expanded) -> first child c. */
	app_down(&a); /* b */
	app_right(&a);
	CHECK_STR_EQ(sel_name(&a), "c");

	/* Inside the dir, Down/Up move among children only. */
	app_down(&a);
	CHECK_STR_EQ(sel_name(&a), "d");
	app_down(&a); /* clamp: d is the last child */
	CHECK_STR_EQ(sel_name(&a), "d");

	/* Left returns to the parent dir. */
	app_left(&a);
	CHECK_STR_EQ(sel_name(&a), "b");

	/* Left on the expanded dir collapses it; selection stays on the dir. */
	app_left(&a);
	CHECK_STR_EQ(sel_name(&a), "b");
	CHECK(a.visible.len == 3); /* a, b, e */

	/* Right enters a collapsed dir: expand AND descend in one press. */
	app_right(&a);
	CHECK_STR_EQ(sel_name(&a), "c");
	CHECK(a.visible.len == 5);

	app_free(&a);
}

void test_nav_filter_timeout(void)
{
	app a;
	build(&a);

	/* Typing within the window appends; an idle gap resets the buffer. */
	app_filter_age(&a, 1000ull * 1000000ull); /* t = 1000ms */
	app_filter_push(&a, 'a');
	app_filter_age(&a, 1100ull * 1000000ull); /* +100ms: still fresh */
	app_filter_push(&a, 'b');
	CHECK_STR_EQ(a.filter, "ab");
	CHECK(!app_filter_expired(&a, 1300ull * 1000000ull)); /* +200ms */
	CHECK(app_filter_expired(&a, 2000ull * 1000000ull));  /* +900ms idle */

	/* Next keystroke after the gap starts fresh. */
	app_filter_age(&a, 2000ull * 1000000ull);
	app_filter_push(&a, 'z');
	CHECK_STR_EQ(a.filter, "z");

	app_free(&a);
}

void test_nav_dotfiles(void)
{
	app a;
	build(&a);
	tree_add(&a.t, ".env", ST_UNTRACKED);
	app_reflatten(&a);
	CHECK(a.visible.len == 6); /* .env sorts first */
	CHECK_STR_EQ(a.t.nodes[a.visible.rows[0].node].name, ".env");

	app_toggle_dotfiles(&a); /* hide */
	CHECK(a.visible.len == 5);
	for (uint32_t i = 0; i < a.visible.len; i++)
		CHECK(a.t.nodes[a.visible.rows[i].node].name[0] != '.');

	app_toggle_dotfiles(&a); /* show again */
	CHECK(a.visible.len == 6);

	app_free(&a);
}

/* The incremental splice in flat_toggle must match a from-scratch flatten. */
static bool same_list(const flat_list *x, const flat_list *y)
{
	if (x->len != y->len)
		return false;
	for (uint32_t i = 0; i < x->len; i++) {
		if (x->rows[i].node != y->rows[i].node ||
		    x->rows[i].depth != y->rows[i].depth)
			return false;
	}
	return true;
}

void test_toggle_matches_oracle(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, "a", 0);
	tree_add(&t, "b/c", 0);
	tree_add(&t, "b/d/e", 0); /* nested: exercises multi-level splice */
	tree_add(&t, "b/f", 0);
	tree_add(&t, "g", 0);

	flat_list inc, oracle;
	flat_init(&inc);
	flat_init(&oracle);
	flatten(&inc, &t, false);

	uint32_t b = tree_find(&t, "b");
	uint32_t rb = 0;
	for (uint32_t i = 0; i < inc.len; i++)
		if (inc.rows[i].node == b)
			rb = i;

	/* Collapse b incrementally, compare to a fresh flatten. */
	flat_toggle(&inc, &t, rb, false);
	flatten(&oracle, &t, false);
	CHECK(same_list(&inc, &oracle));

	/* Expand again, compare. */
	flat_toggle(&inc, &t, rb, false);
	flatten(&oracle, &t, false);
	CHECK(same_list(&inc, &oracle));

	flat_free(&inc);
	flat_free(&oracle);
	tree_free(&t);
}

void test_nav_filter_buffer(void)
{
	app a;
	build(&a);

	app_filter_push(&a, 's');
	app_filter_push(&a, 'r');
	CHECK(a.filter_len == 2);
	CHECK_STR_EQ(a.filter, "sr");

	app_filter_push(&a, 0x4E2D); /* 中, 3 bytes */
	CHECK(a.filter_len == 5);

	app_filter_backspace(&a); /* removes whole codepoint */
	CHECK(a.filter_len == 2);
	CHECK_STR_EQ(a.filter, "sr");

	app_filter_clear(&a);
	CHECK(a.filter_len == 0);
	CHECK_STR_EQ(a.filter, "");

	app_free(&a);
}
