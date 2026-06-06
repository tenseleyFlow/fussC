#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "term.h"
#include "util.h"
#include "width.h"

/* Small growable string builder. */
typedef struct {
	char *buf;
	size_t len;
	size_t cap;
} strbuf;

static void sb_put(strbuf *s, const char *str)
{
	size_t n = strlen(str);
	if (s->buf == NULL || s->len + n + 1 > s->cap) {
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

/* ---- interactive rendering ---------------------------------------------- */

static void sb_putc(strbuf *s, char c)
{
	if (s->buf == NULL || s->len + 2 > s->cap) {
		s->cap = s->cap ? s->cap * 2 : 64;
		s->buf = xrealloc(s->buf, s->cap);
	}
	s->buf[s->len++] = c;
	s->buf[s->len] = '\0';
}

/* Copy `in`, keeping SGR escapes (zero width) but stopping once `cols` display
 * columns of real glyphs have been emitted. Closes any styling on truncation.
 */
static char *clip_to_width(const char *in, int cols)
{
	strbuf out = {0};
	int w = 0;
	bool truncated = false;
	const char *p = in;

	while (*p != '\0') {
		if (*p == 0x1B) { /* ESC [ ... <final> : copy verbatim */
			sb_putc(&out, *p++);
			if (*p == '[') {
				sb_putc(&out, *p++);
				while (*p && !(*p >= '@' && *p <= '~'))
					sb_putc(&out, *p++);
				if (*p)
					sb_putc(&out, *p++);
			}
			continue;
		}
		uint32_t cp;
		int n = utf8_decode(p, &cp);
		int cw = cp_width(cp);
		if (w + cw > cols) {
			truncated = true;
			break;
		}
		for (int i = 0; i < n; i++)
			sb_putc(&out, p[i]);
		w += cw;
		p += n;
	}
	if (truncated)
		sb_put(&out, "\033[0m");
	return out.buf ? out.buf : xstrdup("");
}

static char *build_header(const char *repo, const char *branch, const app *a,
                          bool color)
{
	strbuf s = {0};
	if (color)
		sb_put(&s, "\033[36m");
	sb_put(&s, repo ? repo : "");
	if (color)
		sb_put(&s, "\033[0m");
	sb_put(&s, ":");
	if (color)
		sb_put(&s, "\033[33m");
	sb_put(&s, branch ? branch : "");
	if (color)
		sb_put(&s, "\033[0m");
	(void)a; /* the live query lives in the footer, not the header */
	return s.buf ? s.buf : xstrdup("");
}

/* Footer row 1: the grey "type:" slot shows the live filter in place of the
 * word "filter"; a pending git-op status message takes over the whole row. */
static char *build_footer_nav(const app *a, bool color)
{
	strbuf s = {0};
	if (a->status[0] != '\0') {
		if (color)
			sb_put(&s, "\033[33m");
		sb_put(&s, a->status);
		if (color)
			sb_put(&s, "\033[0m");
		return s.buf ? s.buf : xstrdup("");
	}
	if (color)
		sb_put(&s, "\033[90m");
	sb_put(&s, "type:");
	sb_put(&s, a->filter_len > 0 ? a->filter : "filter");
	sb_put(&s, "  \342\206\221\342\206\223 sibling  \342\206\222 in  "
	           "\342\206\220 out  Space peek  H hidden  ?:help  Q quit");
	if (color)
		sb_put(&s, "\033[0m");
	return s.buf ? s.buf : xstrdup("");
}

/* Footer row 2: git commands, ordered by frequency so narrow terminals clip the
 * least-used keys first (the full set lives in the ? overlay). */
static char *build_footer_git(bool color)
{
	strbuf s = {0};
	if (color)
		sb_put(&s, "\033[90m");
	sb_put(&s, "A stage  C commit  P push  L pull  F fetch  U unstage  "
	           "X discard  D delete  R rename  T tag  M amend  "
	           "V view  G status  S/Z all");
	if (color)
		sb_put(&s, "\033[0m");
	return s.buf ? s.buf : xstrdup("");
}

static char *build_tree_line(const tree *t, uint32_t idx, const bool *lad,
                             uint16_t depth, bool is_last, bool color,
                             bool selected, int cols)
{
	strbuf c = {0};
	for (uint16_t a = 0; a < depth; a++)
		sb_put(&c, lad[a] ? G_BLANK : G_PIPE);
	sb_put(&c, is_last ? G_ELL : G_TEE);

	const node *n = &t->nodes[idx];
	if (!node_is_file(n))
		sb_put(&c, node_is_expanded(n) ? "\342\226\274 "   /* down */
		                               : "\342\226\266 "); /* right */

	bool use_color = color && !selected;
	bool ignored = (n->status & ST_GITIGNORED) != 0;
	if (use_color && ignored)
		sb_put(&c, "\033[90m");
	sb_put(&c, n->name);
	if (use_color && ignored)
		sb_put(&c, "\033[0m");
	append_status(&c, n->status, use_color);

	strbuf s = {0};
	if (selected) {
		int w = (int)display_width(c.buf ? c.buf : "");
		if (color)
			sb_put(&s, "\033[7m");
		sb_put(&s, c.buf ? c.buf : "");
		for (int i = w; i < cols; i++)
			sb_putc(&s, ' ');
		if (color)
			sb_put(&s, "\033[0m");
	} else {
		sb_put(&s, c.buf ? c.buf : "");
	}
	free(c.buf);

	char *clipped = clip_to_width(s.buf ? s.buf : "", cols);
	free(s.buf);
	return clipped;
}

/* is_last[i] = visible row i is the last visible child of its parent. */
static bool *compute_is_last(const flat_list *f, uint16_t *out_maxd)
{
	uint16_t maxd = 0;
	for (uint32_t i = 0; i < f->len; i++)
		if (f->rows[i].depth > maxd)
			maxd = f->rows[i].depth;
	*out_maxd = maxd;
	if (f->len == 0)
		return NULL;

	bool *is_last = xmalloc(f->len * sizeof(*is_last));
	int *pending = xmalloc((size_t)(maxd + 1) * sizeof(*pending));
	for (uint16_t k = 0; k <= maxd; k++)
		pending[k] = -1;

	for (uint32_t i = 0; i < f->len; i++) {
		uint16_t d = f->rows[i].depth;
		is_last[i] = true;
		if (pending[d] >= 0)
			is_last[pending[d]] = false;
		pending[d] = (int)i;
		for (uint16_t k = (uint16_t)(d + 1); k <= maxd; k++)
			pending[k] = -1;
	}
	free(pending);
	return is_last;
}

/* ---- modal overlay ------------------------------------------------------- */

#define BOX_TL       "\342\224\214" /* corners + edges */
#define BOX_TR       "\342\224\220"
#define BOX_BL       "\342\224\224"
#define BOX_BR       "\342\224\230"
#define BOX_H        "\342\224\200"
#define BOX_V        "\342\224\202"
#define CURSOR       "\342\226\210" /* block cursor */
#define OV_MAX_LINES 6

static void sb_putn(strbuf *s, const char *str, size_t n)
{
	for (size_t i = 0; i < n; i++)
		sb_putc(s, str[i]);
}

/* Wrap a UTF-8 string into lines of at most `width` display columns (never
 * splitting a codepoint). Always yields at least one line. */
static char **wrap_text(const char *s, int width, int *count)
{
	char **lines = NULL;
	int n = 0, cap = 0;
	strbuf cur = {0};
	int curw = 0;

	for (const char *p = s; *p != '\0';) {
		uint32_t cp;
		int len = utf8_decode(p, &cp);
		int w = cp_width(cp);
		if (curw > 0 && curw + w > width) {
			if (n == cap) {
				cap = cap ? cap * 2 : 4;
				lines = xrealloc(lines,
				                 (size_t)cap * sizeof(*lines));
			}
			lines[n++] = cur.buf ? cur.buf : xstrdup("");
			cur = (strbuf){0};
			curw = 0;
		}
		sb_putn(&cur, p, (size_t)len);
		curw += w;
		p += len;
	}
	if (n == cap) {
		cap = cap ? cap * 2 : 4;
		lines = xrealloc(lines, (size_t)cap * sizeof(*lines));
	}
	lines[n++] = cur.buf ? cur.buf : xstrdup("");
	*count = n;
	return lines;
}

/* Place `seg` at (row, col), indented with spaces, clipped to cols. */
static void box_line(char **lines, int rows, int cols, int row, int col,
                     const char *seg)
{
	if (row < 0 || row >= rows)
		return;
	strbuf s = {0};
	for (int i = 0; i < col; i++)
		sb_putc(&s, ' ');
	sb_put(&s, seg);
	free(lines[row]);
	lines[row] = clip_to_width(s.buf ? s.buf : "", cols);
	free(s.buf);
}

/* Append `text` then pad with spaces to `width` DISPLAY columns (so columns of
 * arrow glyphs - 1 column but multi-byte - still line up). */
static void pad_to(strbuf *s, const char *text, int width)
{
	sb_put(s, text);
	for (int w = (int)display_width(text); w < width; w++)
		sb_putc(s, ' ');
}

/* The keymap reference, formatted as aligned two-column rows. */
static char **build_help(int *count)
{
	static const char *const NAV[][2] = {
	    {"\342\206\221 / Ctrl-P", "previous sibling"},
	    {"\342\206\223 / Ctrl-N", "next sibling"},
	    {"\342\206\222 / Ctrl-F", "enter directory"},
	    {"\342\206\220 / Ctrl-B", "up a level / collapse"},
	    {"Space", "expand / collapse"},
	    {"type\342\200\246", "fuzzy-jump to a file"},
	    {"H", "toggle hidden files"},
	};
	static const char *const GIT[][2] = {
	    {"A stage", "U unstage"}, {"S stage all", "Z unstage all"},
	    {"C commit", "M amend"},  {"P push", "L pull"},
	    {"F fetch", "T tag"},     {"X discard", "D delete"},
	    {"R rename", "V view"},   {"G status", ""},
	};
	int nav_n = (int)(sizeof(NAV) / sizeof(*NAV));
	int git_n = (int)(sizeof(GIT) / sizeof(*GIT));

	char **lines = xmalloc((size_t)(nav_n + git_n + 4) * sizeof(*lines));
	int n = 0;
	lines[n++] = xstrdup("Navigation");
	for (int i = 0; i < nav_n; i++) {
		strbuf s = {0};
		sb_put(&s, "  ");
		pad_to(&s, NAV[i][0], 13);
		sb_put(&s, NAV[i][1]);
		lines[n++] = s.buf ? s.buf : xstrdup("");
	}
	lines[n++] = xstrdup("Git");
	for (int i = 0; i < git_n; i++) {
		strbuf s = {0};
		sb_put(&s, "  ");
		pad_to(&s, GIT[i][0], 16);
		sb_put(&s, GIT[i][1]);
		lines[n++] = s.buf ? s.buf : xstrdup("");
	}
	lines[n++] = xstrdup("Q quit    ? help    Esc cancel");
	*count = n;
	return lines;
}

static void draw_overlay(char **lines, int rows, int cols, const overlay *o,
                         bool color)
{
	int inner = cols - 8;
	if (inner < 16)
		inner = 16;
	if (inner > 56)
		inner = 56;

	/* Content lines: the keymap for help, a fixed hint for confirm, else
	 * the wrapped input with a block cursor spliced in at the edit pos. */
	int ncontent = 0;
	char **content;
	if (o->kind == OV_HELP) {
		content = build_help(&ncontent);
		if (inner < 40)
			inner = 40; /* keep the aligned columns readable */
	} else if (o->kind == OV_REMOTE) {
		ncontent = o->remote_count;
		content = xmalloc((size_t)(ncontent ? ncontent : 1) *
		                  sizeof(*content));
		for (int i = 0; i < ncontent; i++) {
			strbuf s = {0};
			sb_put(&s, i == o->remote_sel ? "\342\206\222 " : "  ");
			sb_put(&s, o->remotes[i]);
			content[i] = s.buf ? s.buf : xstrdup("");
		}
		if (ncontent == 0)
			content[ncontent++] = xstrdup("  (no remotes)");
	} else if (o->kind == OV_CONFIRM) {
		content = xmalloc(sizeof(*content));
		content[0] = xstrdup("y: yes    n: no");
		ncontent = 1;
	} else {
		strbuf disp = {0};
		sb_putn(&disp, o->text, o->cursor);
		sb_put(&disp, CURSOR);
		sb_put(&disp, o->text + o->cursor);
		content =
		    wrap_text(disp.buf ? disp.buf : CURSOR, inner, &ncontent);
		free(disp.buf);
		if (ncontent > OV_MAX_LINES) { /* keep the tail (cursor area) */
			int drop = ncontent - OV_MAX_LINES;
			for (int i = 0; i < drop; i++)
				free(content[i]);
			memmove(content, content + drop,
			        (size_t)OV_MAX_LINES * sizeof(*content));
			ncontent = OV_MAX_LINES;
		}
	}

	const char *hint = o->kind == OV_CONFIRM ? ""
	                   : o->kind == OV_HELP  ? "any key to close"
	                   : o->kind == OV_REMOTE
	                       ? "\342\206\221\342\206\223 select  "
	                         "Enter  Esc cancel"
	                   : o->kind == OV_COMMIT ? "Enter commit  Esc cancel"
	                   : o->kind == OV_RENAME ? "Enter rename  Esc cancel"
	                   : o->kind == OV_TAG    ? "Enter next  Esc cancel"
	                                       : "Enter create tag  Esc cancel";
	bool has_hint = hint[0] != '\0';

	int box_w = inner + 4; /* BOX_V + space + inner + space + BOX_V */
	int box_h = 1 + ncontent + 1;
	int total_h = box_h + (has_hint ? 1 : 0);
	int start_row = (rows - total_h) / 2;
	if (start_row < 0)
		start_row = 0;
	int start_col = (cols - box_w) / 2;
	if (start_col < 0)
		start_col = 0;

	/* Top border with embedded title. */
	strbuf top = {0};
	if (color)
		sb_put(&top, "\033[1m");
	sb_put(&top, BOX_TL BOX_H " ");
	sb_put(&top, o->title);
	sb_put(&top, " ");
	int fill = (box_w - 2) - (3 + (int)display_width(o->title));
	for (int i = 0; i < fill; i++)
		sb_put(&top, BOX_H);
	sb_put(&top, BOX_TR);
	if (color)
		sb_put(&top, "\033[0m");
	box_line(lines, rows, cols, start_row, start_col,
	         top.buf ? top.buf : "");
	free(top.buf);

	/* Content rows. */
	for (int i = 0; i < ncontent; i++) {
		strbuf s = {0};
		sb_put(&s, BOX_V " ");
		sb_put(&s, content[i]);
		int w = (int)display_width(content[i]);
		for (int k = w; k < inner; k++)
			sb_putc(&s, ' ');
		sb_put(&s, " " BOX_V);
		box_line(lines, rows, cols, start_row + 1 + i, start_col,
		         s.buf ? s.buf : "");
		free(s.buf);
	}

	/* Bottom border. */
	strbuf bot = {0};
	sb_put(&bot, BOX_BL);
	for (int i = 0; i < box_w - 2; i++)
		sb_put(&bot, BOX_H);
	sb_put(&bot, BOX_BR);
	box_line(lines, rows, cols, start_row + box_h - 1, start_col,
	         bot.buf ? bot.buf : "");
	free(bot.buf);

	if (has_hint) {
		strbuf s = {0};
		if (color)
			sb_put(&s, "\033[90m");
		sb_put(&s, hint);
		if (color)
			sb_put(&s, "\033[0m");
		box_line(lines, rows, cols, start_row + box_h, start_col,
		         s.buf ? s.buf : "");
		free(s.buf);
	}

	for (int i = 0; i < ncontent; i++)
		free(content[i]);
	free(content);
}

char **render_frame(const app *a, const char *repo, const char *branch,
                    bool color, int rows, int cols)
{
	char **lines = xmalloc((size_t)rows * sizeof(*lines));
	for (int r = 0; r < rows; r++)
		lines[r] = NULL;

	if (rows >= 1) {
		char *h = build_header(repo, branch, a, color);
		lines[0] = clip_to_width(h, cols);
		free(h);
	}

	/* Footer is two rows (nav + git) when there's room, one when cramped.
	 */
	int foot_rows = rows >= 3 ? 2 : (rows >= 2 ? 1 : 0);
	int tree_top = 1;
	int tree_h = rows - 1 - foot_rows;
	if (tree_h < 0)
		tree_h = 0;

	const flat_list *f = &a->visible;
	uint16_t maxd = 0;
	bool *is_last = compute_is_last(f, &maxd);
	/* maxd+1 is always >= 1 (maxd is uint16_t), so this never allocates 0.
	 */
	bool *lad = xmalloc((size_t)(maxd + 1) * sizeof(bool));
	for (uint16_t k = 0; k <= maxd; k++)
		lad[k] = false;

	int len = (int)f->len;
	int start = 0;
	if (len > tree_h) {
		start = (int)a->selected - tree_h / 2;
		if (start < 0)
			start = 0;
		if (start > len - tree_h)
			start = len - tree_h;
	}

	/* Advance the ancestor-last state up to the first visible row. */
	for (int i = 0; i < start && i < len; i++)
		lad[f->rows[i].depth] = is_last[i];

	for (int r = 0; r < tree_h; r++) {
		int vis = start + r;
		int row = tree_top + r;
		if (vis < len) {
			uint16_t d = f->rows[vis].depth;
			lad[d] = is_last[vis];
			lines[row] = build_tree_line(
			    &a->t, f->rows[vis].node, lad, d, is_last[vis],
			    color, vis == (int)a->selected, cols);
		} else {
			lines[row] = xstrdup("");
		}
	}

	/* Empty viewport: the working tree is clean (or the repo is empty).
	 * Fuzzy jumps rather than filters, so len==0 is never a no-match state.
	 */
	if (len == 0 && tree_h > 0) {
		const char *hint = "working tree clean";
		int w = (int)display_width(hint);
		int pad = (cols - w) / 2;
		if (pad < 0)
			pad = 0;
		strbuf s = {0};
		if (color)
			sb_put(&s, "\033[90m");
		for (int i = 0; i < pad; i++)
			sb_putc(&s, ' ');
		sb_put(&s, hint);
		if (color)
			sb_put(&s, "\033[0m");
		free(lines[tree_top + tree_h / 2]);
		lines[tree_top + tree_h / 2] =
		    clip_to_width(s.buf ? s.buf : "", cols);
		free(s.buf);
	}

	if (foot_rows >= 1) { /* nav row */
		char *ft = build_footer_nav(a, color);
		lines[rows - foot_rows] = clip_to_width(ft, cols);
		free(ft);
	}
	if (foot_rows >= 2) { /* git row */
		char *ft = build_footer_git(color);
		lines[rows - 1] = clip_to_width(ft, cols);
		free(ft);
	}

	if (overlay_active(&a->ov))
		draw_overlay(lines, rows, cols, &a->ov, color);

	free(is_last);
	free(lad);
	return lines;
}

void free_frame(char **lines, int rows)
{
	if (!lines)
		return;
	for (int r = 0; r < rows; r++)
		free(lines[r]);
	free(lines);
}

int frame_diff(char *const *oldf, char *const *newf, int rows, FILE *out)
{
	int changed = 0;
	for (int r = 0; r < rows; r++) {
		const char *o = oldf ? oldf[r] : NULL;
		if (o == NULL || strcmp(o, newf[r]) != 0) {
			fprintf(out, "\033[%d;1H\033[2K%s", r + 1, newf[r]);
			changed++;
		}
	}
	return changed;
}

void screen_init(screen *s)
{
	s->prev = NULL;
	s->rows = 0;
}

void screen_free(screen *s)
{
	free_frame(s->prev, s->rows);
	s->prev = NULL;
	s->rows = 0;
}

void screen_invalidate(screen *s)
{
	free_frame(s->prev, s->rows);
	s->prev = NULL;
	s->rows = 0;
}

void screen_draw(screen *s, const app *a, const char *repo, const char *branch,
                 bool color)
{
	int rows, cols;
	term_size(&rows, &cols);

	bool full = (s->prev == NULL || s->rows != rows);
	if (full) {
		free_frame(s->prev, s->rows);
		s->prev = NULL;
		s->rows = rows;
	}

	char **frame = render_frame(a, repo, branch, color, rows, cols);

	char *buf = NULL;
	size_t blen = 0;
	FILE *m = open_memstream(&buf, &blen);
	if (m) {
		if (full)
			fputs("\033[2J", m);
		frame_diff(s->prev, frame, rows, m);
		fclose(m);
		if (blen > 0 && write(STDOUT_FILENO, buf, blen) < 0)
			(void)0;
	}
	free(buf);

	free_frame(s->prev, s->rows);
	s->prev = frame;
	s->rows = rows;
}
