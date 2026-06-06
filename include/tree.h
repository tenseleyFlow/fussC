#ifndef FUSSY_TREE_H
#define FUSSY_TREE_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

/* Index sentinel for "no node". */
#define NODE_NIL UINT32_MAX

/* Per-node flags. */
enum {
	NF_FILE = 1u << 0,     /* leaf file (else a directory) */
	NF_EXPANDED = 1u << 1, /* directory shows its children */
};

/*
 * One entry in the flat node arena. Children are a singly-linked sibling list
 * kept in case-insensitive sorted order; `parent` gives O(1) ascent. Indices
 * (not pointers) are used throughout so the arena can grow by realloc. The
 * synthetic root "." lives at index 0.
 */
typedef struct {
	char *name;       /* basename, owned ("." for the root)        */
	char *name_lower; /* ASCII-lowercased name, owned (for fuzzy)   */
	char *path;       /* repo-relative path, owned ("" for root)   */
	file_status status;
	uint8_t flags;
	uint32_t first_child;
	uint32_t next_sibling;
	uint32_t parent;
} node;

typedef struct {
	node *nodes;
	uint32_t len;
	uint32_t cap;
} tree;

/* Initialise an empty tree containing only the synthetic root at index 0. */
void tree_init(tree *t);
void tree_free(tree *t);

/*
 * Add a repo-relative, slash-separated file `path` with `status`, creating
 * intermediate directory nodes as needed. If the leaf already exists its status
 * is merged. Children are inserted in sorted order, so no separate sort pass is
 * required. Empty path components are ignored.
 */
void tree_add(tree *t, const char *path, file_status status);

/* Return the node index for a repo-relative path, or NODE_NIL if absent. */
uint32_t tree_find(const tree *t, const char *path);

static inline bool node_is_file(const node *n)
{
	return (n->flags & NF_FILE) != 0;
}

static inline bool node_is_expanded(const node *n)
{
	return (n->flags & NF_EXPANDED) != 0;
}

#endif /* FUSSY_TREE_H */
