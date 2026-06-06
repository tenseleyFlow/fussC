#include "git.h"

#include <git2.h>
#include <stdlib.h>
#include <string.h>

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
	opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
	             GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

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
