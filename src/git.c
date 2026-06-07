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
	git_reference *head = NULL;
	if (git_repository_head(&head, g->repo) != 0)
		return;
	git_reference *up = NULL;
	if (git_branch_upstream(&up, head) != 0) {
		git_reference_free(head);
		return;
	}

	/* Diff from the merge-base to upstream, NOT from HEAD: that captures
	 * only what upstream changed since we diverged (what a pull would
	 * bring). A HEAD->upstream diff would also flag our own local-only
	 * files as "incoming" deletions, which is wrong. */
	const git_oid *hoid = git_reference_target(head);
	const git_oid *uoid = git_reference_target(up);
	git_oid base_oid;
	bool have_base = hoid != NULL && uoid != NULL &&
	                 git_merge_base(&base_oid, g->repo, hoid, uoid) == 0;
	git_tree *ut = peel_tree(up);
	git_reference_free(head);
	git_reference_free(up);

	git_tree *base_tree = NULL;
	if (have_base) {
		git_commit *bc = NULL;
		if (git_commit_lookup(&bc, g->repo, &base_oid) == 0) {
			git_commit_tree(&base_tree, bc);
			git_commit_free(bc);
		}
	}
	if (ut == NULL || base_tree == NULL) {
		git_tree_free(ut);
		git_tree_free(base_tree);
		return;
	}

	git_diff *diff = NULL;
	if (git_diff_tree_to_tree(&diff, g->repo, base_tree, ut, NULL) == 0) {
		size_t nd = git_diff_num_deltas(diff);
		for (size_t i = 0; i < nd; i++) {
			const git_diff_delta *d = git_diff_get_delta(diff, i);
			if (d != NULL && d->new_file.path != NULL)
				tree_add(t, d->new_file.path, ST_INCOMING);
		}
		git_diff_free(diff);
	}
	git_tree_free(ut);
	git_tree_free(base_tree);
}

/* ---- history (revwalk) --------------------------------------------------- */

/* Drain a prepared revwalk (caller pushed its start point(s) + set sorting)
 * into a git_log_list. Frees the walk. */
static git_log_list log_collect(git_ctx *g, git_revwalk *w, int max)
{
	git_log_list out = {0};
	int cap = 0;
	git_oid oid;
	while (git_revwalk_next(&oid, w) == 0) {
		if (max > 0 && out.count >= max)
			break;

		git_commit *c = NULL;
		if (git_commit_lookup(&c, g->repo, &oid) != 0)
			continue;
		const char *summary = git_commit_summary(c);
		if (summary == NULL)
			summary = "";

		char abbrev[8]; /* 7 hex + NUL */
		git_oid_tostr(abbrev, sizeof(abbrev), &oid);
		char full[GIT_OID_HEXSZ + 1];
		git_oid_tostr(full, sizeof(full), &oid);

		if (out.count == cap) {
			cap = cap ? cap * 2 : 256;
			out.lines = xrealloc(out.lines,
			                     (size_t)cap * sizeof(*out.lines));
			out.shas =
			    xrealloc(out.shas, (size_t)cap * sizeof(*out.shas));
		}
		size_t n = strlen(abbrev) + 2 + strlen(summary) + 1;
		char *line = xmalloc(n);
		snprintf(line, n, "%s  %s", abbrev, summary);
		out.lines[out.count] = line;
		out.shas[out.count] = xstrdup(full);
		out.count++;

		git_commit_free(c);
	}

	git_revwalk_free(w);
	return out;
}

/* Topological first so a commit always precedes its parents (stable even when
 * commits share a timestamp), then by time to order across branches. */
#define LOG_SORT (GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME)

git_log_list git_log(git_ctx *g, int max)
{
	git_revwalk *w = NULL;
	if (git_revwalk_new(&w, g->repo) != 0)
		return (git_log_list){0};
	git_revwalk_sorting(w, LOG_SORT);
	if (git_revwalk_push_head(w) != 0) { /* unborn / empty repo */
		git_revwalk_free(w);
		return (git_log_list){0};
	}
	return log_collect(g, w, max);
}

git_log_list git_log_all(git_ctx *g, int max)
{
	git_revwalk *w = NULL;
	if (git_revwalk_new(&w, g->repo) != 0)
		return (git_log_list){0};
	git_revwalk_sorting(w, LOG_SORT);
	/* Every local branch, so cherry-pick can reach commits off HEAD. */
	if (git_revwalk_push_glob(w, "refs/heads/*") != 0) {
		git_revwalk_free(w);
		return (git_log_list){0};
	}
	return log_collect(g, w, max);
}

