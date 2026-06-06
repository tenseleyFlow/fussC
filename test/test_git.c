#include "git.h"
#include "test.h"
#include "tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * These tests build a real repository in a temp dir with the `git` CLI, then
 * read it back through our libgit2 wrapper. No commits are needed: a staged new
 * file shows as INDEX_NEW even before the first commit. The temp dir is named
 * from the pid (mkdtemp is hidden under strict _POSIX_C_SOURCE on macOS/BSD).
 */

static void temp_dir(char *buf, size_t n, const char *tag)
{
	snprintf(buf, n, "/tmp/fussy_%s_%ld", tag, (long)getpid());
}

/* The fixtures need the `git` CLI to build a repo. Skip (don't fail) when it is
 * absent so the suite still runs on a minimal box. */
static bool have_git(void)
{
	return system("command -v git >/dev/null 2>&1") == 0;
}

static void cleanup(const char *dir)
{
	char cmd[2100];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	int rc = system(cmd);
	(void)rc;
}

static bool has_bit(const tree *t, const char *path, file_status bit)
{
	uint32_t i = tree_find(t, path);
	return i != NODE_NIL && (t->nodes[i].status & bit) != 0;
}

void test_git_status(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_status (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "git");

	char cmd[2300];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && git init -q && "
	         "printf a > staged.txt && "
	         "printf b > untracked.txt && "
	         "mkdir sub && printf c > sub/nested.txt && "
	         "git add staged.txt sub/nested.txt",
	         dir, dir, dir);
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
	cleanup(dir);
}

void test_git_ignored(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_ignored (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitign");

	/* An ignored directory must surface as a single dimmed node, not a
	 * flood of every artifact under it. */
	char cmd[2300];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && git init -q && "
	         "printf 'build/\\n' > .gitignore && "
	         "mkdir build && printf x > build/a.o && printf y > build/b.o",
	         dir, dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		tree t;
		tree_init(&t);
		CHECK(git_load_tree(&g, &t, false) == 0);

		CHECK(has_bit(&t, "build", ST_GITIGNORED));
		/* Not recursed: the artifacts under build/ are absent. */
		CHECK(tree_find(&t, "build/a.o") == NODE_NIL);
		CHECK(tree_find(&t, "build/b.o") == NODE_NIL);

		tree_free(&t);
		git_close(&g);
	}

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}

void test_git_incoming(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_incoming (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitinc");

	/* A bare remote, a clone that commits + pushes (the "upstream" change),
	 * and our working clone that only fetches. git_mark_incoming must flag
	 * exactly what the fetched upstream changed relative to HEAD. */
	char cmd[3000];
	snprintf(
	    cmd, sizeof(cmd),
	    "rm -rf '%s' && mkdir -p '%s' && cd '%s' && "
	    "git -c init.defaultBranch=master init -q --bare bare && "
	    "git -c init.defaultBranch=master clone -q bare up && cd up && "
	    "git config user.email t@t && git config user.name t && "
	    "printf one > a.txt && git add a.txt && git commit -qm init && "
	    "git push -q -u origin master && cd '%s' && "
	    "git clone -q bare work && cd work && "
	    "git config user.email t@t && git config user.name t && "
	    "cd '%s/up' && printf onemore > a.txt && printf two > b.txt && "
	    "git add -A && git commit -qm upstream && git push -q && "
	    "cd '%s/work' && git fetch -q",
	    dir, dir, dir, dir, dir, dir);
	CHECK(system(cmd) == 0);

	char workdir[320];
	snprintf(workdir, sizeof(workdir), "%s/work", dir);
	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(workdir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		tree t;
		tree_init(&t);
		CHECK(git_load_tree(&g, &t, true) == 0);
		git_mark_incoming(&g, &t);

		CHECK(
		    has_bit(&t, "a.txt", ST_INCOMING)); /* modified upstream */
		CHECK(has_bit(&t, "b.txt", ST_INCOMING)); /* added upstream */

		tree_free(&t);
		git_close(&g);
	}

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}

void test_git_log(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_log (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitlog");

	char cmd[2300];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && git init -q && "
	         "git config user.email t@t && git config user.name t && "
	         "printf 1 > f && git add f && git commit -qm first && "
	         "printf 2 > f && git commit -qam second && "
	         "printf 3 > f && git commit -qam third",
	         dir, dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		git_log_list log = git_log(&g, 0);
		CHECK(log.count == 3);
		if (log.count == 3) {
			/* newest first; lines carry the summary, shas are full.
			 */
			CHECK(strstr(log.lines[0], "third") != NULL);
			CHECK(strstr(log.lines[2], "first") != NULL);
			CHECK(strlen(log.shas[0]) == 40);
		}
		/* max caps the walk. */
		git_log_free(&log);
		git_log_list two = git_log(&g, 2);
		CHECK(two.count == 2);
		git_log_free(&two);

		git_close(&g);
	}

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}

void test_git_not_a_repo(void)
{
	char dir[256];
	temp_dir(dir, sizeof(dir), "nogit");

	char cmd[2300];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'", dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	err[0] = '\0';
	CHECK(!git_open(&g, err, sizeof(err)));
	CHECK(err[0] != '\0'); /* a message was produced */

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}
