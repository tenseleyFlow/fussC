#include "fuzzy.h"

#include <stdbool.h>
#include <string.h>

int fuzzy_score(const char *pat, const char *text)
{
	if (pat[0] == '\0')
		return SCORE_NONE;
	if (strcmp(pat, text) == 0)
		return SCORE_EXACT;

	size_t plen = strlen(pat);
	size_t tlen = strlen(text);
	if (plen < tlen && memcmp(text, pat, plen) == 0)
		return SCORE_PREFIX;

	int score = 0;
	size_t pi = 0;
	int consec = 0;
	bool is_consec = false;
	bool started = false;

	for (size_t ti = 0; text[ti] != '\0' && pat[pi] != '\0'; ti++) {
		if (text[ti] == pat[pi]) {
			started = true;
			score += FZ_CHAR;
			if (is_consec) {
				consec++;
				score += consec * FZ_CONSEC;
			} else {
				consec = 1;
				is_consec = true;
			}
			if (ti == 0) {
				score += FZ_START;
			} else {
				char prev = text[ti - 1];
				if (prev == '/' || prev == '_' || prev == '-' ||
				    prev == '.')
					score += FZ_BOUNDARY;
			}
			pi++;
		} else {
			is_consec = false;
			consec = 0;
			if (started)
				score -= FZ_GAP;
		}
	}

	if (pat[pi] != '\0')
		return SCORE_NONE; /* did not consume the whole pattern */

	score -= (int)tlen; /* prefer shorter matches */
	return score < 1 ? 1 : score;
}

/* Lowercase ASCII into `out` (bounded), leaving other bytes untouched. */
static void ascii_lower(const char *in, char *out, size_t cap)
{
	size_t i = 0;
	for (; in[i] != '\0' && i + 1 < cap; i++) {
		char c = in[i];
		out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
	}
	out[i] = '\0';
}

uint32_t fuzzy_best_match(const tree *t, const char *query)
{
	if (query[0] == '\0')
		return NODE_NIL;

	char pat[256];
	ascii_lower(query, pat, sizeof(pat));

	int best = SCORE_NONE;
	uint32_t best_idx = NODE_NIL;

	/* Pass 1: basenames (node index 0 is the synthetic root - skip it). */
	for (uint32_t i = 1; i < t->len; i++) {
		int s = fuzzy_score(pat, t->nodes[i].name_lower);
		if (s > best) {
			best = s;
			best_idx = i;
		}
	}
	if (best >= SCORE_PREFIX)
		return best_idx; /* strong basename hit wins outright */

	/* Pass 2: full paths. */
	for (uint32_t i = 1; i < t->len; i++) {
		int s = fuzzy_score(pat, t->nodes[i].path_lower);
		if (s > best) {
			best = s;
			best_idx = i;
		}
	}

	return best > SCORE_NONE ? best_idx : NODE_NIL;
}
