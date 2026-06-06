#include "git.h"

#include <errno.h>
#include <git2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "proc.h"
#include "util.h"

/* libgit2 is reference-counted; init on first open, shutdown on last close. */
static void copy_err(char *errbuf, size_t errlen, const char *fallback)
{
	const git_error *e = git_error_last();
	const char *msg = (e && e->message) ? e->message : fallback;
	if (errbuf && errlen) {
		strncpy(errbuf, msg, errlen - 1);
		errbuf[errlen - 1] = '\0';
	}
}

static char *basename_dup(const char *path)
{
	size_t len = strlen(path);
	/* Drop trailing slashes (workdir paths end in '/'). */
	while (len > 1 && path[len - 1] == '/')
		len--;
	size_t start = len;
	while (start > 0 && path[start - 1] != '/')
		start--;
	size_t n = len - start;
	char *out = xmalloc(n + 1);
	memcpy(out, path + start, n);
	out[n] = '\0';
	return out;
}

bool git_open(git_ctx *g, char *errbuf, size_t errlen)
{
	g->repo = NULL;
	g->repo_name = NULL;
	g->branch = NULL;

	git_libgit2_init();

	git_buf root = {0};
	if (git_repository_discover(&root, ".", 0, NULL) != 0) {
		copy_err(errbuf, errlen, "not a git repository");
		git_libgit2_shutdown();
		return false;
	}

	git_repository *repo = NULL;
	int rc = git_repository_open(&repo, root.ptr);
	git_buf_dispose(&root);
	if (rc != 0) {
		copy_err(errbuf, errlen, "failed to open repository");
		git_libgit2_shutdown();
		return false;
	}
	g->repo = repo;

	const char *wd = git_repository_workdir(repo);
	g->repo_name = basename_dup(wd ? wd : ".");

	/* Branch short name; bare/detached/unborn fall back to "HEAD". */
	git_reference *head = NULL;
	if (git_repository_head(&head, repo) == 0) {
		const char *sh = git_reference_shorthand(head);
		g->branch = xstrdup(sh ? sh : "HEAD");
		git_reference_free(head);
	} else {
		g->branch = xstrdup("HEAD");
	}

	return true;
}

void git_close(git_ctx *g)
{
	if (g->repo)
		git_repository_free(g->repo);
	free(g->repo_name);
	free(g->branch);
	g->repo = NULL;
	g->repo_name = NULL;
	g->branch = NULL;
	git_libgit2_shutdown();
}

/* Map a libgit2 status word to our flag set. */
static file_status map_status(unsigned int s)
{
	file_status fs = 0;
	if (s & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED |
	         GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_RENAMED |
	         GIT_STATUS_INDEX_TYPECHANGE))
		fs |= ST_STAGED;
	if (s & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED |
	         GIT_STATUS_WT_TYPECHANGE | GIT_STATUS_WT_RENAMED))
		fs |= ST_UNSTAGED;
	if (s & GIT_STATUS_WT_NEW)
		fs |= ST_UNTRACKED;
	if (s & GIT_STATUS_IGNORED)
		fs |= ST_GITIGNORED;
	return fs;
}

/* The repo-relative path a status entry refers to, preferring the index side.
 */
static const char *entry_path(const git_status_entry *e)
{
	if (e->head_to_index && e->head_to_index->new_file.path)
		return e->head_to_index->new_file.path;
	if (e->index_to_workdir && e->index_to_workdir->new_file.path)
		return e->index_to_workdir->new_file.path;
	return NULL;
}

