#include "git.h"
#include "test.h"
#include "tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* These tests build a real repo (with an initial commit and a user identity so
 * libgit2 can sign commits), run a mutation, and assert the result by reading
 * status back through git_load_tree. They skip when the `git` CLI is absent. */

static bool have_git(void)
{
	return system("command -v git >/dev/null 2>&1") == 0;
}

static char g_cwd[2048];
static char g_dir[256];

static bool enter_repo(const char *tag)
{
	snprintf(g_dir, sizeof(g_dir), "/tmp/fussy_ops_%s_%ld", tag,
	         (long)getpid());
	char cmd[2400];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && git init -q && "
	         "git config user.email t@t.t && git config user.name Test && "
	         "printf base > base.txt && git add base.txt && "
	         "git commit -q -m init",
	         g_dir, g_dir, g_dir);
	if (system(cmd) != 0)
		return false;
	if (getcwd(g_cwd, sizeof(g_cwd)) == NULL)
		return false;
	return chdir(g_dir) == 0;
}

static void leave_repo(void)
{
	if (chdir(g_cwd) != 0)
		(void)0;
	char cmd[2400];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_dir);
	int rc = system(cmd);
	(void)rc;
}

static void write_file(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");
	if (f) {
		fputs(s, f);
		fclose(f);
	}
}

static bool file_is(const char *path, const char *want)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	char buf[256] = {0};
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return strcmp(buf, want) == 0;
}

static bool exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static file_status status_of(git_ctx *g, const char *path)
{
	tree t;
	tree_init(&t);
	git_load_tree(g, &t, false);
	uint32_t i = tree_find(&t, path);
	file_status s = (i == NODE_NIL) ? 0 : t.nodes[i].status;
	tree_free(&t);
	return s;
}

#define SKIP_NO_GIT(name)                                                      \
	if (!have_git()) {                                                     \
		fprintf(stderr, "  SKIP %s (no git CLI)\n", name);             \
		return;                                                        \
	}

void test_gitop_stage_unstage(void)
{
	SKIP_NO_GIT("test_gitop_stage_unstage");
	if (!enter_repo("stage")) {
		CHECK(0);
		return;
	}
	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		write_file("base.txt", "changed");
		CHECK(gitop_stage(&g, "base.txt", err, sizeof(err)) == 0);
		CHECK((status_of(&g, "base.txt") & ST_STAGED) != 0);

		CHECK(gitop_unstage(&g, "base.txt", err, sizeof(err)) == 0);
		file_status s = status_of(&g, "base.txt");
		CHECK((s & ST_UNSTAGED) != 0 && (s & ST_STAGED) == 0);
		git_close(&g);
	} else {
		CHECK(0);
	}
	leave_repo();
}

void test_gitop_stage_all(void)
{
	SKIP_NO_GIT("test_gitop_stage_all");
	if (!enter_repo("stageall")) {
		CHECK(0);
		return;
	}
	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		write_file("base.txt", "x");
		write_file("two.txt", "y"); /* untracked */
		CHECK(gitop_stage_all(&g, err, sizeof(err)) == 0);
		CHECK((status_of(&g, "base.txt") & ST_STAGED) != 0);
		CHECK((status_of(&g, "two.txt") & ST_STAGED) != 0);

		CHECK(gitop_unstage_all(&g, err, sizeof(err)) == 0);
		CHECK((status_of(&g, "base.txt") & ST_STAGED) == 0);
		CHECK((status_of(&g, "two.txt") & ST_UNTRACKED) != 0);
		git_close(&g);
	} else {
		CHECK(0);
	}
	leave_repo();
}

void test_gitop_commit_amend(void)
{
	SKIP_NO_GIT("test_gitop_commit_amend");
	if (!enter_repo("commit")) {
		CHECK(0);
		return;
	}
	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		write_file("base.txt", "v2");
		CHECK(gitop_stage(&g, "base.txt", err, sizeof(err)) == 0);
		CHECK(gitop_commit(&g, "second", err, sizeof(err)) == 0);
		CHECK(status_of(&g, "base.txt") == 0); /* clean now */
		char *m = gitop_last_message(&g);
		CHECK(m != NULL && strstr(m, "second") != NULL);
		free(m);

		write_file("base.txt", "v3");
		CHECK(gitop_stage(&g, "base.txt", err, sizeof(err)) == 0);
		CHECK(gitop_amend(&g, "second (reworded)", err, sizeof(err)) ==
		      0);
		m = gitop_last_message(&g);
		CHECK(m != NULL && strstr(m, "reworded") != NULL);
		free(m);
		git_close(&g);
	} else {
		CHECK(0);
	}
	leave_repo();
}

void test_gitop_discard(void)
{
	SKIP_NO_GIT("test_gitop_discard");
	if (!enter_repo("discard")) {
		CHECK(0);
		return;
	}
	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		write_file("base.txt", "garbage");
		CHECK(gitop_discard(&g, "base.txt", false, err, sizeof(err)) ==
		      0);
		CHECK(file_is("base.txt", "base")); /* restored from HEAD */
		CHECK(status_of(&g, "base.txt") == 0);

		write_file("scratch.txt", "tmp"); /* untracked */
		CHECK(gitop_discard(&g, "scratch.txt", true, err,
		                    sizeof(err)) == 0);
		CHECK(!exists("scratch.txt"));
		git_close(&g);
	} else {
		CHECK(0);
	}
	leave_repo();
}

void test_gitop_delete(void)
{
	SKIP_NO_GIT("test_gitop_delete");
	if (!enter_repo("delete")) {
		CHECK(0);
		return;
	}
	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		CHECK(gitop_delete(&g, "base.txt", false, err, sizeof(err)) ==
		      0);
		CHECK(!exists("base.txt"));
		CHECK((status_of(&g, "base.txt") & ST_STAGED) !=
		      0); /* del staged */

		write_file("scratch.txt", "tmp");
		CHECK(gitop_delete(&g, "scratch.txt", true, err, sizeof(err)) ==
		      0);
		CHECK(!exists("scratch.txt"));
		git_close(&g);
	} else {
		CHECK(0);
	}
	leave_repo();
}

void test_gitop_rename(void)
{
	SKIP_NO_GIT("test_gitop_rename");
	if (!enter_repo("rename")) {
		CHECK(0);
		return;
	}
	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		CHECK(gitop_rename(&g, "base.txt", "renamed.txt", err,
		                   sizeof(err)) == 0);
		CHECK(!exists("base.txt"));
		CHECK(exists("renamed.txt"));
		/* The index tracked the move, so the new path is staged. */
		CHECK((status_of(&g, "renamed.txt") & ST_STAGED) != 0);
		git_close(&g);
	} else {
		CHECK(0);
	}
	leave_repo();
}

void test_gitop_tag(void)
{
	SKIP_NO_GIT("test_gitop_tag");
	if (!enter_repo("tag")) {
		CHECK(0);
		return;
	}
	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		CHECK(gitop_tag(&g, "v1", NULL, err, sizeof(err)) == 0);
		CHECK(gitop_tag(&g, "v2", "annotated", err, sizeof(err)) == 0);
		CHECK(system("git rev-parse refs/tags/v1 >/dev/null 2>&1") ==
		      0);
		CHECK(system("git rev-parse refs/tags/v2 >/dev/null 2>&1") ==
		      0);
		git_close(&g);
	} else {
		CHECK(0);
	}
	leave_repo();
}
