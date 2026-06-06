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

/* Everything the loop and dispatch touch. */
typedef struct {
	app *a;
	git_ctx *g;
	fuzzy_engine *eng;
	bool have_repo;
	bool all;
	bool running;
} loopctx;

static void set_status(app *a, const char *msg)
{
	snprintf(a->status, sizeof(a->status), "%s", msg);
}

static const char *sel_path(app *a)
{
	uint32_t n = app_selected_node(a);
	return n == NODE_NIL ? NULL : a->t.nodes[n].path;
}

static bool sel_untracked(app *a)
{
	uint32_t n = app_selected_node(a);
	if (n == NODE_NIL)
		return false;
	file_status s = a->t.nodes[n].status;
	return (s & ST_UNTRACKED) && !(s & ST_STAGED);
}

/* Rebuild the tree from git after a mutation, keeping the user's collapse state
 * and selection, and keeping the fuzzy worker safe across the free/rebuild. */
static void do_refresh(loopctx *L)
{
	app *a = L->a;
	uint32_t ncol = 0;
	char **collapsed = app_collapsed_paths(a, &ncol);
	char *selpath = app_selected_path_dup(a);

	if (L->eng)
		fuzzy_engine_pause(L->eng);
	tree_free(&a->t);
	tree_init(&a->t);
	if (L->have_repo)
		git_load_tree(L->g, &a->t, L->all);
	app_collapse_paths(a, collapsed, ncol);
	if (L->eng)
		fuzzy_engine_resume(L->eng);

	flatten(&a->visible, &a->t, a->hide_dotfiles);
	app_select_path(a, selpath);

	for (uint32_t i = 0; i < ncol; i++)
		free(collapsed[i]);
	free(collapsed);
	free(selpath);

	if (L->eng && a->filter_len > 0)
		fuzzy_submit(L->eng, &a->t, a->filter);
}

/* Run an immediate op result: refresh + ok message, or surface the error. */
static void op_result(loopctx *L, int rc, const char *err, const char *ok)
{
	if (rc != 0) {
		set_status(L->a, err);
	} else {
		do_refresh(L);
		set_status(L->a, ok);
	}
}

/* UPPERCASE command from the main view: immediate ops run now; the rest open a
 * modal overlay that executes on confirm. */
static void run_command(loopctx *L, uint32_t letter)
{
	app *a = L->a;
	if (!L->have_repo)
		return;
	char err[256];
	const char *p = sel_path(a);

	switch (letter) {
	case 'A':
		if (p)
			op_result(L, gitop_stage(L->g, p, err, sizeof(err)),
			          err, "staged");
		break;
	case 'S':
		op_result(L, gitop_stage_all(L->g, err, sizeof(err)), err,
		          "staged all");
		break;
	case 'U':
		if (p)
			op_result(L, gitop_unstage(L->g, p, err, sizeof(err)),
			          err, "unstaged");
		break;
	case 'Z':
		op_result(L, gitop_unstage_all(L->g, err, sizeof(err)), err,
		          "unstaged all");
		break;
	case 'C':
		overlay_open_commit(&a->ov, false, NULL);
		break;
	case 'M': {
		char *m = gitop_last_message(L->g);
		overlay_open_commit(&a->ov, true, m);
		free(m);
		break;
	}
	case 'T':
		overlay_open_tag(&a->ov);
		break;
	case 'R':
		if (p)
			overlay_open_rename(&a->ov, p);
		break;
	case 'X':
		if (p) {
			overlay_open_confirm(&a->ov, "Discard changes? (y/n)");
			a->ov.confirm_op = CONF_DISCARD;
			snprintf(a->ov.target, sizeof(a->ov.target), "%s", p);
			a->ov.target_untracked = sel_untracked(a);
		}
		break;
	case 'D':
		if (p) {
			overlay_open_confirm(&a->ov, "Delete file? (y/n)");
			a->ov.confirm_op = CONF_DELETE;
			snprintf(a->ov.target, sizeof(a->ov.target), "%s", p);
			a->ov.target_untracked = sel_untracked(a);
		}
		break;
	default:
		break;
	}
}

