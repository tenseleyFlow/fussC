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
	    "cd '%s/work' && git fetch -q && "
	    /* a local-only commit: its file must NOT be flagged incoming */
	    "printf mine > local.txt && git add local.txt && "
	    "git commit -qm localwork",
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
		/* Our own local-only commit is NOT incoming (the bug: a
		 * HEAD->upstream diff flags it as a deletion). */
		CHECK(!has_bit(&t, "local.txt", ST_INCOMING));

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

void test_git_reflog(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_reflog (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitrl");

	char cmd[2300];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && git init -q && "
	         "git config user.email t@t && git config user.name t && "
	         "printf 1 > f && git add f && git commit -qm first && "
	         "printf 2 > f && git commit -qam second",
	         dir, dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		git_log_list rl = git_reflog_list(&g, 0);
		CHECK(rl.count >= 2); /* two commits -> two reflog entries */
		if (rl.count >= 1) {
			CHECK(strstr(rl.lines[0], "HEAD@{0}") != NULL);
			CHECK(strlen(rl.shas[0]) == 40);
		}
		git_log_free(&rl);
		git_close(&g);
	}

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}

void test_git_branches(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_branches (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitbr");

	char cmd[2300];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && "
	         "git -c init.defaultBranch=master init -q && "
	         "git config user.email t@t && git config user.name t && "
	         "printf 1 > f && git add f && git commit -qm first && "
	         "git branch feature",
	         dir, dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		git_branchlist bl = git_branches(&g);
		CHECK(bl.count == 2);
		bool current = false, feature = false;
		for (int i = 0; i < bl.count; i++) {
			if (bl.display[i][0] == '*')
				current = true; /* HEAD marked */
			if (strcmp(bl.names[i], "feature") == 0)
				feature = true;
		}
		CHECK(current);
		CHECK(feature);
		git_branchlist_free(&bl);

		/* Switch to feature; HEAD follows. */
		CHECK(gitop_checkout(&g, "feature", err, sizeof(err)) == 0);
		git_reload_head(&g);
		CHECK_STR_EQ(g.branch, "feature");

		/* Create a branch at HEAD; it shows up. */
		CHECK(gitop_branch_create(&g, "newbr", err, sizeof(err)) == 0);
		git_branchlist b2 = git_branches(&g);
		bool has_new = false;
		for (int i = 0; i < b2.count; i++)
			if (strcmp(b2.names[i], "newbr") == 0)
				has_new = true;
		CHECK(has_new);
		git_branchlist_free(&b2);

		/* Delete refuses the current branch, allows a merged one. */
		CHECK(gitop_branch_delete(&g, "feature", err, sizeof(err)) !=
		      0);
		CHECK(gitop_branch_delete(&g, "newbr", err, sizeof(err)) == 0);
		git_branchlist b3 = git_branches(&g);
		bool gone = true;
		for (int i = 0; i < b3.count; i++)
			if (strcmp(b3.names[i], "newbr") == 0)
				gone = false;
		CHECK(gone);
		git_branchlist_free(&b3);

		git_close(&g);
	}

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}

void test_git_stashes(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_stashes (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitst");

	char cmd[2300];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && git init -q && "
	         "git config user.email t@t && git config user.name t && "
	         "printf 1 > f && git add f && git commit -qm first && "
	         "printf 2 > f", /* an uncommitted change to stash */
	         dir, dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		/* No stashes yet. */
		git_stashlist s0 = git_stashes(&g);
		CHECK(s0.count == 0);
		git_stashlist_free(&s0);

		/* Push: stashes the dirty change; the entry appears. */
		CHECK(gitop_stash_push(&g, "wip", err, sizeof(err)) == 0);
		git_stashlist s1 = git_stashes(&g);
		CHECK(s1.count == 1);
		if (s1.count == 1)
			CHECK(strstr(s1.display[0], "wip") != NULL);
		git_stashlist_free(&s1);

		/* Pop: applies and drops; the list is empty again. */
		CHECK(gitop_stash_pop(&g, 0, err, sizeof(err)) == 0);
		git_stashlist s2 = git_stashes(&g);
		CHECK(s2.count == 0);
		git_stashlist_free(&s2);

		/* Nothing to stash now (change is back in the worktree... it
		 * is, so push again works; then drop without applying). */
		CHECK(gitop_stash_push(&g, NULL, err, sizeof(err)) == 0);
		CHECK(gitop_stash_drop(&g, 0, err, sizeof(err)) == 0);
		git_stashlist s3 = git_stashes(&g);
		CHECK(s3.count == 0);
		git_stashlist_free(&s3);

		git_close(&g);
	}

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}