int git_load_tree(git_ctx *g, tree *t, bool all)
{
	git_repository *repo = g->repo;

	git_status_options opts;
	git_status_options_init(&opts, GIT_STATUS_OPTIONS_VERSION);
	opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	/* Include ignored paths so they render dimmed, but do NOT recurse into
	 * ignored dirs: one node per top-level ignored path (e.g. "build/"),
	 * not a flood of every artifact under it. */
	opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
	             GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
	             GIT_STATUS_OPT_INCLUDE_IGNORED;

	git_status_list *list = NULL;
	if (git_status_list_new(&list, repo, &opts) != 0)
		return -1;

	size_t n = git_status_list_entrycount(list);
	for (size_t i = 0; i < n; i++) {
		const git_status_entry *e = git_status_byindex(list, i);
		const char *path = entry_path(e);
		if (path)
			tree_add(t, path, map_status(e->status));
	}
	git_status_list_free(list);

	if (all) {
		/* Add every tracked file; clean ones carry no status bits.
		 * tree_add merges, so dirty entries keep their flags. */
		git_index *idx = NULL;
		if (git_repository_index(&idx, repo) != 0)
			return -1;
		size_t count = git_index_entrycount(idx);
		for (size_t i = 0; i < count; i++) {
			const git_index_entry *e =
			    git_index_get_byindex(idx, i);
			if (e && e->path)
				tree_add(t, e->path, 0);
		}
		git_index_free(idx);
	}

	return 0;
}

/* ---- local mutations ----------------------------------------------------- */

static void errno_msg(char *err, size_t errlen, const char *what)
{
	if (err && errlen)
		snprintf(err, errlen, "%s: %s", what, strerror(errno));
}

/* Stage the given pathspec (file, dir, or - with an empty spec - everything),
 * matching `git add --all` semantics (adds, modifies, and stages deletions). */
static int stage_spec(git_repository *repo, const char *path, char *err,
                      size_t errlen)
{
	git_index *idx = NULL;
	if (git_repository_index(&idx, repo) != 0) {
		copy_err(err, errlen, "open index failed");
		return -1;
	}
	char *one[1] = {(char *)path};
	git_strarray ps = {path ? one : NULL, path ? 1 : 0};
	int rc = git_index_add_all(idx, &ps, GIT_INDEX_ADD_DEFAULT, NULL, NULL);
	if (rc == 0)
		rc = git_index_write(idx);
	git_index_free(idx);
	if (rc != 0) {
		copy_err(err, errlen, "stage failed");
		return -1;
	}
	return 0;
}

int gitop_stage(git_ctx *g, const char *path, char *err, size_t errlen)
{
	return stage_spec(g->repo, path, err, errlen);
}

int gitop_stage_all(git_ctx *g, char *err, size_t errlen)
{
	return stage_spec(g->repo, NULL, err, errlen);
}

/* Reset index entries to HEAD (unstage). Without a HEAD (unborn branch),
 * "unstaging" means removing the entry/entries from the index. path == NULL
 * means all. */
static int unstage_spec(git_repository *repo, const char *path, char *err,
                        size_t errlen)
{
	git_object *head = NULL;
	if (git_revparse_single(&head, repo, "HEAD") != 0) {
		git_index *idx = NULL;
		if (git_repository_index(&idx, repo) != 0) {
			copy_err(err, errlen, "open index failed");
			return -1;
		}
		int rc = path ? git_index_remove_bypath(idx, path)
		              : git_index_clear(idx);
		if (rc == 0)
			rc = git_index_write(idx);
		git_index_free(idx);
		if (rc != 0) {
			copy_err(err, errlen, "unstage failed");
			return -1;
		}
		return 0;
	}

	int rc;
	if (path) {
		char *one[1] = {(char *)path};
		git_strarray ps = {one, 1};
		rc = git_reset_default(repo, head, &ps);
	} else {
		/* Reset the whole index to HEAD (HEAD does not move, worktree
		 * untouched) - a literal "." pathspec would match nothing. */
		rc = git_reset(repo, head, GIT_RESET_MIXED, NULL);
	}
	git_object_free(head);
	if (rc != 0) {
		copy_err(err, errlen, "unstage failed");
		return -1;
	}
	return 0;
}

int gitop_unstage(git_ctx *g, const char *path, char *err, size_t errlen)
{
	return unstage_spec(g->repo, path, err, errlen);
}

int gitop_unstage_all(git_ctx *g, char *err, size_t errlen)
{
	return unstage_spec(g->repo, NULL, err, errlen);
}

