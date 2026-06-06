#include "overlay.h"

#include <string.h>

#include "util.h"

void overlay_close(overlay *o)
{
	o->kind = OV_NONE;
	o->title[0] = '\0';
	o->text[0] = '\0';
	o->len = 0;
	o->cursor = 0;
	o->amend = false;
	o->target[0] = '\0';
	o->target_untracked = false;
	o->confirm_op = CONF_DISCARD;
	o->remote_count = 0;
	o->remote_sel = 0;
	o->net_op = NET_PUSH;
}

static void set_title(overlay *o, const char *t)
{
	strncpy(o->title, t, sizeof(o->title) - 1);
	o->title[sizeof(o->title) - 1] = '\0';
}

static void set_text(overlay *o, const char *s)
{
	if (s == NULL)
		s = "";
	size_t n = strlen(s);
	if (n >= OVERLAY_TEXT_MAX)
		n = OVERLAY_TEXT_MAX - 1;
	memcpy(o->text, s, n);
	o->text[n] = '\0';
	o->len = n;
	o->cursor = n; /* cursor at end of the prefill */
}

void overlay_open_commit(overlay *o, bool amend, const char *prefill)
{
	o->kind = OV_COMMIT;
	o->amend = amend;
	set_text(o, prefill);
	set_title(o, amend ? "Amend commit" : "Commit message");
}

void overlay_open_tag(overlay *o)
{
	o->kind = OV_TAG;
	o->amend = false;
	set_text(o, "");
	set_title(o, "Tag name");
}

void overlay_open_tag_message(overlay *o, const char *name)
{
	o->kind = OV_TAG_MSG;
	o->amend = false;
	strncpy(o->target, name ? name : "", sizeof(o->target) - 1);
	o->target[sizeof(o->target) - 1] = '\0';
	set_text(o, "");
	set_title(o, "Tag message (empty = lightweight)");
}

void overlay_open_rename(overlay *o, const char *current)
{
	o->kind = OV_RENAME;
	o->amend = false;
	set_text(o, current);
	set_title(o, "Rename to");
	/* Remember the old path; the edited text becomes the new path. */
	strncpy(o->target, current ? current : "", sizeof(o->target) - 1);
	o->target[sizeof(o->target) - 1] = '\0';
}

void overlay_open_confirm(overlay *o, const char *prompt)
{
	o->kind = OV_CONFIRM;
	o->amend = false;
	set_text(o, "");
	set_title(o, prompt);
}

void overlay_open_help(overlay *o)
{
	o->kind = OV_HELP;
	o->amend = false;
	set_text(o, "");
	set_title(o, "Keys");
}

void overlay_open_remote(overlay *o, char *const *names, int count, int net_op)
{
	o->kind = OV_REMOTE;
	set_text(o, "");
	o->net_op = net_op;
	o->remote_sel = 0;
	if (count > OVERLAY_REMOTES_MAX)
		count = OVERLAY_REMOTES_MAX;
	o->remote_count = count;
	for (int i = 0; i < count; i++) {
		strncpy(o->remotes[i], names[i], sizeof(o->remotes[i]) - 1);
		o->remotes[i][sizeof(o->remotes[i]) - 1] = '\0';
	}
	set_title(o, net_op == NET_PUSH   ? "Push to"
	             : net_op == NET_PULL ? "Pull from"
	                                  : "Fetch from");
}

static bool editable(const overlay *o)
{
	return o->kind == OV_COMMIT || o->kind == OV_TAG ||
	       o->kind == OV_TAG_MSG || o->kind == OV_RENAME;
}

/* Step the byte index back to the start of the previous codepoint. */
static size_t prev_cp(const char *s, size_t i)
{
	do {
		i--;
	} while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80);
	return i;
}

void overlay_insert(overlay *o, uint32_t cp)
{
	if (!editable(o))
		return;
	char buf[4];
	int n = utf8_encode(cp, buf);
	if (o->len + (size_t)n + 1 > OVERLAY_TEXT_MAX)
		return;
	/* Shift the tail (and its NUL) right, then drop the new bytes in. */
	memmove(o->text + o->cursor + n, o->text + o->cursor,
	        o->len - o->cursor + 1);
	memcpy(o->text + o->cursor, buf, (size_t)n);
	o->len += (size_t)n;
	o->cursor += (size_t)n;
}

void overlay_backspace(overlay *o)
{
	if (!editable(o) || o->cursor == 0)
		return;
	size_t start = prev_cp(o->text, o->cursor);
	size_t removed = o->cursor - start;
	memmove(o->text + start, o->text + o->cursor, o->len - o->cursor + 1);
	o->len -= removed;
	o->cursor = start;
}

void overlay_left(overlay *o)
{
	if (!editable(o) || o->cursor == 0)
		return;
	o->cursor = prev_cp(o->text, o->cursor);
}

void overlay_right(overlay *o)
{
	if (!editable(o) || o->cursor >= o->len)
		return;
	size_t i = o->cursor + 1;
	while (i < o->len && ((unsigned char)o->text[i] & 0xC0) == 0x80)
		i++;
	o->cursor = i;
}