void test_git_reset(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_reset (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitrst");

	char cmd[2300];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && git init -q && "
	         "git config user.email t@t && git config user.name t && "
	         "printf 1 > f && git add f && git commit -qm first && "
	         "printf 2 > f && git commit -qam second",
	         dir, dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		/* Two commits; reset --hard to the parent leaves one and a
		 * clean worktree (f == "1"). */
		CHECK(gitop_reset(&g, "HEAD~1", RESET_HARD, err, sizeof(err)) ==
		      0);
		git_log_list log = git_log(&g, 0);
		CHECK(log.count == 1);
		git_log_free(&log);

		git_close(&g);
	}

	char content[16] = {0};
	char fpath[320];
	snprintf(fpath, sizeof(fpath), "%s/f", dir);
	FILE *fp = fopen(fpath, "r");
	if (fp) {
		(void)(fgets(content, sizeof(content), fp) != NULL);
		fclose(fp);
	}
	CHECK_STR_EQ(content, "1"); /* hard reset restored the file */

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}

void test_git_cherrypick_revert(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_cherrypick_revert (no git)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitcp");

	/* master has a.txt; a side branch adds b.txt. Cherry-pick the side
	 * commit onto master, then revert it. */
	char cmd[2600];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && "
	         "git -c init.defaultBranch=master init -q && "
	         "git config user.email t@t && git config user.name t && "
	         "printf a > a.txt && git add a.txt && git commit -qm base && "
	         "git checkout -q -b side && "
	         "printf b > b.txt && git add b.txt && git commit -qm addb && "
	         "git checkout -q master",
	         dir, dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		char bpath[320];
		snprintf(bpath, sizeof(bpath), "%s/b.txt", dir);

		/* All-branches log reaches side's commit; HEAD-only does not.
		 */
		git_log_list all = git_log_all(&g, 0);
		git_log_list head = git_log(&g, 0);
		CHECK(all.count > head.count);
		git_log_free(&all);
		git_log_free(&head);

		/* Cherry-pick side's commit: b.txt appears, a new commit lands.
		 */
		CHECK(gitop_cherrypick(&g, "side", err, sizeof(err)) == 0);
		CHECK(access(bpath, F_OK) == 0); /* b.txt now present */
		git_log_list l1 = git_log(&g, 0);
		CHECK(l1.count == 2); /* base + cherry-picked */
		if (l1.count >= 1)
			CHECK(strstr(l1.lines[0], "addb") != NULL);
		git_log_free(&l1);

		/* Revert it: b.txt goes away, another commit lands. */
		CHECK(gitop_revert(&g, "HEAD", err, sizeof(err)) == 0);
		CHECK(access(bpath, F_OK) != 0); /* b.txt removed */
		git_log_list l2 = git_log(&g, 0);
		CHECK(l2.count == 3);
		if (l2.count >= 1)
			CHECK(strstr(l2.lines[0], "Revert") != NULL);
		git_log_free(&l2);

		/* Stage-only cherry-pick: applies but does NOT commit; b.txt is
		 * back and staged, the commit count is unchanged. */
		CHECK(gitop_cherrypick_nocommit(&g, "side", err, sizeof(err)) ==
		      0);
		CHECK(access(bpath, F_OK) == 0);
		git_log_list l4 = git_log(&g, 0);
		CHECK(l4.count == 3); /* no new commit */
		git_log_free(&l4);
		tree t;
		tree_init(&t);
		CHECK(git_load_tree(&g, &t, false) == 0);
		CHECK(has_bit(&t, "b.txt",
		              ST_STAGED)); /* staged, not committed */
		tree_free(&t);

		git_close(&g);
	}

	CHECK(chdir(cwd) == 0);
	cleanup(dir);
}

void test_git_merge(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_git_merge (no git CLI)\n");
		return;
	}

	char dir[256];
	temp_dir(dir, sizeof(dir), "gitmrg");

	/* master edits a.txt; side adds b.txt from the same base -> a true
	 * (non-fast-forward) merge with no conflict. */
	char cmd[2600];
	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && cd '%s' && "
	         "git -c init.defaultBranch=master init -q && "
	         "git config user.email t@t && git config user.name t && "
	         "printf a > a.txt && git add a.txt && git commit -qm base && "
	         "git checkout -q -b side && "
	         "printf b > b.txt && git add b.txt && git commit -qm sideb && "
	         "git checkout -q master && "
	         "printf a2 > a.txt && git commit -qam edita",
	         dir, dir, dir);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(dir) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		char bpath[320];
		snprintf(bpath, sizeof(bpath), "%s/b.txt", dir);

		CHECK(gitop_merge(&g, "side", err, sizeof(err)) == 0);
		CHECK(access(bpath, F_OK) == 0); /* side's b.txt merged in */
		git_log_list log = git_log(&g, 0);
		/* base, sideb, edita, merge commit. */
		CHECK(log.count == 4);
		if (log.count >= 1)
			CHECK(strstr(log.lines[0], "Merge branch 'side'") !=
			      NULL);
		git_log_free(&log);

		/* Merging an already-merged branch is "up to date". */
		CHECK(gitop_merge(&g, "side", err, sizeof(err)) != 0);

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
