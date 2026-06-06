#include "app.h"
#include "render.h"
#include "test.h"
#include "tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_render_frame_layout(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "alpha", ST_UNSTAGED);
	tree_add(&a.t, "beta", ST_STAGED);
	app_reflatten(&a);

	char **f = render_frame(&a, "repo", "trunk", false, 6, 80);
	CHECK(strstr(f[0], "repo:trunk") != NULL); /* header */
	CHECK(strstr(f[1], "alpha") != NULL);      /* first tree row */
	CHECK(strstr(f[2], "beta") != NULL);
	CHECK(strstr(f[5], "quit") != NULL); /* footer */
	free_frame(f, 6);

	app_free(&a);
}

void test_render_frame_selection(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "x", ST_UNSTAGED);
	tree_add(&a.t, "y", ST_UNSTAGED);
	app_reflatten(&a);
	app_down(&a); /* select y */

	char **f = render_frame(&a, "r", "b", true, 6, 40);
	/* Selected row reverse-video; the other row is not. */
	CHECK(strstr(f[2], "\033[7m") != NULL);
	CHECK(strstr(f[1], "\033[7m") == NULL);
	free_frame(f, 6);

	app_free(&a);
}

void test_render_frame_filter_in_header(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "file", ST_UNSTAGED);
	app_reflatten(&a);
	app_filter_push(&a, 'f');
	app_filter_push(&a, 'i');

	char **f = render_frame(&a, "r", "b", false, 5, 40);
	CHECK(strstr(f[0], "/fi") != NULL);
	free_frame(f, 5);

	app_free(&a);
}

void test_frame_diff_minimal(void)
{
	char *oldf[3] = {strdup("aaa"), strdup("bbb"), strdup("ccc")};
	char *newf[3] = {strdup("aaa"), strdup("XXX"), strdup("ccc")};

	char *buf = NULL;
	size_t blen = 0;
	FILE *m = open_memstream(&buf, &blen);
	int changed = frame_diff(oldf, newf, 3, m);
	fclose(m);

	CHECK(changed == 1);
	CHECK(strstr(buf, "\033[2;1H") != NULL); /* row 2 repainted */
	CHECK(strstr(buf, "XXX") != NULL);
	CHECK(strstr(buf, "\033[1;1H") == NULL); /* rows 1 and 3 untouched */
	CHECK(strstr(buf, "\033[3;1H") == NULL);

	free(buf);
	for (int i = 0; i < 3; i++) {
		free(oldf[i]);
		free(newf[i]);
	}
}

void test_frame_diff_full_paint(void)
{
	char *newf[2] = {strdup("h"), strdup("f")};

	char *buf = NULL;
	size_t blen = 0;
	FILE *m = open_memstream(&buf, &blen);
	int changed =
	    frame_diff(NULL, newf, 2, m); /* NULL old -> repaint all */
	fclose(m);

	CHECK(changed == 2);
	free(buf);
	free(newf[0]);
	free(newf[1]);
}
