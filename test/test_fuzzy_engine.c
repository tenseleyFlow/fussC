#include "fuzzy.h"
#include "test.h"
#include "tree.h"

#include <poll.h>
#include <stdio.h>
#include <string.h>

/* Submit a query and block (with a generous timeout) until the worker posts the
 * result for that generation. Returns false only on timeout. */
static bool submit_and_wait(fuzzy_engine *e, const tree *t, const char *q,
                            uint32_t *out)
{
	fuzzy_submit(e, t, q);
	struct pollfd p = {
	    .fd = fuzzy_engine_wake_fd(e), .events = POLLIN, .revents = 0};
	for (int tries = 0; tries < 100; tries++) {
		if (poll(&p, 1, 200) <= 0)
			return false;
		if (fuzzy_engine_take(e, out))
			return true;
		/* Stale wake (older generation dropped); keep waiting. */
	}
	return false;
}

void test_engine_basic(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, "README.md", 0);
	tree_add(&t, "src/main.c", 0);
	tree_add(&t, "src/util.c", 0);

	fuzzy_engine e;
	CHECK(fuzzy_engine_start(&e));

	uint32_t node = NODE_NIL;
	CHECK(submit_and_wait(&e, &t, "main.c", &node));
	CHECK(node != NODE_NIL);
	CHECK_STR_EQ(t.nodes[node].name, "main.c");

	/* No match still produces a (NODE_NIL) result for the latest query. */
	CHECK(submit_and_wait(&e, &t, "zzzzz", &node));
	CHECK(node == NODE_NIL);

	fuzzy_engine_stop(&e);
	tree_free(&t);
}

/* Rapid submits must converge to the final query's result, with stale
 * generations dropped, no deadlock, no leak (verify under ASan/TSan). */
void test_engine_convergence(void)
{
	tree t;
	tree_init(&t);
	char path[64];
	for (int i = 0; i < 4000; i++) {
		snprintf(path, sizeof(path), "dir%d/file%d.c", i % 40, i);
		tree_add(&t, path, 0);
	}

	fuzzy_engine e;
	CHECK(fuzzy_engine_start(&e));

	/* Fire a burst of queries without waiting; the worker should skip the
	 * intermediates and always score the newest. */
	char q[16];
	for (int i = 0; i < 300; i++) {
		snprintf(q, sizeof(q), "file%d", i);
		fuzzy_submit(&e, &t, q);
	}

	uint32_t node = NODE_NIL;
	CHECK(submit_and_wait(&e, &t, "file1234", &node));
	CHECK(node != NODE_NIL);
	CHECK(strstr(t.nodes[node].name, "1234") != NULL);

	fuzzy_engine_stop(&e);
	tree_free(&t);
}

static void fill(tree *t, int n)
{
	char path[64];
	for (int i = 0; i < n; i++) {
		snprintf(path, sizeof(path), "dir%d/file%d.c", i % 20, i);
		tree_add(t, path, 0);
	}
}

/* pause() must make it safe to free and rebuild the arena while the worker is
 * alive: no use-after-free, no race, no stale result applied. ASan/TSan guard.
 */
void test_engine_pause_resume(void)
{
	tree t;
	tree_init(&t);
	fill(&t, 600);

	fuzzy_engine e;
	CHECK(fuzzy_engine_start(&e));
	fuzzy_submit(&e, &t, "file");

	for (int r = 0; r < 60; r++) {
		fuzzy_submit(&e, &t, "fi"); /* keep the worker busy */
		fuzzy_engine_pause(&e);     /* quiesce before touching t */
		tree_free(&t);              /* safe only because of pause */
		tree_init(&t);
		fill(&t, 600);
		fuzzy_engine_resume(&e);
		fuzzy_submit(&e, &t, "file");
	}

	uint32_t node = NODE_NIL;
	CHECK(submit_and_wait(&e, &t, "file123", &node));
	CHECK(node != NODE_NIL);

	fuzzy_engine_stop(&e);
	tree_free(&t);
}
