#include "fuzzy.h"

#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

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

/* Reachability filter for fuzzy_best_match_in. A node is skipped when it is
 * hidden by H (dotfile or gitignored) or sits under a collapsed ignored dir (an
 * ancestor in the barrier set). Walks parent links + reads status/name only -
 * all immutable between submits - so the worker never touches a live flag. */
static bool fz_skip(const tree *t, uint32_t i, bool hide, const uint32_t *bar,
                    int nbar)
{
	const node *n = &t->nodes[i];
	if (hide && (n->name[0] == '.' || (n->status & ST_GITIGNORED)))
		return true;
	for (uint32_t a = n->parent; a != NODE_NIL && a != 0;
	     a = t->nodes[a].parent)
		for (int k = 0; k < nbar; k++)
			if (bar[k] == a)
				return true;
	return false;
}

uint32_t fuzzy_best_match_in(const tree *t, const char *query, bool hide,
                             const uint32_t *bar, int nbar)
{
	if (query[0] == '\0')
		return NODE_NIL;

	char pat[256];
	ascii_lower(query, pat, sizeof(pat));

	int best = SCORE_NONE;
	uint32_t best_idx = NODE_NIL;

	/* Pass 1: basenames (node index 0 is the synthetic root - skip it). */
	for (uint32_t i = 1; i < t->len; i++) {
		if (fz_skip(t, i, hide, bar, nbar))
			continue;
		int s = fuzzy_score(pat, t->nodes[i].name_lower);
		if (s > best) {
			best = s;
			best_idx = i;
		}
	}
	if (best >= SCORE_PREFIX)
		return best_idx; /* strong basename hit wins outright */

	/* Pass 2: full paths, but only matches that clear SCORE_PATH_MIN, so a
	 * stray cross-directory subsequence (e.g. "fll" inside
	 * ".github/workflows/ci.yml") cannot win when no filename matches. */
	for (uint32_t i = 1; i < t->len; i++) {
		if (fz_skip(t, i, hide, bar, nbar))
			continue;
		int s = fuzzy_score(pat, t->nodes[i].path_lower);
		if (s >= SCORE_PATH_MIN && s > best) {
			best = s;
			best_idx = i;
		}
	}

	return best > SCORE_NONE ? best_idx : NODE_NIL;
}

uint32_t fuzzy_best_match(const tree *t, const char *query)
{
	return fuzzy_best_match_in(t, query, false, NULL, 0);
}

/* ---- background engine --------------------------------------------------- */

static void *worker_main(void *arg)
{
	fuzzy_engine *e = arg;

	pthread_mutex_lock(&e->mu);
	for (;;) {
		while (!e->quit && (e->paused || e->generation == e->processed))
			pthread_cond_wait(&e->cv, &e->mu);
		if (e->quit)
			break;

		uint64_t g = e->generation;
		e->processed = g;
		char q[sizeof(e->query)];
		memcpy(q, e->query, sizeof(q));
		bool hide = e->hide;
		int nbar = e->barrier_n;
		uint32_t bar[FZ_BARRIER_MAX];
		memcpy(bar, e->barriers, (size_t)nbar * sizeof(bar[0]));
		const tree *arena = e->arena;
		e->busy = true;
		pthread_mutex_unlock(&e->mu);

		/* Score off-lock: the arena (and this generation's query, hide
		 * flag, and barrier snapshot) is immutable for this generation.
		 */
		uint32_t best =
		    arena ? fuzzy_best_match_in(arena, q, hide, bar, nbar)
		          : NODE_NIL;

		pthread_mutex_lock(&e->mu);
		e->busy = false;
		pthread_cond_signal(&e->idle_cv); /* a pauser may be waiting */
		/* Skip posting if a refresh paused us (arena being freed) or a
		 * newer query arrived (rescore it on the next loop). */
		if (!e->quit && !e->paused && g == e->generation) {
			e->result_gen = g;
			e->result_node = best;
			e->result_ready = true;
			char b = 1;
			if (write(e->wake_w, &b, 1) < 0)
				(void)0; /* pipe full is fine: a wake is a wake
				          */
		}
	}
	pthread_mutex_unlock(&e->mu);
	return NULL;
}

