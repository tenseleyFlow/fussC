#include "git.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Full workflow through our own API: stage -> commit -> push to a local bare
 * remote, then a second clone pushes a change and we pull it back. No network,
 * no `git` CLI for the mutations themselves (only to scaffold the dirs and read
 * results back). Skips when `git` is absent.
 */

static bool have_git(void)
{
	return system("command -v git >/dev/null 2>&1") == 0;
}

static bool read_first_line(const char *path, char *out, size_t n)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return false;
	bool ok = fgets(out, (int)n, fp) != NULL;
	fclose(fp);
	if (ok) {
		size_t l = strlen(out);
		while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r'))
			out[--l] = '\0';
	}
	return ok;
}

void test_e2e_stage_commit_push_pull(void)
{
	if (!have_git()) {
		fprintf(stderr, "  SKIP test_e2e (no git CLI)\n");
		return;
	}

	char base[256];
	snprintf(base, sizeof(base), "/tmp/fussy_e2e_%ld", (long)getpid());

	/* Bare remote + a "work" clone (ours) + a "peer" clone (the upstream
	 * author). The work clone starts from an initial commit already on the
	 * remote so it has an upstream to push to. */
	char cmd[3200];
	snprintf(
	    cmd, sizeof(cmd),
	    "rm -rf '%s' && mkdir -p '%s' && cd '%s' && "
	    "git -c init.defaultBranch=master init -q --bare bare && "
	    "git -c init.defaultBranch=master clone -q bare seed && cd seed && "
	    "git config user.email t@t && git config user.name t && "
	    "printf base > a.txt && git add a.txt && git commit -qm init && "
	    "git push -q -u origin master && cd '%s' && "
	    "git clone -q bare work && cd work && "
	    "git config user.email t@t && git config user.name t && "
	    "cd '%s' && git clone -q bare peer && cd peer && "
	    "git config user.email t@t && git config user.name t",
	    base, base, base, base, base);
	CHECK(system(cmd) == 0);

	char work[320], peer[320], path[400];
	snprintf(work, sizeof(work), "%s/work", base);
	snprintf(peer, sizeof(peer), "%s/peer", base);

	char cwd[2048];
	CHECK(getcwd(cwd, sizeof(cwd)) != NULL);

	/* --- our side: create a file, stage + commit + push through
	 * gitop/gitnet
	 */
	CHECK(chdir(work) == 0);
	CHECK(system("printf ours > b.txt") == 0);

	git_ctx g;
	char err[256], msg[256];
	if (git_open(&g, err, sizeof(err))) {
		CHECK(gitop_stage(&g, "b.txt", err, sizeof(err)) == 0);
		CHECK(gitop_commit(&g, "add b", err, sizeof(err)) == 0);
		CHECK(gitnet_push(&g, NULL, msg, sizeof(msg)) == 0);
		git_close(&g);
	} else {
		CHECK(0);
	}

	/* The remote now has our commit. */
	CHECK(system("git --git-dir='" /* */
	             "../bare' log --all --oneline | grep -q 'add b'") == 0);

	/* --- peer side: sync to our push first, then change a.txt and push
	 * (this becomes the incoming upstream change we pull below). */
	CHECK(chdir(peer) == 0);
	CHECK(system("git pull -q --no-rebase && "
	             "printf changed > a.txt && git add a.txt && "
	             "git commit -qm peer && git push -q") == 0);

	/* --- our side again: pull it back and confirm the working file updated
	 */
	CHECK(chdir(work) == 0);
	if (git_open(&g, err, sizeof(err))) {
		CHECK(gitnet_pull(&g, NULL, msg, sizeof(msg)) == 0);
		git_close(&g);
	} else {
		CHECK(0);
	}
	snprintf(path, sizeof(path), "%s/a.txt", work);
	char line[64] = {0};
	CHECK(read_first_line(path, line, sizeof(line)));
	CHECK_STR_EQ(line, "changed");

	CHECK(chdir(cwd) == 0);
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", base);
	int rc = system(cmd);
	(void)rc;
}