void git_log_free(git_log_list *l)
{
	for (int i = 0; i < l->count; i++) {
		free(l->lines[i]);
		free(l->shas[i]);
	}
	free(l->lines);
	free(l->shas);
	l->lines = NULL;
	l->shas = NULL;
	l->count = 0;
}

git_log_list git_reflog_list(git_ctx *g, int max)
{
	git_log_list out = {0};

	git_reflog *rl = NULL;
	if (git_reflog_read(&rl, g->repo, "HEAD") != 0)
		return out;

	size_t total = git_reflog_entrycount(rl);
	int cap = 0;
	for (size_t i = 0; i < total; i++) {
		if (max > 0 && out.count >= max)
			break;
		const git_reflog_entry *e = git_reflog_entry_byindex(rl, i);
		if (e == NULL)
			continue;
		const git_oid *oid = git_reflog_entry_id_new(e);
		const char *msg = git_reflog_entry_message(e);
		if (msg == NULL)
			msg = "";

		char abbrev[8];
		git_oid_tostr(abbrev, sizeof(abbrev), oid);
		char full[GIT_OID_HEXSZ + 1];
		git_oid_tostr(full, sizeof(full), oid);

		if (out.count == cap) {
			cap = cap ? cap * 2 : 256;
			out.lines = xrealloc(out.lines,
			                     (size_t)cap * sizeof(*out.lines));
			out.shas =
			    xrealloc(out.shas, (size_t)cap * sizeof(*out.shas));
		}
		int len =
		    snprintf(NULL, 0, "%s HEAD@{%zu}: %s", abbrev, i, msg);
		char *line = xmalloc((size_t)len + 1);
		snprintf(line, (size_t)len + 1, "%s HEAD@{%zu}: %s", abbrev, i,
		         msg);
		out.lines[out.count] = line;
		out.shas[out.count] = xstrdup(full);
		out.count++;
	}

	git_reflog_free(rl);
	return out;
}

/* ---- branches ------------------------------------------------------------ */

git_branchlist git_branches(git_ctx *g)
{
	git_branchlist out = {0};

	git_branch_iterator *it = NULL;
	if (git_branch_iterator_new(&it, g->repo, GIT_BRANCH_LOCAL) != 0)
		return out;

	int cap = 0;
	git_reference *ref = NULL;
	git_branch_t type;
	while (git_branch_next(&ref, &type, it) == 0) {
		const char *name = NULL;
		if (git_branch_name(&name, ref) == 0 && name != NULL) {
			bool head = git_branch_is_head(ref) == 1;
			if (out.count == cap) {
				cap = cap ? cap * 2 : 16;
				out.display = xrealloc(
				    out.display,
				    (size_t)cap * sizeof(*out.display));
				out.names =
				    xrealloc(out.names,
				             (size_t)cap * sizeof(*out.names));
			}
			out.names[out.count] = xstrdup(name);
			size_t n = strlen(name) + 3;
			char *disp = xmalloc(n);
			snprintf(disp, n, "%s %s", head ? "*" : " ", name);
			out.display[out.count] = disp;
			out.count++;
		}
		git_reference_free(ref);
	}
	git_branch_iterator_free(it);
	return out;
}

void git_branchlist_free(git_branchlist *b)
{
	for (int i = 0; i < b->count; i++) {
		free(b->display[i]);
		free(b->names[i]);
	}
	free(b->display);
	free(b->names);
	b->display = NULL;
	b->names = NULL;
	b->count = 0;
}

void git_reload_head(git_ctx *g)
{
	free(g->branch);
	git_reference *head = NULL;
	if (git_repository_head(&head, g->repo) == 0) {
		const char *sh = git_reference_shorthand(head);
		g->branch = xstrdup(sh ? sh : "HEAD");
		git_reference_free(head);
	} else {
		g->branch = xstrdup("HEAD");
	}
}

void git_ahead_behind(git_ctx *g, int *ahead, int *behind)
{
	*ahead = 0;
	*behind = 0;
	git_reference *head = NULL;
	if (git_repository_head(&head, g->repo) != 0) /* resolved branch ref */
		return;
	git_reference *up = NULL;
	if (git_branch_upstream(&up, head) == 0) {
		const git_oid *l = git_reference_target(head);
		const git_oid *u = git_reference_target(up);
		if (l != NULL && u != NULL) {
			size_t a = 0, b = 0;
			if (git_graph_ahead_behind(&a, &b, g->repo, l, u) ==
			    0) {
				*ahead = (int)a;
				*behind = (int)b;
			}
		}
		git_reference_free(up);
	}
	git_reference_free(head);
}