bool fuzzy_engine_start(fuzzy_engine *e)
{
	e->arena = NULL;
	e->query[0] = '\0';
	e->hide = false;
	e->barrier_n = 0;
	e->generation = 0;
	e->processed = 0;
	e->quit = false;
	e->paused = false;
	e->busy = false;
	e->result_gen = 0;
	e->result_node = NODE_NIL;
	e->result_ready = false;
	e->started = false;

	int fds[2];
	if (pipe(fds) != 0)
		return false;
	e->wake_r = fds[0];
	e->wake_w = fds[1];
	/* Non-blocking so the drain loop and a full pipe never block. */
	fcntl(e->wake_r, F_SETFL, O_NONBLOCK);
	fcntl(e->wake_w, F_SETFL, O_NONBLOCK);

	if (pthread_mutex_init(&e->mu, NULL) != 0)
		goto fail_pipe;
	if (pthread_cond_init(&e->cv, NULL) != 0)
		goto fail_mu;
	if (pthread_cond_init(&e->idle_cv, NULL) != 0)
		goto fail_cv;
	if (pthread_create(&e->thread, NULL, worker_main, e) != 0)
		goto fail_idle;

	e->started = true;
	return true;

fail_idle:
	pthread_cond_destroy(&e->idle_cv);
fail_cv:
	pthread_cond_destroy(&e->cv);
fail_mu:
	pthread_mutex_destroy(&e->mu);
fail_pipe:
	close(e->wake_r);
	close(e->wake_w);
	return false;
}

void fuzzy_engine_stop(fuzzy_engine *e)
{
	if (!e->started)
		return;
	pthread_mutex_lock(&e->mu);
	e->quit = true;
	pthread_cond_signal(&e->cv);
	pthread_mutex_unlock(&e->mu);

	pthread_join(e->thread, NULL);
	pthread_cond_destroy(&e->idle_cv);
	pthread_cond_destroy(&e->cv);
	pthread_mutex_destroy(&e->mu);
	close(e->wake_r);
	close(e->wake_w);
	e->started = false;
}

int fuzzy_engine_wake_fd(const fuzzy_engine *e)
{
	return e->wake_r;
}

void fuzzy_submit_in(fuzzy_engine *e, const tree *arena, const char *query,
                     bool hide, const uint32_t *barriers, int nbar)
{
	if (nbar > FZ_BARRIER_MAX)
		nbar =
		    FZ_BARRIER_MAX; /* excess stay searchable, not overflow */
	pthread_mutex_lock(&e->mu);
	e->arena = arena;
	strncpy(e->query, query, sizeof(e->query) - 1);
	e->query[sizeof(e->query) - 1] = '\0';
	e->hide = hide;
	e->barrier_n = nbar;
	if (nbar > 0)
		memcpy(e->barriers, barriers,
		       (size_t)nbar * sizeof(e->barriers[0]));
	e->generation++;
	pthread_cond_signal(&e->cv);
	pthread_mutex_unlock(&e->mu);
}

void fuzzy_submit(fuzzy_engine *e, const tree *arena, const char *query)
{
	fuzzy_submit_in(e, arena, query, false, NULL, 0);
}

bool fuzzy_engine_take(fuzzy_engine *e, uint32_t *node)
{
	char drain[64];
	while (read(e->wake_r, drain, sizeof(drain)) > 0)
		; /* clear the wake pipe */

	pthread_mutex_lock(&e->mu);
	bool have = e->result_ready && e->result_gen == e->generation;
	if (have) {
		*node = e->result_node;
		e->result_ready = false;
	}
	pthread_mutex_unlock(&e->mu);
	return have;
}

void fuzzy_engine_pause(fuzzy_engine *e)
{
	if (!e->started)
		return;
	pthread_mutex_lock(&e->mu);
	e->paused = true;
	while (e->busy) /* wait out any in-flight scan of the old arena */
		pthread_cond_wait(&e->idle_cv, &e->mu);
	e->result_ready = false; /* its node indices are about to be stale */
	pthread_mutex_unlock(&e->mu);
}

void fuzzy_engine_resume(fuzzy_engine *e)
{
	if (!e->started)
		return;
	pthread_mutex_lock(&e->mu);
	e->paused = false;
	pthread_cond_signal(&e->cv);
	pthread_mutex_unlock(&e->mu);
}
