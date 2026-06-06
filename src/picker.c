#include "picker.h"

#include <stdlib.h>
#include <string.h>

#include "fuzzy.h"
#include "strbuf.h"
#include "term.h"
#include "util.h"
#include "width.h"

/* Lowercase ASCII into `out` (bounded, NUL-terminated); other bytes pass
 * through. fuzzy_score expects already-lowercased inputs - this is the
 * case-fold step. */
static void ascii_lower(const char *in, char *out, size_t cap)
{
	size_t i = 0;
	for (; in[i] != '\0' && i + 1 < cap; i++) {
		char c = in[i];
		out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
	}
	out[i] = '\0';
}

struct scored {
	int idx;
	int score;
};

static int cmp_scored(const void *a, const void *b)
{
	const struct scored *x = a, *y = b;
	if (x->score != y->score)
		return y->score - x->score; /* higher score first */
	return x->idx - y->idx;             /* stable: original order on ties */
}

int *picker_filter(char *const *items, int count, const char *query, int *n_out)
{
	if (count < 0)
		count = 0;
	int *out = xmalloc((size_t)(count > 0 ? count : 1) * sizeof(*out));

	if (query == NULL || query[0] == '\0') {
		for (int i = 0; i < count; i++)
			out[i] = i;
		*n_out = count;
		return out;
	}

	char qlc[256];
	ascii_lower(query, qlc, sizeof(qlc));

	struct scored *sc =
	    xmalloc((size_t)(count > 0 ? count : 1) * sizeof(*sc));
	int m = 0;

	size_t cap = 64;
	char *tmp = xmalloc(cap);
	for (int i = 0; i < count; i++) {
		const char *it = items[i] ? items[i] : "";
		size_t need = strlen(it) + 1;
		if (need > cap) {
			while (need > cap)
				cap *= 2;
			tmp = xrealloc(tmp, cap);
		}
		ascii_lower(it, tmp, cap);
		int s = fuzzy_score(qlc, tmp);
		if (s > SCORE_NONE) {
			sc[m].idx = i;
			sc[m].score = s;
			m++;
		}
	}
	free(tmp);

	qsort(sc, (size_t)m, sizeof(*sc), cmp_scored);
	for (int i = 0; i < m; i++)
		out[i] = sc[i].idx;
	free(sc);

	*n_out = m;
	return out;
}

/* Clip `src` to cols and store as lines[row] (frees any prior value). */
static void set_line(char **lines, int row, const char *src, int cols)
{
	free(lines[row]);
	lines[row] = clip_to_width(src, cols);
}

