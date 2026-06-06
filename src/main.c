#include "flatten.h"
#include "fussy.h"
#include "git.h"
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
	flatten(&f, &t);

	bool color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
	render_tree(stdout, &t, &f, color);

	flat_free(&f);
	tree_free(&t);
	git_close(&g);
	return 0;
}

/* Placeholder interactive screen. The real renderer and event loop arrive in
 * the navigation sprint; for now this proves clean terminal enter/exit. */
static int run_interactive(void)
{
	if (!term_init()) {
		fprintf(stderr, "%s: not a terminal (try --print)\n",
		        FUSSY_NAME);
		return 1;
	}

	printf("\033[2J\033[H"); /* clear + home */
	printf("%s %s\r\n", FUSSY_NAME, FUSSY_VERSION);
	printf("scaffold build - press any key to exit\r\n");
	fflush(stdout);

	term_read_byte();
	term_restore();
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

	return run_interactive();
}
