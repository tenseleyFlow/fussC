#ifndef FUSSY_FUZZY_H
#define FUSSY_FUZZY_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "tree.h"

/* Cap on the barrier set (collapsed ignored dirs whose subtrees are excluded
 * from the search). A realistic view has a handful; the excess past this just
 * stay searchable (correct, slightly slower) rather than overflow. */
#define FZ_BARRIER_MAX 256

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
	/* Quality floor a full-path (cross-directory) fallback match must clear
	 * to win, so a stray subsequence scattered across directory names does
	 * not yank the selection away when no filename actually matches. */
	SCORE_PATH_MIN = 500,
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

/*
 * Like fuzzy_best_match but with the view's reachability rules applied, so a
 * fuzzy-jump only lands on something the user could see by expanding non-
 * ignored directories:
 *   - `hide` (the H toggle): skip dotfiles and gitignored paths entirely.
 *   - `barriers`/`nbar`: node indices of collapsed *ignored* directories;
 *     anything under one is skipped (a big collapsed node_modules is never
 *     scored). Non-ignored collapsed dirs are still searched (auto-expand).
 * The caller snapshots `barriers` from the live tree, so the worker reads only
 * data that is immutable between submits (no expansion-flag races).
 */
uint32_t fuzzy_best_match_in(const tree *t, const char *query, bool hide,
                             const uint32_t *barriers, int nbar);

/*
 * Background scoring engine. A persistent worker thread scores the arena off
 * the main thread so input never blocks on a huge tree. The query/result
 * handoff is guarded by a mutex+condvar with a generation counter (stale
 * results are dropped); the worker writes a byte to a self-pipe when a result
 * is ready so the main loop's poll() wakes. The arena is immutable between
 * submits, so the worker scores it without holding the lock.
 */
typedef struct {
	pthread_t thread;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	pthread_cond_t idle_cv; /* signalled when the worker finishes a scan */

	/* guarded by mu */
	const tree *arena;
	char query[256];
	bool hide; /* H toggle: skip dotfiles + ignored */
	uint32_t barriers[FZ_BARRIER_MAX]; /* collapsed ignored dirs to prune */
	int barrier_n;
	uint64_t generation; /* bumped on each submit */
	uint64_t processed;  /* generation the worker last began scoring */
	bool quit;
	bool paused; /* refresh in progress: the worker must not scan */
	bool busy;   /* the worker is mid-scan right now */
	uint64_t result_gen;
	uint32_t result_node;
	bool result_ready;

	int wake_r; /* main polls this end */
	int wake_w; /* worker writes this end */
	bool started;
} fuzzy_engine;

bool fuzzy_engine_start(fuzzy_engine *e);
void fuzzy_engine_stop(fuzzy_engine *e);
int fuzzy_engine_wake_fd(const fuzzy_engine *e);

/* Publish a new query against `arena` (bumps the generation, wakes the worker).
 */
void fuzzy_submit(fuzzy_engine *e, const tree *arena, const char *query);

/* As fuzzy_submit, but carrying the view's reachability snapshot (see
 * fuzzy_best_match_in): `hide` and the barrier set are copied under the lock so
 * the worker scores with them race-free. */
void fuzzy_submit_in(fuzzy_engine *e, const tree *arena, const char *query,
                     bool hide, const uint32_t *barriers, int nbar);

/* Drain the wake pipe and, if the latest generation's result is ready, store it
 * in *node and return true. Returns false when no fresh result is pending. */
bool fuzzy_engine_take(fuzzy_engine *e, uint32_t *node);

/*
 * Quiesce the worker so the arena can be freed and rebuilt safely: blocks until
 * any in-flight scan finishes, prevents new scans, and drops any pending result
 * (its node indices would be stale after the rebuild). Pair with resume.
 */
void fuzzy_engine_pause(fuzzy_engine *e);
void fuzzy_engine_resume(fuzzy_engine *e);

#endif /* FUSSY_FUZZY_H */