char **render_picker_frame(const picker_view *v, int rows, int cols, bool color)
{
	char **lines = xmalloc((size_t)(rows > 0 ? rows : 1) * sizeof(*lines));
	for (int i = 0; i < rows; i++)
		lines[i] = NULL;
	if (rows <= 0)
		return lines;

	/* Title bar: "title   m/n", a full-width reverse-video bar when
	 * colored. */
	{
		strbuf text = {0};
		sb_put(&text, v->title ? v->title : "");
		char counter[48];
		snprintf(counter, sizeof(counter), "  %d/%d", v->match_count,
		         v->total);
		sb_put(&text, counter);

		strbuf s = {0};
		if (color) {
			sb_put(&s, "\033[7m");
			sb_put(&s, text.buf ? text.buf : "");
			int w = (int)display_width(text.buf ? text.buf : "");
			for (int k = w; k < cols; k++)
				sb_putc(&s, ' ');
			sb_put(&s, "\033[0m");
		} else {
			sb_put(&s, text.buf ? text.buf : "");
		}
		set_line(lines, 0, s.buf ? s.buf : "", cols);
		free(text.buf);
		free(s.buf);
	}

	int hint_rows = rows >= 4 ? 1 : 0;
	if (rows >= 2) {
		strbuf s = {0};
		if (color)
			sb_put(&s, "\033[90m");
		sb_put(&s, "> ");
		if (color)
			sb_put(&s, "\033[0m");
		sb_put(&s, v->query ? v->query : "");
		if (color) /* block cursor */
			sb_put(&s, "\033[7m \033[0m");
		set_line(lines, 1, s.buf ? s.buf : "> ", cols);
		free(s.buf);
	}

	int list_top = rows >= 2 ? 2 : 1;
	int list_bottom = rows - hint_rows; /* exclusive */
	int list_h = list_bottom - list_top;
	if (list_h < 0)
		list_h = 0;

	if (v->match_count == 0 && list_h > 0) {
		strbuf s = {0};
		if (color)
			sb_put(&s, "\033[90m");
		sb_put(&s, "  (no matches)");
		if (color)
			sb_put(&s, "\033[0m");
		set_line(lines, list_top, s.buf ? s.buf : "", cols);
		free(s.buf);
	}

	int start = 0;
	if (v->match_count > list_h) {
		start = v->sel - list_h / 2;
		if (start < 0)
			start = 0;
		if (start > v->match_count - list_h)
			start = v->match_count - list_h;
	}

	for (int r = 0; r < list_h; r++) {
		int mi = start + r;
		if (mi >= v->match_count)
			break;
		int row = list_top + r;
		const char *text = v->items[v->matches[mi]];
		if (text == NULL)
			text = "";

		if (mi == v->sel && color) {
			strbuf s = {0};
			sb_put(&s, "\033[7m");
			sb_put(&s, text);
			int w = (int)display_width(text);
			for (int k = w; k < cols; k++)
				sb_putc(&s, ' ');
			sb_put(&s, "\033[0m");
			set_line(lines, row, s.buf ? s.buf : "", cols);
			free(s.buf);
		} else if (mi == v->sel) {
			/* no color: mark the selection with a leading caret */
			strbuf s = {0};
			sb_put(&s, "> ");
			sb_put(&s, text);
			set_line(lines, row, s.buf ? s.buf : "", cols);
			free(s.buf);
		} else {
			set_line(lines, row, text, cols);
		}
	}

	if (hint_rows) {
		strbuf s = {0};
		if (color)
			sb_put(&s, "\033[90m");
		sb_put(&s, "\342\206\221\342\206\223 move  Enter select  "
		           "Esc cancel");
		if (color)
			sb_put(&s, "\033[0m");
		set_line(lines, rows - 1, s.buf ? s.buf : "", cols);
		free(s.buf);
	}

	for (int i = 0; i < rows; i++)
		if (lines[i] == NULL)
			lines[i] = xstrdup("");

	return lines;
}

int picker_run(screen *s, const picker_spec *spec, bool color)
{
	screen_invalidate(s); /* we own the screen now: full paint */

	char query[256];
	size_t qlen = 0;
	query[0] = '\0';

	int mcount = 0;
	int *matches = picker_filter(spec->items, spec->count, query, &mcount);
	int sel = 0;
	int result = -1;
	bool running = true;

	while (running) {
		int rows, cols;
		term_size(&rows, &cols);
		picker_view v = {.title = spec->title,
		                 .items = spec->items,
		                 .matches = matches,
		                 .match_count = mcount,
		                 .total = spec->count,
		                 .sel = sel,
		                 .query = query};
		char **frame = render_picker_frame(&v, rows, cols, color);
		screen_present(s, frame, rows);

		int key = term_read_key();
		bool refilter = false;
		switch (key) {
		case KEY_EOF:
		case KEY_ESC:
			result = -1;
			running = false;
			break;
		case KEY_ENTER:
			if (mcount > 0)
				result = matches[sel];
			running = false;
			break;
		case KEY_UP:
		case KEY_CTRL('P'):
			if (sel > 0)
				sel--;
			break;
		case KEY_DOWN:
		case KEY_CTRL('N'):
			if (sel + 1 < mcount)
				sel++;
			break;
		case KEY_RESIZE:
			break; /* loop redraws at the new size */
		case KEY_BACKSPACE:
			if (qlen > 0) {
				size_t i = qlen;
				do {
					i--;
				} while (i > 0 && ((unsigned char)query[i] &
				                   0xC0) == 0x80);
				query[i] = '\0';
				qlen = i;
				refilter = true;
			}
			break;
		default:
			if (key >= 0x20 && key < KEY_SPECIAL_BASE) {
				char buf[4];
				int n = utf8_encode((uint32_t)key, buf);
				if (qlen + (size_t)n + 1 < sizeof(query)) {
					memcpy(query + qlen, buf, (size_t)n);
					qlen += (size_t)n;
					query[qlen] = '\0';
					refilter = true;
				}
			}
			break;
		}

		if (refilter) {
			free(matches);
			matches = picker_filter(spec->items, spec->count, query,
			                        &mcount);
			sel = 0;
		}
	}

	free(matches);
	screen_invalidate(s); /* caller repaints its own view next */
	return result;
}