int gitop_checkout(git_ctx *g, const char *branch, char *err, size_t errlen)
{
	git_object *target = NULL;
	if (git_revparse_single(&target, g->repo, branch) != 0) {
		copy_err(err, errlen, "no such branch");
		return -1;
	}

	git_checkout_options opts;
	git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
	opts.checkout_strategy =
	    GIT_CHECKOUT_SAFE; /* refuse to clobber edits */
	int rc = git_checkout_tree(g->repo, target, &opts);
	git_object_free(target);
	if (rc != 0) {
		copy_err(err, errlen,
		         "checkout blocked (uncommitted changes?)");
		return -1;
	}

	char refname[256];
	snprintf(refname, sizeof(refname), "refs/heads/%s", branch);
	if (git_repository_set_head(g->repo, refname) != 0) {
		copy_err(err, errlen, "could not move HEAD");
		return -1;
	}
	return 0;
}

int gitop_branch_create(git_ctx *g, const char *name, char *err, size_t errlen)
{
	git_reference *head = NULL;
	if (git_repository_head(&head, g->repo) != 0) {
		copy_err(err, errlen, "no HEAD to branch from");
		return -1;
	}
	git_commit *target = NULL;
	int rc =
	    git_reference_peel((git_object **)&target, head, GIT_OBJECT_COMMIT);
	git_reference_free(head);
	if (rc != 0) {
		copy_err(err, errlen, "no commit at HEAD");
		return -1;
	}

	git_reference *newref = NULL;
	rc = git_branch_create(&newref, g->repo, name, target, 0);
	git_commit_free(target);
	if (rc != 0) {
		copy_err(err, errlen, "branch create failed (already exists?)");
		return -1;
	}
	git_reference_free(newref);
	return 0;
}

int gitop_branch_delete(git_ctx *g, const char *name, char *err, size_t errlen)
{
	git_reference *ref = NULL;
	if (git_branch_lookup(&ref, g->repo, name, GIT_BRANCH_LOCAL) != 0) {
		copy_err(err, errlen, "no such branch");
		return -1;
	}
	if (git_branch_is_head(ref) == 1) {
		git_reference_free(ref);
		copy_err(err, errlen, "can't delete the current branch");
		return -1;
	}

	/* Refuse unless the branch is fully merged into HEAD (like git -d). */
	bool merged = false;
	const git_oid *btip = git_reference_target(ref);
	git_reference *head = NULL;
	if (git_repository_head(&head, g->repo) == 0) {
		const git_oid *h = git_reference_target(head);
		if (h != NULL && btip != NULL)
			merged = git_oid_equal(h, btip) ||
			         git_graph_descendant_of(g->repo, h, btip) == 1;
		git_reference_free(head);
	}
	if (!merged) {
		git_reference_free(ref);
		copy_err(err, errlen, "not fully merged (refusing delete)");
		return -1;
	}

	int rc = git_branch_delete(ref);
	git_reference_free(ref);
	if (rc != 0) {
		copy_err(err, errlen, "branch delete failed");
		return -1;
	}
	return 0;
}

/* ---- stashes ------------------------------------------------------------- */

struct stash_acc {
	git_stashlist *out;
	int cap;
};

static int stash_cb(size_t index, const char *message, const git_oid *stash_id,
                    void *payload)
{
	(void)stash_id;
	struct stash_acc *a = payload;
	const char *msg = message ? message : "";
	if (a->out->count == a->cap) {
		a->cap = a->cap ? a->cap * 2 : 16;
		a->out->display = xrealloc(
		    a->out->display, (size_t)a->cap * sizeof(*a->out->display));
	}
	int len = snprintf(NULL, 0, "stash@{%zu}: %s", index, msg);
	char *line = xmalloc((size_t)len + 1);
	snprintf(line, (size_t)len + 1, "stash@{%zu}: %s", index, msg);
	a->out->display[a->out->count++] = line;
	return 0;
}

git_stashlist git_stashes(git_ctx *g)
{
	git_stashlist out = {0};
	struct stash_acc acc = {&out, 0};
	git_stash_foreach(g->repo, stash_cb, &acc);
	return out;
}

