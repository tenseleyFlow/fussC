#include "app.h"
#include "fuzzy.h"
#include "test.h"
#include "tree.h"

#include <stdbool.h>
#include <string.h>

static bool name_visible(const app *a, const char *name)
{
	for (uint32_t i = 0; i < a->visible.len; i++)
		if (strcmp(a->t.nodes[a->visible.rows[i].node].name, name) == 0)
			return true;
	return false;
}

void test_fuzzy_tiers(void)
{
	CHECK(fuzzy_score("abc", "abc") == SCORE_EXACT);
	CHECK(fuzzy_score("ab", "abcd") == SCORE_PREFIX);
	CHECK(fuzzy_score("", "x") == SCORE_NONE);
	CHECK(fuzzy_score("xyz", "abc") == SCORE_NONE);
	CHECK(fuzzy_score("ac", "abc") > SCORE_NONE); /* subsequence */
}

void test_fuzzy_ordering(void)
{
	/* exact beats prefix beats scattered. */
	CHECK(fuzzy_score("abc", "abc") > fuzzy_score("abc", "abcd"));
	CHECK(fuzzy_score("ab", "abxx") > fuzzy_score("ab", "axbx"));
	/* word-boundary match scores higher than a mid-word one. */
	CHECK(fuzzy_score("m", "ab_main") > fuzzy_score("m", "abxmain"));
	/* consecutive run beats the same chars spread out. */
	CHECK(fuzzy_score("abc", "abcxx") > fuzzy_score("abc", "axbxc"));
}

void test_fuzzy_best_match(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, "README.md", 0);
	tree_add(&t, "src/main.c", 0);
	tree_add(&t, "src/util.c", 0);

	uint32_t m = fuzzy_best_match(&t, "main.c");
	CHECK(m != NODE_NIL);
	CHECK_STR_EQ(t.nodes[m].name, "main.c");

	uint32_t r = fuzzy_best_match(&t, "readme");
	CHECK(r != NODE_NIL);
	CHECK_STR_EQ(t.nodes[r].name, "README.md");

	/* path subsequence: "srcmain" matches src/main.c across the separator.
	 */
	uint32_t p = fuzzy_best_match(&t, "srcmain");
	CHECK(p != NODE_NIL);
	CHECK_STR_EQ(t.nodes[p].name, "main.c");

	CHECK(fuzzy_best_match(&t, "zzzzz") == NODE_NIL);
	CHECK(fuzzy_best_match(&t, "") == NODE_NIL);

	tree_free(&t);
}

/* A path-fallback match must clear SCORE_PATH_MIN: a compact path query wins,
 * but a subsequence scattered across directory names does not. */
void test_fuzzy_path_floor(void)
{
	/* Reported case: "fll" is a subsequence of the workflow path but
	 * matches no filename; it must score below the floor. */
	CHECK(fuzzy_score("fll", ".github/workflows/ci.yml") < SCORE_PATH_MIN);
	/* A real path query stays well above the floor. */
	CHECK(fuzzy_score("srcmain", "src/main.c") >= SCORE_PATH_MIN);
	/* No filename contains two l's, so the basename pass finds nothing. */
	CHECK(fuzzy_score("fll", "flatten.c") == SCORE_NONE);
}

/* Expected jump targets for representative queries (regression guard). */
void test_fuzzy_expected_matches(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, "src/flatten.c", 0);
	tree_add(&t, "include/flatten.h", 0);
	tree_add(&t, "test/test_flatten.c", 0);
	tree_add(&t, "src/main.c", 0);
	tree_add(&t, ".github/workflows/ci.yml", 0);

	/* Basename prefix lands on a flatten file. */
	uint32_t m = fuzzy_best_match(&t, "fl");
	CHECK(m != NODE_NIL && strstr(t.nodes[m].name, "flatten") != NULL);
	m = fuzzy_best_match(&t, "flat");
	CHECK(m != NODE_NIL && strstr(t.nodes[m].name, "flatten") != NULL);

	/* The reported surprise: "fll" matches no filename and must NOT jump to
	 * the workflow path. */
	CHECK(fuzzy_best_match(&t, "fll") == NODE_NIL);

	/* Compact path typing still works. */
	m = fuzzy_best_match(&t, "srcmain");
	CHECK(m != NODE_NIL);
	CHECK_STR_EQ(t.nodes[m].name, "main.c");

	/* Typing the actual workflow name still finds it. */
	m = fuzzy_best_match(&t, "ci.yml");
	CHECK(m != NODE_NIL);
	CHECK_STR_EQ(t.nodes[m].name, "ci.yml");

	tree_free(&t);
}

/* The headline fix: jump into a collapsed subtree by auto-expanding the path.
 */
void test_fuzzy_auto_expand(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "src/deep/buried.c", 0);
	tree_add(&a.t, "top.txt", 0);
	app_reflatten(&a);

	/* Collapse src so buried.c is not in the visible list. */
	uint32_t src = tree_find(&a.t, "src");
	a.t.nodes[src].flags &= (uint8_t)~NF_EXPANDED;
	app_reflatten(&a);
	CHECK(!name_visible(&a, "buried.c"));

	/* Type a query that only matches the buried file; jump to it. */
	app_filter_push(&a, 'b');
	app_filter_push(&a, 'u');
	app_filter_push(&a, 'r');
	app_apply_match(&a, fuzzy_best_match(&a.t, a.filter));

	CHECK(name_visible(&a, "buried.c"));
	CHECK_STR_EQ(a.t.nodes[app_selected_node(&a)].name, "buried.c");

	/* No match leaves the selection put (no jump). */
	uint32_t before = app_selected_node(&a);
	app_apply_match(&a, fuzzy_best_match(&a.t, "zzzzz"));
	CHECK(app_selected_node(&a) == before);

	app_free(&a);
}

/* Deterministic fuzz: the scorer never crashes and stays within bounds. */
void test_fuzzy_fuzz(void)
{
	uint32_t st = 0x9e3779b9u;
	char pat[12], text[40];
	bool bounded = true;

	for (int iter = 0; iter < 50000; iter++) {
		st = st * 1103515245u + 12345u;
		size_t pn = (st >> 8) % sizeof(pat);
		for (size_t i = 0; i < pn; i++) {
			st = st * 1103515245u + 12345u;
			pat[i] = (char)('a' + ((st >> 10) % 6));
		}
		pat[pn] = '\0';

		st = st * 1103515245u + 12345u;
		size_t tn = (st >> 8) % sizeof(text);
		for (size_t i = 0; i < tn; i++) {
			st = st * 1103515245u + 12345u;
			text[i] = (char)('a' + ((st >> 10) % 6));
		}
		text[tn] = '\0';

		int s = fuzzy_score(pat, text);
		if (s < 0 || s > SCORE_EXACT)
			bounded = false;
	}
	CHECK(bounded);

	/* Invariants. */
	CHECK(fuzzy_score("hello", "hello") == SCORE_EXACT);
	CHECK(fuzzy_score("hel", "hello") == SCORE_PREFIX);
}
