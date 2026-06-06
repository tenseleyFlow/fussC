#include "app.h"

#include <string.h>

void app_init(app *a)
{
	tree_init(&a->t);
	flat_init(&a->visible);
	a->selected = 0;
	a->hide_dotfiles = false;
	a->filter_len = 0;
	a->filter[0] = '\0';
	a->last_input_ns = 0;
	flatten(&a->visible, &a->t, a->hide_dotfiles);
}

void app_free(app *a)
{
	flat_free(&a->visible);
	tree_free(&a->t);
}

uint32_t app_selected_node(const app *a)
{
	return a->selected < a->visible.len ? a->visible.rows[a->selected].node
	                                    : NODE_NIL;
}

void app_reflatten(app *a)
{
	uint32_t sel = app_selected_node(a);
	flatten(&a->visible, &a->t, a->hide_dotfiles);

	a->selected = 0;
	if (sel != NODE_NIL) {
		for (uint32_t i = 0; i < a->visible.len; i++) {
			if (a->visible.rows[i].node == sel) {
				a->selected = i;
				break;
			}
		}
	}
	if (a->visible.len > 0 && a->selected >= a->visible.len)
		a->selected = a->visible.len - 1;
}

void app_down(app *a)
{
	/* Next sibling: scan forward, skipping our descendants (deeper rows),
	 * to the next row at the same depth. Stop if we leave this level. */
	if (a->selected >= a->visible.len)
		return;
	uint16_t d = a->visible.rows[a->selected].depth;
	for (uint32_t j = a->selected + 1; j < a->visible.len; j++) {
		uint16_t dj = a->visible.rows[j].depth;
		if (dj < d)
			return; /* left this subtree; we were the last sibling
			         */
		if (dj == d) {
			a->selected = j;
			return;
		}
	}
}

void app_up(app *a)
{
	/* Previous sibling: scan backward, skipping the previous sibling's
	 * descendants, to the nearest earlier row at the same depth. */
	if (a->selected == 0)
		return;
	uint16_t d = a->visible.rows[a->selected].depth;
	for (uint32_t j = a->selected; j > 0;) {
		j--;
		uint16_t dj = a->visible.rows[j].depth;
		if (dj < d)
			return; /* reached the parent; we were the first sibling
			         */
		if (dj == d) {
			a->selected = j;
			return;
		}
	}
}

void app_home(app *a)
{
	a->selected = 0;
}

void app_end(app *a)
{
	a->selected = a->visible.len > 0 ? a->visible.len - 1 : 0;
}

void app_toggle(app *a)
{
	if (a->selected >= a->visible.len)
		return;
	uint32_t idx = a->visible.rows[a->selected].node;
	if (node_is_file(&a->t.nodes[idx]))
		return;
	flat_toggle(&a->visible, &a->t, a->selected, a->hide_dotfiles);
}

void app_right(app *a)
{
	/* Enter a directory: expand it if collapsed, then step onto its first
	 * visible child. No-op on a file. */
	if (a->selected >= a->visible.len)
		return;
	flat_row r = a->visible.rows[a->selected];
	node *n = &a->t.nodes[r.node];
	if (node_is_file(n))
		return;
	if (!node_is_expanded(n))
		app_toggle(a); /* expand in place (splices children after us) */
	if (a->selected + 1 < a->visible.len &&
	    a->visible.rows[a->selected + 1].depth == r.depth + 1)
		a->selected++;
}

void app_left(app *a)
{
	if (a->selected >= a->visible.len)
		return;
	flat_row r = a->visible.rows[a->selected];
	node *n = &a->t.nodes[r.node];
	if (!node_is_file(n) && node_is_expanded(n)) {
		app_toggle(a); /* expanded dir -> collapse, stay put */
		return;
	}
	if (r.depth == 0)
		return;
	/* go to parent: nearest preceding row one level shallower */
	for (uint32_t i = a->selected; i > 0;) {
		i--;
		if (a->visible.rows[i].depth == r.depth - 1) {
			a->selected = i;
			return;
		}
	}
}

void app_toggle_dotfiles(app *a)
{
	a->hide_dotfiles = !a->hide_dotfiles;
	app_reflatten(a);
}

static int utf8_encode(uint32_t cp, char out[4])
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

void app_filter_push(app *a, uint32_t cp)
{
	char tmp[4];
	int n = utf8_encode(cp, tmp);
	if (a->filter_len + (size_t)n + 1 > FILTER_MAX)
		return;
	memcpy(a->filter + a->filter_len, tmp, (size_t)n);
	a->filter_len += (size_t)n;
	a->filter[a->filter_len] = '\0';
}

void app_filter_backspace(app *a)
{
	if (a->filter_len == 0)
		return;
	size_t i = a->filter_len;
	do {
		i--;
	} while (i > 0 && ((unsigned char)a->filter[i] & 0xC0) == 0x80);
	a->filter_len = i;
	a->filter[i] = '\0';
}

void app_filter_clear(app *a)
{
	a->filter_len = 0;
	a->filter[0] = '\0';
}

/* Record a keystroke at `now_ns`, first clearing the buffer if it has gone idle
 * past the timeout (so typing after a pause starts a fresh query). */
void app_filter_age(app *a, uint64_t now_ns)
{
	uint64_t gap = (uint64_t)FILTER_TIMEOUT_MS * 1000000u;
	if (a->filter_len > 0 && now_ns - a->last_input_ns >= gap)
		app_filter_clear(a);
	a->last_input_ns = now_ns;
}

/* True if the buffer is non-empty and has gone idle past the timeout. The main
 * loop uses this to auto-clear (and redraw) without a keystroke. */
bool app_filter_expired(const app *a, uint64_t now_ns)
{
	uint64_t gap = (uint64_t)FILTER_TIMEOUT_MS * 1000000u;
	return a->filter_len > 0 && now_ns - a->last_input_ns >= gap;
}

void app_expand_to(app *a, uint32_t node)
{
	if (node == NODE_NIL)
		return;
	uint32_t p = a->t.nodes[node].parent;
	while (p != NODE_NIL && p != 0) { /* up to, not including, the root */
		a->t.nodes[p].flags |= NF_EXPANDED;
		p = a->t.nodes[p].parent;
	}
}

void app_apply_match(app *a, uint32_t node)
{
	if (node == NODE_NIL)
		return; /* no match: leave the selection where it is */
	app_expand_to(a, node);
	app_reflatten(a);
	for (uint32_t i = 0; i < a->visible.len; i++) {
		if (a->visible.rows[i].node == node) {
			a->selected = i;
			break;
		}
	}
}
