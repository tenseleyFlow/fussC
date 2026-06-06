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

/*
 * OR ST_INCOMING onto every path the fetched upstream changed relative to HEAD,
 * giving those nodes a down glyph. No-op when there is no upstream. Run after
 * git_load_tree so it merges into the already-populated arena.
 */
void git_mark_incoming(git_ctx *g, tree *t);

/*
 * A commit listing for the history browser. `lines[i]` is a display string
 * ("<abbrev>  <summary>"); `shas[i]` is the matching full hex SHA. Both arrays
 * have `count` entries. Built with a libgit2 revwalk - no subprocess, no
 * parsing.
 */
typedef struct {
	char **lines;
	char **shas;
	int count;
} git_log_list;

/* Walk from HEAD newest-first, up to `max` commits (0 = no cap). Returns an
 * empty list on an unborn/empty repo. Free with git_log_free. */
git_log_list git_log(git_ctx *g, int max);
void git_log_free(git_log_list *l);

/* HEAD's reflog as the same list shape (newest first, up to `max`): `lines[i]`
 * is "<abbrev> HEAD@{i}: <message>", `shas[i]` the entry's resulting commit.
 * Free with git_log_free. */
git_log_list git_reflog_list(git_ctx *g, int max);

/* Local branches for the branch browser. `display[i]` is "* name" for the
 * current branch else "  name"; `names[i]` is the bare name (for checkout). */
typedef struct {
	char **display;
	char **names;
	int count;
} git_branchlist;

git_branchlist git_branches(git_ctx *g);
void git_branchlist_free(git_branchlist *b);

/* Re-read HEAD's short branch name into g->branch (after a checkout). */
void git_reload_head(git_ctx *g);

/* Check out a local branch: update the worktree+index to it (refusing if that
 * would clobber uncommitted changes) and move HEAD. 0 on success, else -1 with
 * a terse message. */
int gitop_checkout(git_ctx *g, const char *branch, char *err, size_t errlen);

/* Create a local branch at HEAD. Fails if it already exists. */
int gitop_branch_create(git_ctx *g, const char *name, char *err, size_t errlen);
/* Delete a local branch. Refuses the current branch and any branch not fully
 * merged into HEAD (mirroring `git branch -d`). */
int gitop_branch_delete(git_ctx *g, const char *name, char *err, size_t errlen);

/* Stashes for the stash browser. `display[i]` is "stash@{i}: <message>"; the
 * stash index for the ops below is the array position i. */
typedef struct {
	char **display;
	int count;
} git_stashlist;

git_stashlist git_stashes(git_ctx *g);
void git_stashlist_free(git_stashlist *s);

/* Stash a new entry from the current changes (message may be NULL/empty). */
int gitop_stash_push(git_ctx *g, const char *message, char *err, size_t errlen);
/* Apply stash `index` to the worktree, keeping it in the stash list. */
int gitop_stash_apply(git_ctx *g, size_t index, char *err, size_t errlen);
/* Apply stash `index` and drop it (git stash pop). */
int gitop_stash_pop(git_ctx *g, size_t index, char *err, size_t errlen);
/* Drop stash `index` without applying. */
int gitop_stash_drop(git_ctx *g, size_t index, char *err, size_t errlen);

/* Reset modes for gitop_reset (values match the order shown in the UI). */
enum { RESET_MIXED, RESET_SOFT, RESET_HARD };

/* Move HEAD to the commit `rev` resolves to. MIXED resets the index (keeps the
 * worktree), SOFT keeps index + worktree, HARD also overwrites the worktree
 * (destructive). 0 on success, else -1 with a message. */
int gitop_reset(git_ctx *g, const char *rev, int mode, char *err,
                size_t errlen);

/*
 * Local mutations, all libgit2 in-process so the index is touched by ONE
 * mechanism (fixing fussr's libgit2/subprocess split). Each returns 0 on
 * success or -1 with a terse message in errbuf. Paths are repo-relative.
 */
int gitop_stage(git_ctx *g, const char *path, char *err,
                size_t errlen); /* file or dir */
int gitop_stage_all(git_ctx *g, char *err, size_t errlen);
int gitop_unstage(git_ctx *g, const char *path, char *err, size_t errlen);
int gitop_unstage_all(git_ctx *g, char *err, size_t errlen);
int gitop_commit(git_ctx *g, const char *message, char *err, size_t errlen);
int gitop_amend(git_ctx *g, const char *message, char *err, size_t errlen);

/* The HEAD commit's message (heap, caller frees), or NULL if there is no HEAD.
 * Used to prefill the amend editor. */
char *gitop_last_message(git_ctx *g);

/* Discard worktree changes: restore from HEAD (tracked) or delete (untracked).
 */
int gitop_discard(git_ctx *g, const char *path, bool untracked, char *err,
                  size_t errlen);
/* Remove a file from the index (tracked) and unlink it from the worktree. */
int gitop_delete(git_ctx *g, const char *path, bool untracked, char *err,
                 size_t errlen);
/* Filesystem rename plus an index update when the old path was tracked, so the
 * index stays consistent (fixing fussr's bare `mv`). */
int gitop_rename(git_ctx *g, const char *oldpath, const char *newpath,
                 char *err, size_t errlen);
/* Create a tag on HEAD: annotated when message is non-empty, else lightweight.
 */
int gitop_tag(git_ctx *g, const char *name, const char *message, char *err,
              size_t errlen);

/*
 * Network operations. Queries use libgit2 (in-process); the actual push/pull/
 * fetch shell out to `git` so the user's credential helpers, SSH config, and
 * git config all apply. Each fills `msg` with a friendly result (success note
 * or mapped error) and returns 0 on success, -1 otherwise. `remote` may be NULL
 * to use the current upstream.
 */
bool git_has_upstream(git_ctx *g);
/* Remote names (heap: each entry and the array; caller frees). */
char **git_remote_names(git_ctx *g, int *count);

int gitnet_push(git_ctx *g, const char *remote, char *msg, size_t msglen);
int gitnet_pull(git_ctx *g, const char *remote, char *msg, size_t msglen);
int gitnet_fetch(git_ctx *g, const char *remote, char *msg, size_t msglen);

#endif /* FUSSY_GIT_H */
