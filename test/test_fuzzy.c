#include "fuzzy.h"
#include "test.h"
#include "tree.h"

#include <stdbool.h>
#include <string.h>

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
