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

/* Clip `raw` to `width` display columns, then pad with spaces to exactly that
 * width (SGR escapes are kept and count as zero width). Caller frees. */
static char *pad_clip(const char *raw, int width)
{
	char *clipped = clip_to_width(raw, width);
	int w = (int)display_width(clipped);
	strbuf s = {0};
	sb_put(&s, clipped);
	for (int k = w; k < width; k++)
		sb_putc(&s, ' ');
	free(clipped);
	return s.buf ? s.buf : xstrdup("");
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
	int body_h = list_bottom - list_top;
	if (body_h < 0)
		body_h = 0;

	/* Split into list | preview when a preview exists and there is room. */
	bool split = v->preview_lines != NULL && cols >= 40 && body_h > 0;
	int left_w = split ? cols * 2 / 5 : cols;
	if (left_w < 1)
		left_w = 1;
	int right_w = split ? cols - left_w - 1 : 0;

	int start = 0;
	if (v->match_count > body_h) {
		start = v->sel - body_h / 2;
		if (start < 0)
			start = 0;
		if (start > v->match_count - body_h)
			start = v->match_count - body_h;
	}

	for (int r = 0; r < body_h; r++) {
		int mi = start + r;
		int row = list_top + r;
		strbuf s = {0};

		/* Left: the match (highlighted) or a blank, padded to left_w.
		 */
		if (mi < v->match_count) {
			const char *text = v->items[v->matches[mi]];
			if (text == NULL)
				text = "";
			if (mi == v->sel && color) {
				char *cell = pad_clip(text, left_w);
				sb_put(&s, "\033[7m");
				sb_put(&s, cell);
				sb_put(&s, "\033[0m");
				free(cell);
			} else if (mi == v->sel) {
				strbuf c = {0};
				sb_put(&c, "> ");
				sb_put(&c, text);
				char *cell =
				    pad_clip(c.buf ? c.buf : "> ", left_w);
				sb_put(&s, cell);
				free(cell);
				free(c.buf);
			} else {
				char *cell = pad_clip(text, left_w);
				sb_put(&s, cell);
				free(cell);
			}
		} else if (v->match_count == 0 && r == 0) {
			const char *nm = color ? "\033[90m  (no matches)\033[0m"
			                       : "  (no matches)";
			char *cell = pad_clip(nm, left_w);
			sb_put(&s, cell);
			free(cell);
		} else {
			char *cell = pad_clip("", left_w);
			sb_put(&s, cell);
			free(cell);
		}

		/* Right: separator + the preview line for this body row. */
		if (split) {
			sb_put(&s, color ? "\033[90m\342\224\202\033[0m"
			                 : "\342\224\202");
			const char *pl =
			    (r < v->preview_count) ? v->preview_lines[r] : "";
			char *pc = clip_to_width_off(pl ? pl : "",
			                             v->preview_col, right_w);
			sb_put(&s, pc);
			free(pc);
		}

		set_line(lines, row, s.buf ? s.buf : "", cols);
		free(s.buf);
	}

	if (hint_rows) {
		strbuf s = {0};
		if (color)
			sb_put(&s, "\033[90m");
		sb_put(&s, "\342\206\221\342\206\223 move  Enter select  "
		           "Esc cancel");
		if (split)
			sb_put(&s, "  \342\206\220\342\206\222 scroll");
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

/* Split `text` into heap line strings on '\n' (a trailing '\r' is trimmed).
 * *n_out gets the count; caller frees each line and the array. */
static char **split_lines(const char *text, int *n_out)
{
	char **lines = NULL;
	int n = 0, cap = 0;
	const char *p = text;
	while (*p != '\0') {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		if (len > 0 && p[len - 1] == '\r')
			len--;
		if (n == cap) {
			cap = cap ? cap * 2 : 16;
			lines = xrealloc(lines, (size_t)cap * sizeof(*lines));
		}
		char *line = xmalloc(len + 1);
		memcpy(line, p, len);
		line[len] = '\0';
		lines[n++] = line;
		if (!nl)
			break;
		p = nl + 1;
	}
	*n_out = n;
	return lines;
}

static void free_lines(char **lines, int n)
{
	for (int i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
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

	/* Preview is recomputed only when the selected item changes. */
	int preview_item = -1;
	char **pv = NULL;
	int pv_count = 0;
	int pv_maxw = 0;     /* widest preview line, for the scroll clamp */
	int preview_col = 0; /* horizontal scroll offset */
	const int HSTEP = 8; /* columns shifted per Left/Right press */

	while (running) {
		int cur_item = mcount > 0 ? matches[sel] : -1;
		if (spec->preview && cur_item != preview_item) {
			free_lines(pv, pv_count);
			pv = NULL;
			pv_count = 0;
			pv_maxw = 0;
			preview_col =
			    0; /* new preview: back to the left edge */
			if (cur_item >= 0) {
				char *txt =
				    spec->preview(spec->preview_ctx, cur_item);
				if (txt) {
					pv = split_lines(txt, &pv_count);
					free(txt);
					for (int i = 0; i < pv_count; i++) {
						int w =
						    (int)display_width(pv[i]);
						if (w > pv_maxw)
							pv_maxw = w;
					}
				}
			}
			preview_item = cur_item;
		}

		int rows, cols;
		term_size(&rows, &cols);
		picker_view v = {.title = spec->title,
		                 .items = spec->items,
		                 .matches = matches,
		                 .match_count = mcount,
		                 .total = spec->count,
		                 .sel = sel,
		                 .query = query,
		                 .preview_lines = pv,
		                 .preview_count = pv_count,
		                 .preview_col = preview_col};
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
		case KEY_LEFT:
			preview_col -= HSTEP;
			if (preview_col < 0)
				preview_col = 0;
			break;
		case KEY_RIGHT:
			/* Clamp so the widest line can't scroll fully off. */
			if (preview_col + HSTEP < pv_maxw)
				preview_col += HSTEP;
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

	free_lines(pv, pv_count);
	free(matches);
	screen_invalidate(s); /* caller repaints its own view next */
	return result;
}
