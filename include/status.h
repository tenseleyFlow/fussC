#ifndef FUSSY_STATUS_H
#define FUSSY_STATUS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * A file's git state as a small bitfield. A node can hold several bits at once
 * (a file staged AND with further unstaged edits is ST_STAGED | ST_UNSTAGED).
 * Mirrors the five-flag model the originals used, but as one byte.
 */
typedef uint8_t file_status;

/*
 * Bit         Meaning                            Rendered as
 * ST_STAGED    change recorded in the index       up arrow, green
 * ST_UNSTAGED  tracked file modified in worktree  x, red
 * ST_UNTRACKED new file, not yet tracked          x, grey
 * ST_INCOMING  differs from upstream              down arrow, blue
 * ST_GITIGNORED ignored by .gitignore             name dimmed
 */
enum {
	ST_STAGED = 1u << 0,
	ST_UNSTAGED = 1u << 1,
	ST_UNTRACKED = 1u << 2,
	ST_INCOMING = 1u << 3,
	ST_GITIGNORED = 1u << 4,
};

/* "Dirty" means the working tree has something to act on. Incoming and ignored
 * are informational, not dirty. */
#define ST_DIRTY_MASK ((file_status)(ST_STAGED | ST_UNSTAGED | ST_UNTRACKED))

static inline file_status status_merge(file_status a, file_status b)
{
	return (file_status)(a | b);
}

static inline bool status_is_dirty(file_status s)
{
	return (s & ST_DIRTY_MASK) != 0;
}

#endif /* FUSSY_STATUS_H */
