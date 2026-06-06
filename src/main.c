#include "app.h"
#include "flatten.h"
#include "fussy.h"
#include "fuzzy.h"
#include "git.h"
#include "input.h"
#include "render.h"
#include "term.h"
#include "tree.h"

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

static void apply_action(app *a, action act, bool *running)
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
		app_filter_push(a, act.cp);
		app_apply_match(a, fuzzy_best_match(&a->t, a->filter));
		break;
	case ACT_FILTER_BACKSPACE:
		app_filter_backspace(a);
		if (a->filter_len > 0)
			app_apply_match(a, fuzzy_best_match(&a->t, a->filter));
		else
			a->filter_nomatch = false;
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

	bool running = true;
	while (running) {
		screen_draw(&s, &a, repo, branch, color);
		int key = term_read_key();
		apply_action(&a, input_classify(key), &running);
	}

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