static void apply_action(loopctx *L, action act)
{
	app *a = L->a;
	a->status[0] = '\0'; /* a fresh keystroke clears the last message */

	switch (act.kind) {
	case ACT_QUIT:
		L->running = false;
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
		if (L->eng)
			fuzzy_submit(L->eng, &a->t, a->filter);
		else
			app_apply_match(a, fuzzy_best_match(&a->t, a->filter));
		break;
	case ACT_FILTER_BACKSPACE:
		app_filter_age(a, mono_ns());
		app_filter_backspace(a);
		if (a->filter_len > 0) {
			if (L->eng)
				fuzzy_submit(L->eng, &a->t, a->filter);
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
	case ACT_COMMAND:
		run_command(L, act.cp);
		break;
	case ACT_REDRAW:
	case ACT_NONE:
		break;
	}
}

/* Execute the active overlay's git op (Enter on a text field, y on a confirm).
 * On success the overlay closes and the tree refreshes; on error it stays up
 * with the message in the footer. */
static void overlay_execute(loopctx *L)
{
	app *a = L->a;
	overlay *o = &a->ov;
	char err[256];

	switch (o->kind) {
	case OV_COMMIT:
		if (o->len == 0)
			return;
		if ((o->amend ? gitop_amend(L->g, o->text, err, sizeof(err))
		              : gitop_commit(L->g, o->text, err,
		                             sizeof(err))) != 0) {
			set_status(a, err);
			return;
		}
		overlay_close(o);
		do_refresh(L);
		set_status(a, o->amend ? "amended" : "committed");
		break;
	case OV_TAG:
		if (o->len == 0)
			return;
		if (gitop_tag(L->g, o->text, NULL, err, sizeof(err)) != 0) {
			set_status(a, err);
			return;
		}
		overlay_close(o);
		do_refresh(L);
		set_status(a, "tagged");
		break;
	case OV_RENAME:
		if (o->len == 0 || strcmp(o->text, o->target) == 0) {
			overlay_close(o);
			return;
		}
		if (gitop_rename(L->g, o->target, o->text, err, sizeof(err)) !=
		    0) {
			set_status(a, err);
			return;
		}
		overlay_close(o);
		do_refresh(L);
		set_status(a, "renamed");
		break;
	case OV_CONFIRM:
		if (o->confirm_op == CONF_DISCARD)
			op_result(L,
			          gitop_discard(L->g, o->target,
			                        o->target_untracked, err,
			                        sizeof(err)),
			          err, "discarded");
		else
			op_result(L,
			          gitop_delete(L->g, o->target,
			                       o->target_untracked, err,
			                       sizeof(err)),
			          err, "deleted");
		overlay_close(o);
		break;
	default:
		overlay_close(o);
		break;
	}
}

static void overlay_key(loopctx *L, int key)
{
	overlay *o = &L->a->ov;

	if (o->kind == OV_CONFIRM) {
		if (key == 'y' || key == 'Y')
			overlay_execute(L);
		else if (key == 'n' || key == 'N' || key == KEY_ESC)
			overlay_close(o);
		return;
	}

	switch (key) {
	case KEY_ESC:
		overlay_close(o);
		break;
	case KEY_ENTER:
		overlay_execute(L);
		break;
	case KEY_BACKSPACE:
		overlay_backspace(o);
		break;
	case KEY_LEFT:
		overlay_left(o);
		break;
	case KEY_RIGHT:
		overlay_right(o);
		break;
	default:
		if (key >= 0x20 && key < KEY_SPECIAL_BASE)
			overlay_insert(o, (uint32_t)key);
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

	loopctx L = {.a = &a,
	             .g = &g,
	             .eng = have_engine ? &engine : NULL,
	             .have_repo = have_repo,
	             .all = all,
	             .running = true};

	struct pollfd fds[2];
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;
	fds[1].fd = have_engine ? fuzzy_engine_wake_fd(&engine) : -1;
	fds[1].events = POLLIN;
	nfds_t nfds = have_engine ? 2 : 1;

	while (L.running) {
		screen_draw(&s, &a, repo, branch, color);

		bool ov = overlay_active(&a.ov);

		/* Only the filter (main view) arms the idle timeout. */
		int timeout = -1;
		if (!ov && a.filter_len > 0) {
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
				term_take_resize();
				continue;
			}
			break;
		}
		if (pr == 0) {
			if (app_filter_expired(&a, mono_ns()))
				app_filter_clear(&a);
			continue;
		}

		/* Fuzzy results apply only in the main view; while an overlay
		 * is up, drain the wake pipe and ignore them. */
		if (have_engine && (fds[1].revents & POLLIN)) {
			uint32_t node;
			if (fuzzy_engine_take(&engine, &node) && !ov)
				app_apply_match(&a, node);
		}

		if (fds[0].revents != 0) {
			int key = term_read_key();
			if (overlay_active(&a.ov))
				overlay_key(&L, key);
			else
				apply_action(&L, input_classify(key));
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