/* Build a tree object from the current index. */
static int index_tree(git_repository *repo, git_tree **out, char *err,
                      size_t errlen)
{
	git_index *idx = NULL;
	git_oid oid;
	if (git_repository_index(&idx, repo) != 0) {
		copy_err(err, errlen, "open index failed");
		return -1;
	}
	int rc = git_index_write_tree(&oid, idx);
	git_index_free(idx);
	if (rc != 0) {
		copy_err(err, errlen, "write tree failed");
		return -1;
	}
	if (git_tree_lookup(out, repo, &oid) != 0) {
		copy_err(err, errlen, "tree lookup failed");
		return -1;
	}
	return 0;
}

int gitop_commit(git_ctx *g, const char *message, char *err, size_t errlen)
{
	git_repository *repo = g->repo;
	git_tree *tree = NULL;
	git_signature *sig = NULL;
	git_commit *parent = NULL;
	int rc = -1;

	if (index_tree(repo, &tree, err, errlen) != 0)
		goto done;
	if (git_signature_default(&sig, repo) != 0) {
		copy_err(err, errlen, "set user.name and user.email");
		goto done;
	}

	git_oid parent_oid;
	int has_head = git_reference_name_to_id(&parent_oid, repo, "HEAD") == 0;
	if (has_head && git_commit_lookup(&parent, repo, &parent_oid) != 0) {
		copy_err(err, errlen, "HEAD lookup failed");
		goto done;
	}

	git_oid commit_oid;
	const git_commit *parents[1] = {parent};
	rc = git_commit_create(&commit_oid, repo, "HEAD", sig, sig, NULL,
	                       message, tree, has_head ? 1 : 0,
	                       has_head ? parents : NULL);
	if (rc != 0) {
		copy_err(err, errlen, "commit failed");
		rc = -1;
	}

done:
	if (parent)
		git_commit_free(parent);
	if (sig)
		git_signature_free(sig);
	if (tree)
		git_tree_free(tree);
	return rc;
}

int gitop_amend(git_ctx *g, const char *message, char *err, size_t errlen)
{
	git_repository *repo = g->repo;
	git_commit *head = NULL;
	git_tree *tree = NULL;
	git_signature *sig = NULL;
	int rc = -1;

	git_oid head_oid;
	if (git_reference_name_to_id(&head_oid, repo, "HEAD") != 0 ||
	    git_commit_lookup(&head, repo, &head_oid) != 0) {
		copy_err(err, errlen, "no commit to amend");
		goto done;
	}
	if (index_tree(repo, &tree, err, errlen) != 0)
		goto done;
	if (git_signature_default(&sig, repo) != 0) {
		copy_err(err, errlen, "set user.name and user.email");
		goto done;
	}

	git_oid newoid;
	rc = git_commit_amend(&newoid, head, "HEAD", NULL, sig, NULL, message,
	                      tree);
	if (rc != 0) {
		copy_err(err, errlen, "amend failed");
		rc = -1;
	}

done:
	if (head)
		git_commit_free(head);
	if (tree)
		git_tree_free(tree);
	if (sig)
		git_signature_free(sig);
	return rc;
}

char *gitop_last_message(git_ctx *g)
{
	git_oid oid;
	git_commit *c = NULL;
	char *out = NULL;
	if (git_reference_name_to_id(&oid, g->repo, "HEAD") == 0 &&
	    git_commit_lookup(&c, g->repo, &oid) == 0) {
		const char *m = git_commit_message(c);
		if (m)
			out = xstrdup(m);
		git_commit_free(c);
	}
	return out;
}

int gitop_discard(git_ctx *g, const char *path, bool untracked, char *err,
                  size_t errlen)
{
	if (untracked) {
		if (unlink(path) != 0 && errno != ENOENT) {
			errno_msg(err, errlen, "discard");
			return -1;
		}
		return 0;
	}

	/* Tracked: reset the index entry to HEAD, then force-checkout HEAD. */
	if (unstage_spec(g->repo, path, err, errlen) != 0)
		return -1;

	git_checkout_options opts;
	git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
	opts.checkout_strategy = GIT_CHECKOUT_FORCE;
	char *one[1] = {(char *)path};
	opts.paths.strings = one;
	opts.paths.count = 1;
	if (git_checkout_head(g->repo, &opts) != 0) {
		copy_err(err, errlen, "discard failed");
		return -1;
	}
	return 0;
}

