#ifndef FUSSY_GIT_H
#define FUSSY_GIT_H

#include <stdbool.h>
#include <stddef.h>

#include "tree.h"

/*
 * Thin wrapper over libgit2 for the read paths fussy needs. Local mutations and
 * the network ops are added in later sprints; this covers repo discovery, the
 * status scan, and repo/branch identity.
 */
typedef struct {
	void *repo;      /* git_repository*, opaque to callers */
	char *repo_name; /* working-dir basename, owned */
	char *branch;    /* current branch short name, owned (may be "HEAD") */
} git_ctx;

/*
 * Discover and open the repository containing the current directory. On failure
 * returns false and writes a terse message into errbuf. On success the caller
 * must later call git_close.
 */
bool git_open(git_ctx *g, char *errbuf, size_t errlen);
void git_close(git_ctx *g);

/*
 * Populate an initialised tree with the repository's file set and per-file
 * status. When `all` is false, only dirty/untracked files are included; when
 * true, all tracked files are added as well (clean ones with no status bits).
 * Returns 0 on success, -1 on error.
 */
int git_load_tree(git_ctx *g, tree *t, bool all);

#endif /* FUSSY_GIT_H */
