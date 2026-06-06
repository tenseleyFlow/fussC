#ifndef FUSSY_FUZZY_H
#define FUSSY_FUZZY_H

#include <stdint.h>

#include "tree.h"

/*
 * fzf-style subsequence scorer and an index-all-nodes best-match search. The
 * scorer is a pure function over already-lowercased strings; the search scans
 * every arena node (not just the visible list), which is what lets fussy jump
 * into collapsed directories - the bug both originals shared.
 */

/* Score tiers and weights (named, no magic numbers). */
enum {
	SCORE_NONE = 0,
	SCORE_EXACT = 10000,
	SCORE_PREFIX = 5000,
	FZ_CHAR = 100,     /* per matched character */
	FZ_CONSEC = 50,    /* escalating consecutive-run bonus */
	FZ_START = 200,    /* match at index 0 */
	FZ_BOUNDARY = 150, /* match immediately after a separator */
	FZ_GAP = 1,        /* penalty per skipped char once matching began */
};

/*
 * Score `pat` against `text`, both ASCII-lowercased. Returns SCORE_NONE when
 * `pat` is not a subsequence of `text` (or is empty). Higher is better:
 * exact > prefix > word-boundary/consecutive hits > scattered.
 */
int fuzzy_score(const char *pat, const char *text);

/*
 * Best-matching node for `query` over the whole arena: basename pass first,
 * then a full-path pass if no strong basename hit. Returns the node index, or
 * NODE_NIL for an empty query or no match. `query` is lowercased internally.
 */
uint32_t fuzzy_best_match(const tree *t, const char *query);

#endif /* FUSSY_FUZZY_H */