int gitop_delete(git_ctx *g, const char *path, bool untracked, char *err,
                 size_t errlen)
{
	if (!untracked) {
		git_index *idx = NULL;
		if (git_repository_index(&idx, g->repo) != 0) {
			copy_err(err, errlen, "open index failed");
			return -1;
		}
		int rc = git_index_remove_bypath(idx, path);
		if (rc == 0)
			rc = git_index_write(idx);
		git_index_free(idx);
		if (rc != 0) {
			copy_err(err, errlen, "delete (index) failed");
			return -1;
		}
	}
	if (unlink(path) != 0 && errno != ENOENT) {
		errno_msg(err, errlen, "delete");
		return -1;
	}
	return 0;
}

int gitop_rename(git_ctx *g, const char *oldpath, const char *newpath,
                 char *err, size_t errlen)
{
	if (rename(oldpath, newpath) != 0) {
		errno_msg(err, errlen, "rename");
		return -1;
	}
	/* Keep the index consistent when the old path was tracked. */
	git_index *idx = NULL;
	if (git_repository_index(&idx, g->repo) == 0) {
		if (git_index_get_bypath(idx, oldpath, 0) != NULL) {
			git_index_remove_bypath(idx, oldpath);
			git_index_add_bypath(idx, newpath);
			git_index_write(idx);
		}
		git_index_free(idx);
	}
	return 0;
}

int gitop_tag(git_ctx *g, const char *name, const char *message, char *err,
              size_t errlen)
{
	git_object *target = NULL;
	if (git_revparse_single(&target, g->repo, "HEAD") != 0) {
		copy_err(err, errlen, "no commit to tag");
		return -1;
	}

	git_oid oid;
	int rc;
	if (message != NULL && message[0] != '\0') {
		git_signature *sig = NULL;
		if (git_signature_default(&sig, g->repo) != 0) {
			copy_err(err, errlen, "set user.name and user.email");
			git_object_free(target);
			return -1;
		}
		rc = git_tag_create(&oid, g->repo, name, target, sig, message,
		                    0);
		git_signature_free(sig);
	} else {
		rc = git_tag_create_lightweight(&oid, g->repo, name, target, 0);
	}
	git_object_free(target);
	if (rc != 0) {
		copy_err(err, errlen, "tag failed");
		return -1;
	}
	return 0;
}

/* ---- network operations -------------------------------------------------- */

bool git_has_upstream(git_ctx *g)
{
	git_reference *head = NULL;
	bool has = false;
	if (git_repository_head(&head, g->repo) == 0) {
		git_reference *up = NULL;
		if (git_branch_upstream(&up, head) == 0) {
			has = true;
			git_reference_free(up);
		}
		git_reference_free(head);
	}
	return has;
}

char **git_remote_names(git_ctx *g, int *count)
{
	git_strarray rs = {0};
	if (git_remote_list(&rs, g->repo) != 0 || rs.count == 0) {
		git_strarray_dispose(&rs);
		*count = 0;
		return NULL;
	}
	char **out = xmalloc(rs.count * sizeof(*out));
	for (size_t i = 0; i < rs.count; i++)
		out[i] = xstrdup(rs.strings[i]);
	*count = (int)rs.count;
	git_strarray_dispose(&rs);
	return out;
}

/* Copy the first non-empty line of `s` into msg. */
static void first_line(const char *s, char *msg, size_t msglen)
{
	while (*s == '\n' || *s == '\r')
		s++;
	size_t n = 0;
	while (s[n] != '\0' && s[n] != '\n' && s[n] != '\r')
		n++;
	if (n >= msglen)
		n = msglen - 1;
	memcpy(msg, s, n);
	msg[n] = '\0';
}