void git_stashlist_free(git_stashlist *s)
{
	for (int i = 0; i < s->count; i++)
		free(s->display[i]);
	free(s->display);
	s->display = NULL;
	s->count = 0;
}

int gitop_stash_push(git_ctx *g, const char *message, char *err, size_t errlen)
{
	git_signature *sig = NULL;
	if (git_signature_default(&sig, g->repo) != 0) {
		copy_err(err, errlen, "set user.name and user.email");
		return -1;
	}
	git_oid oid;
	int rc = git_stash_save(&oid, g->repo, sig,
	                        (message && message[0]) ? message : NULL,
	                        GIT_STASH_DEFAULT);
	git_signature_free(sig);
	if (rc == GIT_ENOTFOUND) {
		copy_err(err, errlen, "nothing to stash");
		return -1;
	}
	if (rc != 0) {
		copy_err(err, errlen, "stash failed");
		return -1;
	}
	return 0;
}

int gitop_stash_apply(git_ctx *g, size_t index, char *err, size_t errlen)
{
	int rc = git_stash_apply(g->repo, index, NULL);
	if (rc != 0) {
		copy_err(err, errlen,
		         rc == GIT_EMERGECONFLICT
		             ? "stash conflicts with the working tree"
		             : "apply failed");
		return -1;
	}
	return 0;
}

int gitop_stash_pop(git_ctx *g, size_t index, char *err, size_t errlen)
{
	int rc = git_stash_pop(g->repo, index, NULL);
	if (rc != 0) {
		copy_err(err, errlen,
		         rc == GIT_EMERGECONFLICT
		             ? "stash conflicts; not dropped"
		             : "pop failed");
		return -1;
	}
	return 0;
}

int gitop_stash_drop(git_ctx *g, size_t index, char *err, size_t errlen)
{
	int rc = git_stash_drop(g->repo, index);
	if (rc != 0) {
		copy_err(err, errlen, "drop failed");
		return -1;
	}
	return 0;
}

int gitop_reset(git_ctx *g, const char *rev, int mode, char *err, size_t errlen)
{
	git_object *target = NULL;
	if (git_revparse_single(&target, g->repo, rev) != 0) {
		copy_err(err, errlen, "no such commit");
		return -1;
	}
	git_reset_t t = mode == RESET_SOFT   ? GIT_RESET_SOFT
	                : mode == RESET_HARD ? GIT_RESET_HARD
	                                     : GIT_RESET_MIXED;
	int rc = git_reset(g->repo, target, t, NULL);
	git_object_free(target);
	if (rc != 0) {
		copy_err(err, errlen, "reset failed");
		return -1;
	}
	return 0;
}

/* ---- cherry-pick / revert ------------------------------------------------ */

/* Commit the current index on top of HEAD. Returns 0 on success, 1 if the index
 * has conflicts (caller aborts), -1 on a hard error. */
static int commit_from_index(git_repository *repo, const git_signature *author,
                             const git_signature *committer,
                             const char *message, char *err, size_t errlen)
{
	git_index *idx = NULL;
	if (git_repository_index(&idx, repo) != 0) {
		copy_err(err, errlen, "open index failed");
		return -1;
	}
	if (git_index_has_conflicts(idx) != 0) {
		git_index_free(idx);
		return 1;
	}
	git_oid tree_oid;
	int rc = git_index_write_tree(&tree_oid, idx);
	git_index_free(idx);
	if (rc != 0) {
		copy_err(err, errlen, "write tree failed");
		return -1;
	}

	git_tree *tree = NULL;
	git_reference *head = NULL;
	git_commit *parent = NULL;
	if (git_tree_lookup(&tree, repo, &tree_oid) != 0 ||
	    git_repository_head(&head, repo) != 0 ||
	    git_reference_peel((git_object **)&parent, head,
	                       GIT_OBJECT_COMMIT) != 0) {
		git_tree_free(tree);
		git_reference_free(head);
		copy_err(err, errlen, "resolve HEAD failed");
		return -1;
	}
	git_reference_free(head);

	git_oid commit_oid;
	const git_commit *parents[1] = {parent};
	rc = git_commit_create(&commit_oid, repo, "HEAD", author, committer,
	                       NULL, message, tree, 1, parents);
	git_tree_free(tree);
	git_commit_free(parent);
	if (rc != 0) {
		copy_err(err, errlen, "commit failed");
		return -1;
	}
	return 0;
}

