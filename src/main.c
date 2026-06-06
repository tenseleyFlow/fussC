#include "fussy.h"
#include "term.h"

#include <stdio.h>
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
			/* parsed in a later sprint */
			continue;
		}
		fprintf(stderr, "%s: unknown option '%s'\n", FUSSY_NAME, a);
		print_usage(stderr);
		return 2;
	}

	if (want_print || !isatty(STDIN_FILENO)) {
		/* Non-interactive tree output lands in the tree-core sprint. */
		printf("%s %s: print mode not yet implemented\n", FUSSY_NAME,
		       FUSSY_VERSION);
		return 0;
	}

	return run_interactive();
}
