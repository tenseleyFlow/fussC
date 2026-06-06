#include "git.h"
#include "test.h"
#include "tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* These tests build a real repository in a temp dir with the `git` CLI, then
 * read it back through our libgit2 wrapper. No commits are needed: a staged new
 * file shows as INDEX_NEW even before the first commit. */

static bool has_bit(const tree *t, const char *path, file_status bit)
{
	uint32_t i = tree_find(t, path);
	return i != NODE_NIL && (t->nodes[i].status & bit) != 0;
}

void test_git_status(void)
{
	char dir[] = "/tmp/fussy_git_XXXXXX";
	CHECK(mkdtemp(dir) != NULL);

	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
	         "cd '%s' && git init -q && "
	         "printf a > staged.txt && "
	         "printf b > untracked.txt && "
	         "mkdir sub && printf c > sub/nested.txt && "
	         "git add staged.txt sub/nested.txt",
	         dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	bool opened = git_open(&g, err, sizeof(err));
	CHECK(opened);
	if (opened) {
		tree t;
		tree_init(&t);
		CHECK(git_load_tree(&g, &t, false) == 0);

		CHECK(has_bit(&t, "staged.txt", ST_STAGED));
		CHECK(has_bit(&t, "untracked.txt", ST_UNTRACKED));
		CHECK(has_bit(&t, "sub/nested.txt", ST_STAGED));
		/* The "sub" directory node exists with no status of its own. */
		CHECK(tree_find(&t, "sub") != NODE_NIL);

		CHECK(g.repo_name != NULL && g.branch != NULL);

		tree_free(&t);
		git_close(&g);
	}

	CHECK(chdir(cwd) == 0);
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	(void)system(cmd);
}

void test_git_not_a_repo(void)
{
	char dir[] = "/tmp/fussy_nogit_XXXXXX";
	CHECK(mkdtemp(dir) != NULL);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	err[0] = '\0';
	CHECK(!git_open(&g, err, sizeof(err)));
	CHECK(err[0] != '\0'); /* a message was produced */

	CHECK(chdir(cwd) == 0);
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	(void)system(cmd);
}