/* Turn a git failure into a short, actionable message. */
static void net_message(int rc, const char *out, const char *err,
                        const char *ok, char *msg, size_t msglen)
{
	if (rc == 0) {
		snprintf(msg, msglen, "%s", ok);
		return;
	}
	if (rc < 0) { /* proc_run could not spawn git (out/err are NULL) */
		snprintf(msg, msglen, "could not run git");
		return;
	}
	/* From here git ran and exited non-zero; out/err are valid strings, but
	 * stay defensive in case a future caller passes NULL. */
	if (err == NULL)
		err = "";
	if (out == NULL)
		out = "";
	if (strstr(err, "no upstream") || strstr(err, "has no upstream"))
		snprintf(msg, msglen, "no upstream set");
	else if (strstr(err, "Could not read from remote") ||
	         strstr(err, "Could not resolve host") ||
	         strstr(err, "Connection"))
		snprintf(msg, msglen, "cannot reach remote");
	else if (strstr(err, "rejected") || strstr(err, "non-fast-forward"))
		snprintf(msg, msglen, "rejected; pull first");
	else if (strstr(err, "Authentication") ||
	         strstr(err, "Permission denied"))
		snprintf(msg, msglen, "authentication failed");
	else if (err[0] != '\0')
		first_line(err, msg, msglen);
	else if (out[0] != '\0')
		first_line(out, msg, msglen);
	else
		snprintf(msg, msglen, "failed");
}

static int run_git(char *const argv[], const char *ok, char *msg, size_t msglen)
{
	char *out = NULL, *err = NULL;
	int rc = proc_run(argv, &out, &err);
	net_message(rc, out, err, ok, msg, msglen);
	free(out);
	free(err);
	return rc == 0 ? 0 : -1;
}

int gitnet_push(git_ctx *g, const char *remote, char *msg, size_t msglen)
{
	/* --follow-tags carries annotated tags reachable from the branch, so a
	 * tag made via T ships with the next push - no separate "push tag" key.
	 */
	if (remote != NULL) { /* first push: set the upstream */
		char *argv[] = {"git", "push",         "--follow-tags",
		                "-u",  (char *)remote, g->branch,
		                NULL};
		return run_git(argv, "pushed", msg, msglen);
	}
	char *argv[] = {"git", "push", "--follow-tags", NULL};
	return run_git(argv, "pushed", msg, msglen);
}

int gitnet_pull(git_ctx *g, const char *remote, char *msg, size_t msglen)
{
	if (remote != NULL) {
		char *argv[] = {"git", "pull", (char *)remote, g->branch, NULL};
		return run_git(argv, "pulled", msg, msglen);
	}
	char *argv[] = {"git", "pull", NULL};
	return run_git(argv, "pulled", msg, msglen);
}

int gitnet_fetch(git_ctx *g, const char *remote, char *msg, size_t msglen)
{
	(void)g;
	if (remote != NULL) {
		char *argv[] = {"git", "fetch", (char *)remote, NULL};
		return run_git(argv, "fetched", msg, msglen);
	}
	char *argv[] = {"git", "fetch", NULL};
	return run_git(argv, "fetched", msg, msglen);
}

/* Peel a ref to its tree (commit -> tree). Caller frees. */
static git_tree *peel_tree(git_reference *ref)
{
	git_object *obj = NULL;
	if (git_reference_peel(&obj, ref, GIT_OBJECT_TREE) != 0)
		return NULL;
	return (git_tree *)obj;
}

/* OR ST_INCOMING onto every path that the (already-fetched) upstream changed
 * relative to HEAD, so the tree shows a down glyph for what a pull would bring.
 * No-ops silently when there is no upstream. */
void git_mark_incoming(git_ctx *g, tree *t)
{
	git_reference *head = NULL, *up = NULL;
	if (git_repository_head(&head, g->repo) != 0)
		return;
	if (git_branch_upstream(&up, head) != 0) {
		git_reference_free(head);
		return;
	}
	git_tree *ht = peel_tree(head);
	git_tree *ut = peel_tree(up);
	git_reference_free(head);
	git_reference_free(up);
	if (ht == NULL || ut == NULL) {
		git_tree_free(ht);
		git_tree_free(ut);
		return;
	}

	git_diff *diff = NULL;
	if (git_diff_tree_to_tree(&diff, g->repo, ht, ut, NULL) == 0) {
		size_t nd = git_diff_num_deltas(diff);
		for (size_t i = 0; i < nd; i++) {
			const git_diff_delta *d = git_diff_get_delta(diff, i);
			if (d != NULL && d->new_file.path != NULL)
				tree_add(t, d->new_file.path, ST_INCOMING);
		}
		git_diff_free(diff);
	}
	git_tree_free(ht);
	git_tree_free(ut);
}
