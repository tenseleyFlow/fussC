#include "tree.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

/* Total order over names: case-insensitive first (so the tree reads like the
 * `tree` command), with a case-sensitive tiebreak so distinct names that differ
 * only in case still get a stable, deterministic order. Returns 0 only when the
 * names are byte-identical, i.e. the same node. */
static int name_cmp(const char *a, const char *b)
{
	int c = strcasecmp(a, b);
	if (c != 0)
		return c;
	return strcmp(a, b);
}

static char *ascii_lower_dup(const char *s)
{
	char *d = xstrdup(s);
	for (char *p = d; *p != '\0'; p++) {
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
	}
	return d;
}

static uint32_t tree_alloc(tree *t)
{
	if (t->len == t->cap) {
		t->cap = t->cap ? t->cap * 2 : 16;
		t->nodes = xrealloc(t->nodes, t->cap * sizeof(*t->nodes));
	}
	return t->len++;
}

/* Initialise node `idx` as a leaf of `parent` with the given name/path. The
 * caller links it into the sibling list. */
static void node_init(tree *t, uint32_t idx, uint32_t parent, const char *name,
                      const char *path, uint8_t flags, file_status status)
{
	node *n = &t->nodes[idx];
	n->name = xstrdup(name);
	n->name_lower = ascii_lower_dup(name);
	n->path = xstrdup(path);
	n->status = status;
	n->flags = flags;
	n->first_child = NODE_NIL;
	n->next_sibling = NODE_NIL;
	n->parent = parent;
}

void tree_init(tree *t)
{
	t->nodes = NULL;
	t->len = 0;
	t->cap = 0;
	uint32_t root = tree_alloc(t);
	node_init(t, root, NODE_NIL, ".", "", NF_EXPANDED, 0);
}

void tree_free(tree *t)
{
	for (uint32_t i = 0; i < t->len; i++) {
		free(t->nodes[i].name);
		free(t->nodes[i].name_lower);
		free(t->nodes[i].path);
	}
	free(t->nodes);
	t->nodes = NULL;
	t->len = t->cap = 0;
}

/*
 * Find a direct child of `parent` named `name`. If absent, create it (a file
 * when `is_file`, else an expanded directory), splicing it into the sibling
 * list at the sorted position, and assign its full path. Returns the child
 * index. `child_path` is only consulted when creating.
 */
static uint32_t child_get_or_add(tree *t, uint32_t parent, const char *name,
                                 const char *child_path, bool is_file)
{
	/* Locate the insertion point: prev is the last sibling that sorts
	 * before `name`, cur is the first that sorts at-or-after it. */
	uint32_t prev = NODE_NIL;
	uint32_t cur = t->nodes[parent].first_child;
	while (cur != NODE_NIL) {
		int c = name_cmp(name, t->nodes[cur].name);
		if (c == 0)
			return cur; /* already present */
		if (c < 0)
			break;
		prev = cur;
		cur = t->nodes[cur].next_sibling;
	}

	uint32_t idx = tree_alloc(t); /* may realloc; only use indices after */
	node_init(t, idx, parent, name, child_path,
	          is_file ? NF_FILE : NF_EXPANDED, 0);
	t->nodes[idx].next_sibling = cur;
	if (prev == NODE_NIL)
		t->nodes[parent].first_child = idx;
	else
		t->nodes[prev].next_sibling = idx;
	return idx;
}

void tree_add(tree *t, const char *path, file_status status)
{
	uint32_t cur = 0; /* root */
	const char *p = path;
	char comp[1024];

	while (*p != '\0') {
		const char *slash = strchr(p, '/');
		size_t len = slash ? (size_t)(slash - p) : strlen(p);

		if (len ==
		    0) { /* skip empty component (leading/double slash) */
			if (!slash)
				break;
			p = slash + 1;
			continue;
		}

		bool is_last = (slash == NULL) || (*(slash + 1) == '\0');

		/* Build the cumulative path for this component. */
		size_t off = (size_t)(p - path) + len;
		char child_path[4096];
		if (off >= sizeof(child_path))
			off = sizeof(child_path) - 1;
		memcpy(child_path, path, off);
		child_path[off] = '\0';

		if (len >= sizeof(comp))
			len = sizeof(comp) - 1;
		memcpy(comp, p, len);
		comp[len] = '\0';

		cur = child_get_or_add(t, cur, comp, child_path, is_last);
		if (is_last)
			t->nodes[cur].status =
			    status_merge(t->nodes[cur].status, status);

		if (!slash)
			break;
		p = slash + 1;
	}
}

uint32_t tree_find(const tree *t, const char *path)
{
	uint32_t cur = 0;
	const char *p = path;

	while (*p != '\0') {
		const char *slash = strchr(p, '/');
		size_t len = slash ? (size_t)(slash - p) : strlen(p);
		if (len == 0) {
			if (!slash)
				break;
			p = slash + 1;
			continue;
		}

		uint32_t child = t->nodes[cur].first_child;
		uint32_t found = NODE_NIL;
		while (child != NODE_NIL) {
			const char *cn = t->nodes[child].name;
			if (strlen(cn) == len && memcmp(cn, p, len) == 0) {
				found = child;
				break;
			}
			child = t->nodes[child].next_sibling;
		}
		if (found == NODE_NIL)
			return NODE_NIL;
		cur = found;

		if (!slash)
			break;
		p = slash + 1;
	}
	return cur;
}
