#include "git.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Exercises push/pull/fetch against a local bare repo acting as the remote, so
 * no network is needed. Skips when the `git` CLI is absent. */

static bool have_git(void)
{
	return system("command -v git >/dev/null 2>&1") == 0;
}

void test_gitnet_push_fetch_pull(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_gitnet (no git CLI)\n");
		return;
	}

	char base[256];
	snprintf(base, sizeof(base), "/tmp/fussy_net_%ld", (long)getpid());
	char bare[320], work[320], cmd[2048];
	snprintf(bare, sizeof(bare), "%s/remote.git", base);
	snprintf(work, sizeof(work), "%s/work", base);

	snprintf(cmd, sizeof(cmd),
	         "rm -rf '%s' && mkdir -p '%s' && "
	         "git init -q --bare '%s' && mkdir -p '%s' && cd '%s' && "
	         "git init -q && git config user.email t@t.t && "
	         "git config user.name T && printf a > f.txt && "
	         "git add f.txt && git commit -q -m init && "
	         "git remote add origin '%s'",
	         base, base, bare, work, work, bare);
	CHECK(system(cmd) == 0);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
	CHECK(chdir(work) == 0);

	git_ctx g;
	char err[256];
	if (git_open(&g, err, sizeof(err))) {
		char msg[256];

		CHECK(!git_has_upstream(&g)); /* none yet */

		int n = 0;
		char **r = git_remote_names(&g, &n);
		CHECK(n == 1);
		if (n == 1)
			CHECK_STR_EQ(r[0], "origin");
		for (int i = 0; i < n; i++)
			free(r[i]);
		free(r);

		/* First push sets the upstream and lands the init commit. */
		CHECK(gitnet_push(&g, "origin", msg, sizeof(msg)) == 0);
		CHECK(system("git --git-dir='" /* */
		             "../remote.git' log --all --oneline 2>/dev/null "
		             "| grep -q init") == 0);

		/* A second commit, then a no-arg push via the now-set upstream.
		 */
		CHECK(system("printf b > f.txt && git add f.txt && "
		             "git commit -q -m second") == 0);
		CHECK(gitnet_push(&g, NULL, msg, sizeof(msg)) == 0);
		CHECK(
		    system("git --git-dir='../remote.git' log --all --oneline "
		           "2>/dev/null | grep -q second") == 0);

		/* An annotated tag rides along on the next push
		 * (--follow-tags). */
		CHECK(gitop_tag(&g, "v1", "release one", err, sizeof(err)) ==
		      0);
		CHECK(gitnet_push(&g, NULL, msg, sizeof(msg)) == 0);
		CHECK(system("git --git-dir='../remote.git' tag -l 2>/dev/null "
		             "| grep -q v1") == 0);

		/* Fetch and pull are up to date (exit 0). */
		CHECK(gitnet_fetch(&g, NULL, msg, sizeof(msg)) == 0);
		CHECK(gitnet_pull(&g, NULL, msg, sizeof(msg)) == 0);

		git_close(&g);
	} else {
		CHECK(0);
	}

	CHECK(chdir(cwd) == 0);
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", base);
	int rc = system(cmd);
	(void)rc;
}