/* Reset the worktree/index back to HEAD and clear any in-progress op state. */
static void abort_to_head(git_repository *repo)
{
	git_object *head = NULL;
	if (git_revparse_single(&head, repo, "HEAD") == 0) {
		git_reset(repo, head, GIT_RESET_HARD, NULL);
		git_object_free(head);
	}
	git_repository_state_cleanup(repo);
}

static int resolve_commit(git_repository *repo, const char *rev,
                          git_commit **out)
{
	git_object *obj = NULL;
	if (git_revparse_single(&obj, repo, rev) != 0)
		return -1;
	int rc = git_object_peel((git_object **)out, obj, GIT_OBJECT_COMMIT);
	git_object_free(obj);
	return rc;
}

int gitop_cherrypick(git_ctx *g, const char *rev, char *err, size_t errlen)
{
	git_commit *pick = NULL;
	if (resolve_commit(g->repo, rev, &pick) != 0) {
		copy_err(err, errlen, "no such commit");
		return -1;
	}
	if (git_cherrypick(g->repo, pick, NULL) != 0) {
		git_commit_free(pick);
		copy_err(err, errlen, "cherry-pick failed");
		return -1;
	}

	git_signature *committer = NULL;
	if (git_signature_default(&committer, g->repo) != 0) {
		git_commit_free(pick);
		abort_to_head(g->repo);
		copy_err(err, errlen, "set user.name and user.email");
		return -1;
	}
	int rc = commit_from_index(g->repo, git_commit_author(pick), committer,
	                           git_commit_message(pick), err, errlen);
	git_signature_free(committer);
	git_commit_free(pick);

	if (rc == 1) {
		abort_to_head(g->repo);
		copy_err(err, errlen, "cherry-pick conflicts (aborted)");
		return -1;
	}
	git_repository_state_cleanup(g->repo);
	return rc;
}

int gitop_cherrypick_nocommit(git_ctx *g, const char *rev, char *err,
                              size_t errlen)
{
	git_commit *pick = NULL;
	if (resolve_commit(g->repo, rev, &pick) != 0) {
		copy_err(err, errlen, "no such commit");
		return -1;
	}
	int rc = git_cherrypick(g->repo, pick, NULL);
	git_commit_free(pick);
	if (rc != 0) {
		copy_err(err, errlen, "cherry-pick failed");
		return -1;
	}

	git_index *idx = NULL;
	bool conflicts = false;
	if (git_repository_index(&idx, g->repo) == 0) {
		conflicts = git_index_has_conflicts(idx) != 0;
		git_index_free(idx);
	}
	if (conflicts) {
		abort_to_head(g->repo);
		copy_err(err, errlen, "cherry-pick conflicts (aborted)");
		return -1;
	}
	/* Clear CHERRY_PICK_HEAD so a later commit is a normal one, but keep
	 * the applied changes staged (state cleanup does not touch the index).
	 */
	git_repository_state_cleanup(g->repo);
	return 0;
}

int gitop_revert(git_ctx *g, const char *rev, char *err, size_t errlen)
{
	git_commit *target = NULL;
	if (resolve_commit(g->repo, rev, &target) != 0) {
		copy_err(err, errlen, "no such commit");
		return -1;
	}
	if (git_revert(g->repo, target, NULL) != 0) {
		git_commit_free(target);
		copy_err(err, errlen, "revert failed");
		return -1;
	}

	git_signature *sig = NULL;
	if (git_signature_default(&sig, g->repo) != 0) {
		git_commit_free(target);
		abort_to_head(g->repo);
		copy_err(err, errlen, "set user.name and user.email");
		return -1;
	}
	char full[GIT_OID_HEXSZ + 1];
	git_oid_tostr(full, sizeof(full), git_commit_id(target));
	const char *summary = git_commit_summary(target);
	int len =
	    snprintf(NULL, 0, "Revert \"%s\"\n\nThis reverts commit %s.\n",
	             summary ? summary : "", full);
	char *msg = xmalloc((size_t)len + 1);
	snprintf(msg, (size_t)len + 1,
	         "Revert \"%s\"\n\nThis reverts commit %s.\n",
	         summary ? summary : "", full);

	int rc = commit_from_index(g->repo, sig, sig, msg, err, errlen);
	free(msg);
	git_signature_free(sig);
	git_commit_free(target);

	if (rc == 1) {
		abort_to_head(g->repo);
		copy_err(err, errlen, "revert conflicts (aborted)");
		return -1;
	}
	git_repository_state_cleanup(g->repo);
	return rc;
}

