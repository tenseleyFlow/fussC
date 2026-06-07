#include "test.h"
#include "tree.h"

#include <string.h>

/* nth direct child (0-based), or NODE_NIL. */
static uint32_t nth_child(const tree *t, uint32_t parent, int n)
{
	uint32_t c = t->nodes[parent].first_child;
	while (n-- > 0 && c != NODE_NIL)
		c = t->nodes[c].next_sibling;
	return c;
}

void test_tree_build(void)
{
	tree t;
	tree_init(&t);

	/* Root invariants. */
	CHECK_STR_EQ(t.nodes[0].name, ".");
	CHECK_STR_EQ(t.nodes[0].path, "");
	CHECK(!node_is_file(&t.nodes[0]));
	CHECK(node_is_expanded(&t.nodes[0]));

	tree_add(&t, "README.md", ST_UNSTAGED);
	tree_add(&t, "Makefile", ST_STAGED);
	tree_add(&t, "src/main.c", ST_UNSTAGED);
	tree_add(&t, "src/util.c", ST_STAGED);
	tree_add(&t, ".gitignore", ST_UNTRACKED);

	/* Root children sort case-insensitively: .gitignore, Makefile,
	 * README.md, src. */
	uint32_t c0 = nth_child(&t, 0, 0);
	uint32_t c1 = nth_child(&t, 0, 1);
	uint32_t c2 = nth_child(&t, 0, 2);
	uint32_t c3 = nth_child(&t, 0, 3);
	CHECK_STR_EQ(t.nodes[c0].name, ".gitignore");
	CHECK_STR_EQ(t.nodes[c1].name, "Makefile");
	CHECK_STR_EQ(t.nodes[c2].name, "README.md");
	CHECK_STR_EQ(t.nodes[c3].name, "src");
	CHECK(nth_child(&t, 0, 4) == NODE_NIL);

	/* Leaf vs directory, status, lowercased name. */
	CHECK(node_is_file(&t.nodes[c0]));
	CHECK(t.nodes[c0].status == ST_UNTRACKED);
	CHECK(!node_is_file(&t.nodes[c3]));
	CHECK(node_is_expanded(&t.nodes[c3]));
	CHECK_STR_EQ(t.nodes[c1].name_lower, "makefile");
	CHECK_STR_EQ(t.nodes[c2].name_lower, "readme.md");

	/* Directory contents and full paths. */
	uint32_t s0 = nth_child(&t, c3, 0);
	uint32_t s1 = nth_child(&t, c3, 1);
	CHECK_STR_EQ(t.nodes[s0].name, "main.c");
	CHECK_STR_EQ(t.nodes[s1].name, "util.c");
	CHECK_STR_EQ(t.nodes[s0].path, "src/main.c");
	CHECK(t.nodes[s0].status == ST_UNSTAGED);
	CHECK(t.nodes[s1].status == ST_STAGED);

	/* Parent links. */
	CHECK(t.nodes[s0].parent == c3);
	CHECK(t.nodes[c3].parent == 0);

	/* Lookup. */
	CHECK(tree_find(&t, "src/util.c") == s1);
	CHECK(tree_find(&t, "src") == c3);
	CHECK(tree_find(&t, "nope") == NODE_NIL);
	CHECK(tree_find(&t, "src/nope.c") == NODE_NIL);

	tree_free(&t);
}

void test_tree_merge_status(void)
{
	tree t;
	tree_init(&t);

	tree_add(&t, "f", ST_STAGED);
	tree_add(&t, "f", ST_UNSTAGED);

	uint32_t f = tree_find(&t, "f");
	CHECK((t.nodes[f].status & ST_STAGED) != 0);
	CHECK((t.nodes[f].status & ST_UNSTAGED) != 0);
	/* Same path added twice is one node. */
	CHECK(t.nodes[0].first_child == f);
	CHECK(t.nodes[f].next_sibling == NODE_NIL);

	tree_free(&t);
}

void test_tree_trailing_slash_is_dir(void)
{
	tree t;
	tree_init(&t);

	/* libgit2 reports a wholly-ignored dir as "build/": the trailing slash
	 * must make it a directory node (expandable), not a file leaf, while
	 * still carrying the status. */
	tree_add(&t, "build/", ST_GITIGNORED);
	uint32_t b = tree_find(&t, "build");
	CHECK(b != NODE_NIL);
	CHECK(!node_is_file(&t.nodes[b]));
	CHECK((t.nodes[b].status & ST_GITIGNORED) != 0);

	/* A normal file path is still a file. */
	tree_add(&t, "src/main.c", 0);
	CHECK(node_is_file(&t.nodes[tree_find(&t, "src/main.c")]));
	CHECK(!node_is_file(&t.nodes[tree_find(&t, "src")]));

	tree_free(&t);
}

void test_tree_case_order(void)
{
	tree t;
	tree_init(&t);

	tree_add(&t, "cherry", 0);
	tree_add(&t, "Banana", 0);
	tree_add(&t, "apple", 0);
	CHECK_STR_EQ(t.nodes[nth_child(&t, 0, 0)].name, "apple");
	CHECK_STR_EQ(t.nodes[nth_child(&t, 0, 1)].name, "Banana");
	CHECK_STR_EQ(t.nodes[nth_child(&t, 0, 2)].name, "cherry");

	/* Names equal under case folding break the tie case-sensitively, with
	 * uppercase first; both remain distinct nodes. */
	tree_add(&t, "abc", 0);
	tree_add(&t, "ABC", 0);
	CHECK_STR_EQ(t.nodes[nth_child(&t, 0, 0)].name, "ABC");
	CHECK_STR_EQ(t.nodes[nth_child(&t, 0, 1)].name, "abc");
	CHECK(tree_find(&t, "abc") != tree_find(&t, "ABC"));

	tree_free(&t);
}
