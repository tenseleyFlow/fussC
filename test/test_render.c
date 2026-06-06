#include "flatten.h"
#include "render.h"
#include "test.h"
#include "tree.h"

#include <stdlib.h>
#include <string.h>

void test_render_golden(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, ".gitignore", ST_UNTRACKED);
	tree_add(&t, "Makefile", ST_STAGED);
	tree_add(&t, "README.md", ST_UNSTAGED);
	tree_add(&t, "src/main.c", ST_UNSTAGED);
	tree_add(&t, "src/util.c", ST_STAGED);

	flat_list f;
	flat_init(&f);
	flatten(&f, &t);

	char *out = render_tree_string(&t, &f, false);
	const char *want =
	    ".\n"
	    "\342\224\234\342\224\200\342\224\200 .gitignore \342\234\227\n"
	    "\342\224\234\342\224\200\342\224\200 Makefile \342\206\221\n"
	    "\342\224\234\342\224\200\342\224\200 README.md \342\234\227\n"
	    "\342\224\224\342\224\200\342\224\200 src\n"
	    "    \342\224\234\342\224\200\342\224\200 main.c \342\234\227\n"
	    "    \342\224\224\342\224\200\342\224\200 util.c \342\206\221\n";
	CHECK_STR_EQ(out, want);
	free(out);

	flat_free(&f);
	tree_free(&t);
}

/* Exercises the "|   " continuation gutter under a non-last directory. */
void test_render_pipe_gutter(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, "aaa/x.c", ST_UNSTAGED);
	tree_add(&t, "zzz.txt", ST_STAGED);

	flat_list f;
	flat_init(&f);
	flatten(&f, &t);

	char *out = render_tree_string(&t, &f, false);
	const char *want =
	    ".\n"
	    "\342\224\234\342\224\200\342\224\200 aaa\n"
	    "\342\224\202   \342\224\224\342\224\200\342\224\200 x.c "
	    "\342\234\227\n"
	    "\342\224\224\342\224\200\342\224\200 zzz.txt \342\206\221\n";
	CHECK_STR_EQ(out, want);
	free(out);

	flat_free(&f);
	tree_free(&t);
}

void test_render_color(void)
{
	tree t;
	tree_init(&t);
	tree_add(&t, "staged", ST_STAGED);
	tree_add(&t, "ignored", ST_GITIGNORED);

	flat_list f;
	flat_init(&f);
	flatten(&f, &t);

	char *out = render_tree_string(&t, &f, true);
	/* Green for staged, dim wrap for the ignored name. */
	CHECK(strstr(out, "\033[32m \342\206\221\033[0m") != NULL);
	CHECK(strstr(out, "\033[90mignored\033[0m") != NULL);
	free(out);

	/* With color off there are no escape sequences at all. */
	char *plain = render_tree_string(&t, &f, false);
	CHECK(strchr(plain, '\033') == NULL);
	free(plain);

	flat_free(&f);
	tree_free(&t);
}
