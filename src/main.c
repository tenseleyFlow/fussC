#include "app.h"
#include "flatten.h"
#include "fussy.h"
#include "fuzzy.h"
#include "git.h"
#include "input.h"
#include "render.h"
#include "term.h"
#include "tree.h"
#include "util.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(FILE *out)
{
	fprintf(
	    out,
	    "usage: %s [options]\n"
	    "\n"
	    "  -a, --all      include all tracked files, not just dirty ones\n"
	    "  -p, --print    non-interactive tree output\n"
	    "  -h, --help     show this help and exit\n"
	    "  -V, --version  show version and exit\n",
	    FUSSY_NAME);
}

/* Non-interactive tree output. */
static int run_print(bool all)
{
	git_ctx g;
	char err[256];
	if (!git_open(&g, err, sizeof(err))) {
		fprintf(stderr, "%s: %s\n", FUSSY_NAME, err);
		return 1;
	}

	tree t;
	tree_init(&t);
	if (git_load_tree(&g, &t, all) != 0) {
		fprintf(stderr, "%s: failed to read git status\n", FUSSY_NAME);
		tree_free(&t);
		git_close(&g);
		return 1;
	}

	flat_list f;
	flat_init(&f);
	flatten(&f, &t, false);

	bool color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
	render_tree(stdout, &t, &f, color);

	flat_free(&f);
	tree_free(&t);
	git_close(&g);
	return 0;
}

static void apply_action(app *a, action act, bool *running, fuzzy_engine *eng)
{
	switch (act.kind) {
	case ACT_QUIT:
		*running = false;
		break;
	case ACT_UP:
		app_up(a);
		break;
	case ACT_DOWN:
		app_down(a);
		break;
	case ACT_LEFT:
		app_left(a);
		break;
	case ACT_RIGHT:
		app_right(a);
		break;
	case ACT_TOGGLE:
		app_toggle(a);
		break;
	case ACT_HOME:
		app_home(a);
		break;
	case ACT_END:
		app_end(a);
		break;
	case ACT_FILTER_PUSH:
		app_filter_age(a, mono_ns()); /* reset if idle, then append */
		app_filter_push(a, act.cp);
		if (eng)
			fuzzy_submit(eng, &a->t, a->filter);
		else
			app_apply_match(a, fuzzy_best_match(&a->t, a->filter));
		break;
	case ACT_FILTER_BACKSPACE:
		app_filter_age(a, mono_ns());
		app_filter_backspace(a);
		if (a->filter_len > 0) {
			if (eng)
				fuzzy_submit(eng, &a->t, a->filter);
			else
				app_apply_match(
				    a, fuzzy_best_match(&a->t, a->filter));
		}
		break;
	case ACT_FILTER_CLEAR:
		app_filter_clear(a);
		break;
	case ACT_TOGGLE_DOTFILES:
		app_toggle_dotfiles(a);
		break;
	case ACT_COMMAND: /* git commands land in Sprint 4 */
	case ACT_REDRAW:  /* the loop redraws every iteration */
	case ACT_NONE:
		break;
	}
}

/* Interactive tree: build state from git, then loop on keys with an
 * incrementally diffed redraw between each. */
static int run_interactive(bool all)
{
	app a;
	app_init(&a);

	git_ctx g;
	char err[256];
	bool have_repo = git_open(&g, err, sizeof(err));
	if (have_repo)
		git_load_tree(&g, &a.t, all);
	app_reflatten(&a);

	const char *repo = have_repo ? g.repo_name : "(not a repo)";
	const char *branch = have_repo ? g.branch : "";

	if (!term_init()) {
		/* stdout is a tty but stdin is not, or raw mode failed. */
		render_tree(stdout, &a.t, &a.visible, false);
		app_free(&a);
		if (have_repo)
			git_close(&g);
		return 0;
	}

	bool color = getenv("NO_COLOR") == NULL;
	screen s;
	screen_init(&s);

	fuzzy_engine engine;
	bool have_engine = fuzzy_engine_start(&engine);
	fuzzy_engine *eng = have_engine ? &engine : NULL;

	struct pollfd fds[2];
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;
	fds[1].fd = have_engine ? fuzzy_engine_wake_fd(&engine) : -1;
	fds[1].events = POLLIN;
	nfds_t nfds = have_engine ? 2 : 1;

	bool running = true;
	while (running) {
		screen_draw(&s, &a, repo, branch, color);

		/* Block until input or a fuzzy result; while a filter is
		 * active, wake when it goes idle so we can clear and revert the
		 * footer. */
		int timeout = -1;
		if (a.filter_len > 0) {
			uint64_t gap = (uint64_t)FILTER_TIMEOUT_MS * 1000000u;
			uint64_t elapsed = mono_ns() - a.last_input_ns;
			timeout = elapsed >= gap
			              ? 0
			              : (int)((gap - elapsed) / 1000000u) + 1;
		}

		fds[0].revents = fds[1].revents = 0;
		int pr = poll(fds, nfds, timeout);
		if (pr < 0) {
			if (errno == EINTR) {
				term_take_resize(); /* redraw next iteration */
				continue;
			}
			break;
		}
		if (pr == 0) { /* idle timeout: drop the stale filter */
			if (app_filter_expired(&a, mono_ns()))
				app_filter_clear(&a);
			continue;
		}

		/* Apply a ready fuzzy result (expand to + select the match). */
		if (have_engine && (fds[1].revents & POLLIN)) {
			uint32_t node;
			if (fuzzy_engine_take(&engine, &node))
				app_apply_match(&a, node);
		}

		/* Handle a keypress. */
		if (fds[0].revents != 0) {
			int key = term_read_key();
			apply_action(&a, input_classify(key), &running, eng);
		}
	}

	if (have_engine)
		fuzzy_engine_stop(&engine);
	screen_free(&s);
	term_restore();
	app_free(&a);
	if (have_repo)
		git_close(&g);
	return 0;
}

int main(int argc, char **argv)
{
	bool want_print = false;
	bool want_all = false;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			print_usage(stdout);
			return 0;
		}
		if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
			printf("%s %s\n", FUSSY_NAME, FUSSY_VERSION);
			return 0;
		}
		if (strcmp(a, "-p") == 0 || strcmp(a, "--print") == 0) {
			want_print = true;
			continue;
		}
		if (strcmp(a, "-a") == 0 || strcmp(a, "--all") == 0) {
			want_all = true;
			continue;
		}
		fprintf(stderr, "%s: unknown option '%s'\n", FUSSY_NAME, a);
		print_usage(stderr);
		return 2;
	}

	if (want_print || !isatty(STDIN_FILENO))
		return run_print(want_all);

	return run_interactive(want_all);
}
