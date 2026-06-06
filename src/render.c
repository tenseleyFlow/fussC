#include "render.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

/* Small growable string builder. */
typedef struct {
	char *buf;
	size_t len;
	size_t cap;
} strbuf;

static void sb_put(strbuf *s, const char *str)
{
	size_t n = strlen(str);
	if (s->len + n + 1 > s->cap) {
		while (s->len + n + 1 > s->cap)
			s->cap = s->cap ? s->cap * 2 : 256;
		s->buf = xrealloc(s->buf, s->cap);
	}
	memcpy(s->buf + s->len, str, n);
	s->len += n;
	s->buf[s->len] = '\0';
}

/* Box-drawing gutter pieces (UTF-8). */
#define G_PIPE  "\342\224\202   " /* "|   " vertical */
#define G_BLANK "    "
#define G_TEE   "\342\224\234\342\224\200\342\224\200 " /* "|-- " */
#define G_ELL   "\342\224\224\342\224\200\342\224\200 " /* "`-- " */

static void append_status(strbuf *s, file_status st, bool color)
{
	if (st & ST_STAGED)
		sb_put(s, color ? "\033[32m \342\206\221\033[0m"
		                : " \342\206\221");
	if (st & ST_UNSTAGED)
		sb_put(s, color ? "\033[31m \342\234\227\033[0m"
		                : " \342\234\227");
	if (st & ST_UNTRACKED)
		sb_put(s, color ? "\033[90m \342\234\227\033[0m"
		                : " \342\234\227");
	if (st & ST_INCOMING)
		sb_put(s, color ? "\033[34m \342\206\223\033[0m"
		                : " \342\206\223");
}

char *render_tree_string(const tree *t, const flat_list *f, bool color)
{
	strbuf s = {0};
	sb_put(&s, ".\n");

	/* last_at_depth[d] = was the node currently on the path at depth d the
	 * last child of its parent? Drives whether ancestor columns draw a
	 * vertical bar or blank. Grows to the deepest row. */
	bool *last_at_depth = NULL;
	uint16_t cap_depth = 0;

	for (uint32_t i = 0; i < f->len; i++) {
		uint32_t idx = f->rows[i].node;
		uint16_t d = f->rows[i].depth;
		const node *n = &t->nodes[idx];
		bool is_last = (n->next_sibling == NODE_NIL);

		if (d >= cap_depth) {
			uint16_t newcap = (uint16_t)(d + 1);
			last_at_depth = xrealloc(
			    last_at_depth, newcap * sizeof(*last_at_depth));
			cap_depth = newcap;
		}
		last_at_depth[d] = is_last;

		for (uint16_t a = 0; a < d; a++)
			sb_put(&s, last_at_depth[a] ? G_BLANK : G_PIPE);
		sb_put(&s, is_last ? G_ELL : G_TEE);

		bool ignored = (n->status & ST_GITIGNORED) != 0;
		if (ignored && color)
			sb_put(&s, "\033[90m");
		sb_put(&s, n->name);
		if (ignored && color)
			sb_put(&s, "\033[0m");

		append_status(&s, n->status, color);
		sb_put(&s, "\n");
	}

	free(last_at_depth);
	return s.buf;
}

void render_tree(FILE *out, const tree *t, const flat_list *f, bool color)
{
	char *str = render_tree_string(t, f, color);
	fputs(str, out);
	free(str);
}
