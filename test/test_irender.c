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

	char **f = render_frame(&a, "r", "b", false, 24, 70);
	bool title = false, git = false, close = false;
	for (int i = 0; i < 24; i++) {
		if (strstr(f[i], "Keys"))
			title = true;
		if (strstr(f[i], "A stage"))
			git = true;
		if (strstr(f[i], "any key to close"))
			close = true;
	}
	CHECK(title);
	CHECK(git);
	CHECK(close);
	free_frame(f, 24);
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
