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
	CHECK(strstr(f[4], "quit") != NULL);  /* footer row 1 (nav) */
	CHECK(strstr(f[5], "stage") != NULL); /* footer row 2 (git) */
	free_frame(f, 6);

	app_free(&a);
}

void test_render_frame_ahead_behind(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "x", ST_STAGED);
	app_reflatten(&a);

	/* No upstream divergence: header is just repo:branch. */
	char **f0 = render_frame(&a, "repo", "main", false, 6, 80);
	CHECK(strstr(f0[0], "repo:main") != NULL);
	CHECK(strstr(f0[0], "\342\206\221") == NULL); /* no up arrow */
	free_frame(f0, 6);

	/* 2 ahead, 1 behind -> "↑2" and "↓1" in the header. */
	a.ahead = 2;
	a.behind = 1;
	char **f1 = render_frame(&a, "repo", "main", false, 6, 80);
	CHECK(strstr(f1[0], "\342\206\221"
	                    "2") != NULL); /* up arrow 2 */
	CHECK(strstr(f1[0], "\342\206\223"
	                    "1") != NULL); /* down arrow 1 */
	free_frame(f1, 6);

	app_free(&a);
}

void test_render_frame_degenerate_sizes(void)
{
	/* Tiny / zero-sized terminals must not crash or over-read (ASan/UBSan
	 * in CI make this assertion teeth). Exercised with an overlay up too.
	 */
	app a;
	app_init(&a);
	tree_add(&a.t, "x/y/z.txt", ST_UNSTAGED);
	tree_add(&a.t, "a.txt", ST_STAGED);
	app_reflatten(&a);

	int sizes[][2] = {{0, 0}, {1, 1}, {1, 80}, {80, 1},
	                  {2, 3}, {3, 2}, {1, 0},  {0, 1}};
	for (int i = 0; i < (int)(sizeof(sizes) / sizeof(*sizes)); i++) {
		int r = sizes[i][0], c = sizes[i][1];
		char **f = render_frame(&a, "repo", "trunk", true, r, c);
		free_frame(f, r);
		overlay_open_help(&a.ov);
		f = render_frame(&a, "repo", "trunk", true, r, c);
		free_frame(f, r);
		overlay_close(&a.ov);
	}
	CHECK(1); /* reaching here without a sanitizer trip is the test */

	app_free(&a);
}

void test_render_frame_empty_clean(void)
{
	app a;
	app_init(&a);
	app_reflatten(&a); /* no files: clean / empty working tree */

	char **f = render_frame(&a, "repo", "trunk", false, 8, 80);
	bool clean = false;
	for (int i = 0; i < 8; i++)
		if (strstr(f[i], "working tree clean"))
			clean = true;
	CHECK(clean);
	free_frame(f, 8);

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

void test_render_frame_filter_in_footer(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "file", ST_UNSTAGED);
	app_reflatten(&a);

	/* Empty filter: the nav footer row shows the "type:filter" hint. */
	char **f = render_frame(&a, "r", "b", false, 6, 60);
	CHECK(strstr(f[0], "/") == NULL); /* header has no query */
	CHECK(strstr(f[4], "type:filter") != NULL);
	free_frame(f, 6);

	/* Typed: the live text replaces "filter" in the footer slot. */
	app_filter_push(&a, 'f');
	app_filter_push(&a, 'i');
	f = render_frame(&a, "r", "b", false, 6, 60);
	CHECK(strstr(f[4], "type:fi") != NULL);
	CHECK(strstr(f[4], "type:filter") == NULL);
	free_frame(f, 6);

	app_free(&a);
}

void test_render_overlay_commit(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "file.c", 0);
	app_reflatten(&a);
	overlay_open_commit(&a.ov, false, "fix: thing");

	char **f = render_frame(&a, "r", "b", false, 12, 60);
	bool title = false, text = false;
	for (int i = 0; i < 12; i++) {
		if (strstr(f[i], "Commit message"))
			title = true;
		if (strstr(f[i], "fix: thing"))
			text = true;
	}
	CHECK(title);
	CHECK(text);
	free_frame(f, 12);
	app_free(&a);
}

void test_render_overlay_help(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "x", 0);
	app_reflatten(&a);
	overlay_open_help(&a.ov);

	char **f = render_frame(&a, "r", "b", false, 28, 70);
	bool title = false, git = false, stash = false, status = false,
	     close = false;
	for (int i = 0; i < 28; i++) {
		if (strstr(f[i], "Keys"))
			title = true;
		if (strstr(f[i], "A stage"))
			git = true;
		if (strstr(f[i], "W stash"))
			stash = true; /* stash-all bind listed */
		if (strstr(f[i], "staged") && strstr(f[i], "modified"))
			status = true; /* the status legend */
		if (strstr(f[i], "any key to close"))
			close = true;
	}
	CHECK(title);
	CHECK(git);
	CHECK(stash);
	CHECK(status);
	CHECK(close);
	free_frame(f, 28);
	app_free(&a);
}

void test_render_overlay_remote(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "x", 0);
	app_reflatten(&a);
	char *names[] = {"origin", "upstream"};
	overlay_open_remote(&a.ov, names, 2, NET_PUSH);

	char **f = render_frame(&a, "r", "b", false, 14, 60);
	bool title = false, both = false, marker = false;
	for (int i = 0; i < 14; i++) {
		if (strstr(f[i], "Push to"))
			title = true;
		if (strstr(f[i], "origin") && strstr(f[i], "\342\206\222"))
			marker = true; /* selected remote has the arrow */
	}
	/* both remotes listed across the frame */
	for (int i = 0; i < 14; i++)
		if (strstr(f[i], "upstream"))
			both = true;
	CHECK(title);
	CHECK(marker);
	CHECK(both);
	free_frame(f, 14);
	app_free(&a);
}

void test_render_overlay_confirm(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "x", 0);
	app_reflatten(&a);
	overlay_open_confirm(&a.ov, "Delete x?");

	char **f = render_frame(&a, "r", "b", false, 12, 60);
	bool prompt = false, yn = false;
	for (int i = 0; i < 12; i++) {
		if (strstr(f[i], "Delete x?"))
			prompt = true;
		if (strstr(f[i], "y: yes"))
			yn = true;
	}
	CHECK(prompt);
	CHECK(yn);
	free_frame(f, 12);
	app_free(&a);
}

/* A long message wraps to multiple content rows: the box grows. */
void test_render_overlay_grows(void)
{
	app a;
	app_init(&a);
	tree_add(&a.t, "x", 0);
	app_reflatten(&a);
	char msg[160];
	memset(msg, 'a', 150);
	msg[150] = '\0';
	overlay_open_commit(&a.ov, false, msg);

	char **f = render_frame(&a, "r", "b", false, 20, 60);
	int content_rows = 0;
	for (int i = 0; i < 20; i++)
		if (strstr(f[i], "aaaa"))
			content_rows++;
	CHECK(content_rows >= 2); /* wrapped -> box taller than one line */
	free_frame(f, 20);
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