/* ---- merge ----------------------------------------------------------------
 */

int gitop_merge(git_ctx *g, const char *branch, char *err, size_t errlen)
{
	git_repository *repo = g->repo;

	git_reference *ref = NULL;
	if (git_branch_lookup(&ref, repo, branch, GIT_BRANCH_LOCAL) != 0) {
		copy_err(err, errlen, "no such branch");
		return -1;
	}
	git_annotated_commit *their = NULL;
	int rc = git_annotated_commit_from_ref(&their, repo, ref);
	git_reference_free(ref);
	if (rc != 0) {
		copy_err(err, errlen, "merge setup failed");
		return -1;
	}
	git_oid their_oid = *git_annotated_commit_id(their);

	git_merge_analysis_t analysis;
	git_merge_preference_t pref;
	const git_annotated_commit *heads[1] = {their};
	if (git_merge_analysis(&analysis, &pref, repo, heads, 1) != 0) {
		git_annotated_commit_free(their);
		copy_err(err, errlen, "merge analysis failed");
		return -1;
	}

	if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
		git_annotated_commit_free(their);
		copy_err(err, errlen, "already up to date");
		return -1;
	}

	git_checkout_options co;
	git_checkout_options_init(&co, GIT_CHECKOUT_OPTIONS_VERSION);
	co.checkout_strategy = GIT_CHECKOUT_SAFE;

	if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) {
		git_object *target = NULL;
		rc = git_object_lookup(&target, repo, &their_oid,
		                       GIT_OBJECT_COMMIT);
		if (rc == 0)
			rc = git_checkout_tree(repo, target, &co);
		if (rc == 0) {
			git_reference *head = NULL;
			if (git_repository_head(&head, repo) == 0) {
				git_reference *newref = NULL;
				rc = git_reference_set_target(
				    &newref, head, &their_oid,
				    "merge: fast-forward");
				git_reference_free(newref);
				git_reference_free(head);
			}
		}
		git_object_free(target);
		git_annotated_commit_free(their);
		if (rc != 0) {
			copy_err(err, errlen, "fast-forward failed");
			return -1;
		}
		return 0;
	}

	/* Normal merge: apply, then commit with two parents (or abort). */
	git_merge_options mo;
	git_merge_options_init(&mo, GIT_MERGE_OPTIONS_VERSION);
	rc = git_merge(repo, heads, 1, &mo, &co);
	git_annotated_commit_free(their);
	if (rc != 0) {
		abort_to_head(repo);
		copy_err(err, errlen, "merge failed");
		return -1;
	}

	git_index *idx = NULL;
	if (git_repository_index(&idx, repo) != 0) {
		abort_to_head(repo);
		copy_err(err, errlen, "open index failed");
		return -1;
	}
	if (git_index_has_conflicts(idx) != 0) {
		git_index_free(idx);
		abort_to_head(repo);
		copy_err(err, errlen, "merge conflicts (aborted)");
		return -1;
	}
	git_oid tree_oid;
	rc = git_index_write_tree(&tree_oid, idx);
	git_index_free(idx);

	git_tree *tree = NULL;
	git_reference *head = NULL;
	git_commit *p1 = NULL, *p2 = NULL;
	git_signature *sig = NULL;
	if (rc == 0 && git_tree_lookup(&tree, repo, &tree_oid) == 0 &&
	    git_repository_head(&head, repo) == 0 &&
	    git_reference_peel((git_object **)&p1, head, GIT_OBJECT_COMMIT) ==
	        0 &&
	    git_commit_lookup(&p2, repo, &their_oid) == 0 &&
	    git_signature_default(&sig, repo) == 0) {
		char msg[160];
		snprintf(msg, sizeof(msg), "Merge branch '%s'", branch);
		const git_commit *parents[2] = {p1, p2};
		git_oid commit_oid;
		rc = git_commit_create(&commit_oid, repo, "HEAD", sig, sig,
		                       NULL, msg, tree, 2, parents);
	} else {
		rc = -1;
	}
	git_reference_free(head);
	git_tree_free(tree);
	git_commit_free(p1);
	git_commit_free(p2);
	git_signature_free(sig);

	if (rc != 0) {
		abort_to_head(repo);
		copy_err(err, errlen, "merge commit failed");
		return -1;
	}
	git_repository_state_cleanup(repo);
	return 0;
}
