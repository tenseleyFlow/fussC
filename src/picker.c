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
		if (v->extra != NULL && v->extra[0] != '\0') {
			sb_put(&s, "  ");
			sb_put(&s, v->extra);
		}
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

picker_result picker_run(screen *s, const picker_spec *spec, bool color)
{
	screen_invalidate(s); /* we own the screen now: full paint */

	char query[256];
	size_t qlen = 0;
	query[0] = '\0';

	int mcount = 0;
	int *matches = picker_filter(spec->items, spec->count, query, &mcount);
	int sel = 0;
	picker_result result = {.index = -1, .key = KEY_ESC};
	bool running = true;

	/* Hint text for the spec's bindings, built once (e.g. "^N new  ^D
	 * del"). */
	strbuf extra = {0};
	for (int b = 0; b < spec->binding_count; b++) {
		if (b > 0)
			sb_put(&extra, "  ");
		sb_put(&extra, spec->bindings[b].label);
	}

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
			str_free_lines(pv, pv_count);
			pv = NULL;
			pv_count = 0;
			pv_maxw = 0;
			preview_col =
			    0; /* new preview: back to the left edge */
			if (cur_item >= 0) {
				char *txt =
				    spec->preview(spec->preview_ctx, cur_item);
				if (txt) {
					pv = str_split_lines(txt, &pv_count);
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
		                 .preview_col = preview_col,
		                 .extra = extra.buf};
		char **frame = render_picker_frame(&v, rows, cols, color);
		screen_present(s, frame, rows);

		int key = term_read_key();
		bool refilter = false;

		/* Browser bindings win over the filter (they use non-printable
		 * keys). Surface the key + selected item to the caller. */
		bool bound = false;
		for (int b = 0; b < spec->binding_count; b++) {
			if (key == spec->bindings[b].key) {
				result.index = mcount > 0 ? matches[sel] : -1;
				result.key = key;
				running = false;
				bound = true;
				break;
			}
		}
		if (bound)
			break;

		switch (key) {
		case KEY_EOF:
		case KEY_ESC:
			result.index = -1;
			result.key = key;
			running = false;
			break;
		case KEY_ENTER:
			result.index = mcount > 0 ? matches[sel] : -1;
			result.key = KEY_ENTER;
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

	str_free_lines(pv, pv_count);
	free(matches);
	free(extra.buf);
	screen_invalidate(s); /* caller repaints its own view next */
	return result;
}

bool prompt_line(screen *s, const char *title, char *out, size_t outsz,
                 bool color)
{
	screen_invalidate(s);

	char buf[256];
	size_t len = 0;
	buf[0] = '\0';
	bool accepted = false;
	bool running = true;

	while (running) {
		int rows, cols;
		term_size(&rows, &cols);

		char **lines =
		    xmalloc((size_t)(rows > 0 ? rows : 1) * sizeof(*lines));
		for (int i = 0; i < rows; i++)
			lines[i] = NULL;

		int prow = rows > 0 ? rows / 2 : 0;
		if (rows > 0) {
			strbuf ln = {0};
			if (color)
				sb_put(&ln, "\033[1m");
			sb_put(&ln, title ? title : "");
			if (color)
				sb_put(&ln, "\033[0m");
			sb_put(&ln, "> ");
			sb_put(&ln, buf);
			if (color)
				sb_put(&ln, "\033[7m \033[0m"); /* cursor */
			lines[prow] = clip_to_width(ln.buf ? ln.buf : "", cols);
			free(ln.buf);
		}
		for (int i = 0; i < rows; i++)
			if (lines[i] == NULL)
				lines[i] = xstrdup("");
		screen_present(s, lines, rows);

		int key = term_read_key();
		if (key == KEY_ENTER) {
			accepted = len > 0;
			running = false;
		} else if (key == KEY_ESC || key == KEY_EOF) {
			running = false;
		} else if (key == KEY_BACKSPACE) {
			if (len > 0) {
				size_t i = len;
				do {
					i--;
				} while (i > 0 && ((unsigned char)buf[i] &
				                   0xC0) == 0x80);
				buf[i] = '\0';
				len = i;
			}
		} else if (key >= 0x20 && key < KEY_SPECIAL_BASE) {
			char enc[4];
			int n = utf8_encode((uint32_t)key, enc);
			if (len + (size_t)n + 1 < sizeof(buf)) {
				memcpy(buf + len, enc, (size_t)n);
				len += (size_t)n;
				buf[len] = '\0';
			}
		}
	}

	if (accepted && outsz > 0)
		snprintf(out, outsz, "%s", buf);
	screen_invalidate(s);
	return accepted;
}

bool confirm_modal(screen *s, const char *prompt, bool color)
{
	screen_invalidate(s);

	bool result = false;
	bool running = true;
	while (running) {
		int rows, cols;
		term_size(&rows, &cols);

		char **lines =
		    xmalloc((size_t)(rows > 0 ? rows : 1) * sizeof(*lines));
		for (int i = 0; i < rows; i++)
			lines[i] = NULL;
		if (rows > 0) {
			strbuf ln = {0};
			if (color)
				sb_put(&ln, "\033[1m");
			sb_put(&ln, prompt ? prompt : "");
			sb_put(&ln, "  (y/n)");
			if (color)
				sb_put(&ln, "\033[0m");
			lines[rows / 2] =
			    clip_to_width(ln.buf ? ln.buf : "", cols);
			free(ln.buf);
		}
		for (int i = 0; i < rows; i++)
			if (lines[i] == NULL)
				lines[i] = xstrdup("");
		screen_present(s, lines, rows);

		int key = term_read_key();
		if (key == 'y' || key == 'Y') {
			result = true;
			running = false;
		} else if (key == 'n' || key == 'N' || key == KEY_ESC ||
		           key == KEY_EOF) {
			result = false;
			running = false;
		}
	}

	screen_invalidate(s);
	return result;
}
