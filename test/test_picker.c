#include "picker.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static char *const ITEMS[] = {
    "src/render.c",
    "src/fuzzy.c",
    "src/flatten.c",
    "README.md",
    ".github/workflows/ci.yml",
};
static const int N = (int)(sizeof(ITEMS) / sizeof(*ITEMS));

void test_picker_filter_empty_query(void)
{
	/* Empty query matches everything in original order. */
	int n = -1;
	int *m = picker_filter(ITEMS, N, "", &n);
	CHECK(n == N);
	for (int i = 0; i < N; i++)
		CHECK(m[i] == i);
	free(m);

	/* NULL query behaves like empty. */
	int n2 = -1;
	int *m2 = picker_filter(ITEMS, N, NULL, &n2);
	CHECK(n2 == N);
	free(m2);
}

void test_picker_filter_subsequence(void)
{
	/* "fz" is a subsequence of src/fuzzy.c only. */
	int n = -1;
	int *m = picker_filter(ITEMS, N, "fz", &n);
	CHECK(n == 1);
	if (n == 1)
		CHECK_STR_EQ(ITEMS[m[0]], "src/fuzzy.c");
	free(m);

	/* Case-insensitive: "README" matches README.md. */
	n = -1;
	m = picker_filter(ITEMS, N, "readme", &n);
	CHECK(n == 1);
	if (n == 1)
		CHECK_STR_EQ(ITEMS[m[0]], "README.md");
	free(m);
}

void test_picker_filter_ranks_best_first(void)
{
	/* "render" hits src/render.c as a strong contiguous run; nothing else
	 * should outrank it, and it must be first. */
	int n = -1;
	int *m = picker_filter(ITEMS, N, "render", &n);
	CHECK(n >= 1);
	if (n >= 1)
		CHECK_STR_EQ(ITEMS[m[0]], "src/render.c");
	free(m);
}

void test_picker_filter_no_match(void)
{
	int n = -1;
	int *m = picker_filter(ITEMS, N, "zzzqqq", &n);
	CHECK(n == 0);
	free(m);
}

void test_render_picker_frame_layout(void)
{
	int matches[] = {0, 1, 2, 3, 4};
	picker_view v = {.title = "Commits",
	                 .items = ITEMS,
	                 .matches = matches,
	                 .match_count = N,
	                 .total = N,
	                 .sel = 1,
	                 .query = "s"};

	char **f = render_picker_frame(&v, 10, 60, false);
	/* row 0: title + counter; row 1: prompt with the query. */
	CHECK(strstr(f[0], "Commits") != NULL);
	CHECK(strstr(f[0], "5/5") != NULL);
	CHECK(strstr(f[1], "> s") != NULL);
	/* the selected (second) match is marked with a caret in no-color mode
	 */
	bool found_sel = false;
	for (int i = 2; i < 10; i++)
		if (strstr(f[i], "> src/fuzzy.c"))
			found_sel = true;
	CHECK(found_sel);
	free_frame(f, 10);
}

void test_render_picker_frame_no_matches(void)
{
	picker_view v = {.title = "Commits",
	                 .items = ITEMS,
	                 .matches = NULL,
	                 .match_count = 0,
	                 .total = N,
	                 .sel = 0,
	                 .query = "zzz"};

	char **f = render_picker_frame(&v, 8, 50, false);
	CHECK(strstr(f[0], "0/5") != NULL);
	bool none = false;
	for (int i = 0; i < 8; i++)
		if (strstr(f[i], "(no matches)"))
			none = true;
	CHECK(none);
	free_frame(f, 8);
}

void test_render_picker_frame_tiny(void)
{
	/* Degenerate sizes must not crash (ASan/UBSan give this teeth). */
	int matches[] = {0, 1, 2, 3, 4};
	picker_view v = {.title = "T",
	                 .items = ITEMS,
	                 .matches = matches,
	                 .match_count = N,
	                 .total = N,
	                 .sel = 4,
	                 .query = ""};
	int sizes[][2] = {{0, 0}, {1, 1}, {1, 40}, {2, 5}, {3, 3}};
	for (int i = 0; i < (int)(sizeof(sizes) / sizeof(*sizes)); i++) {
		char **f =
		    render_picker_frame(&v, sizes[i][0], sizes[i][1], true);
		free_frame(f, sizes[i][0]);
	}
	CHECK(1);
}
