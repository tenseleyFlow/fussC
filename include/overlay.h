#ifndef FUSSY_OVERLAY_H
#define FUSSY_OVERLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Transient modal overlays layered over the tree: a text editor (commit/amend
 * message, tag name, rename) or a yes/no confirmation. This is just the state
 * and UTF-8-safe editing; main runs the git op when the overlay is confirmed,
 * and render draws the box.
 */

#define OVERLAY_TEXT_MAX 512

typedef enum {
	OV_NONE,
	OV_COMMIT,  /* commit or amend message (auto-growing box) */
	OV_TAG,     /* tag name */
	OV_RENAME,  /* rename input */
	OV_CONFIRM, /* y/n confirmation */
} overlay_kind;

enum { CONF_DISCARD, CONF_DELETE };

typedef struct {
	overlay_kind kind;
	char title[48];              /* box title / confirm prompt */
	char text[OVERLAY_TEXT_MAX]; /* editable content */
	size_t len;                  /* byte length of text */
	size_t cursor;               /* byte cursor, on a codepoint boundary */
	bool amend; /* OV_COMMIT: amend rather than new commit */

	/* The path being acted on (OV_RENAME: old path; OV_CONFIRM: target). */
	char target[1024];
	bool target_untracked; /* OV_CONFIRM: target is untracked */
	int confirm_op;        /* OV_CONFIRM: CONF_DISCARD or CONF_DELETE */
} overlay;

static inline bool overlay_active(const overlay *o)
{
	return o->kind != OV_NONE;
}

void overlay_close(overlay *o);

void overlay_open_commit(overlay *o, bool amend, const char *prefill);
void overlay_open_tag(overlay *o);
void overlay_open_rename(overlay *o, const char *current);
void overlay_open_confirm(overlay *o, const char *prompt);

/* Text editing (no-ops while OV_CONFIRM is up). */
void overlay_insert(overlay *o, uint32_t cp);
void overlay_backspace(overlay *o);
void overlay_left(overlay *o);
void overlay_right(overlay *o);

#endif /* FUSSY_OVERLAY_H */
