#include "test.h"

int fussy_checks = 0;
int fussy_failures = 0;

/* Declared in their respective test_*.c files. */
void test_xstrdup(void);
void test_xmalloc_zero(void);
void test_status_merge(void);
void test_status_is_dirty(void);
void test_tree_build(void);
void test_tree_merge_status(void);
void test_tree_case_order(void);
void test_flatten_expanded(void);
void test_flatten_collapsed(void);
void test_flatten_empty(void);
void test_git_status(void);
void test_git_not_a_repo(void);
void test_render_golden(void);
void test_render_pipe_gutter(void);
void test_render_color(void);
void test_width_ascii(void);
void test_width_wide(void);
void test_width_combining(void);
void test_utf8_decode(void);

int main(void)
{
	RUN(test_xstrdup);
	RUN(test_xmalloc_zero);
	RUN(test_status_merge);
	RUN(test_status_is_dirty);
	RUN(test_tree_build);
	RUN(test_tree_merge_status);
	RUN(test_tree_case_order);
	RUN(test_flatten_expanded);
	RUN(test_flatten_collapsed);
	RUN(test_flatten_empty);
	RUN(test_git_status);
	RUN(test_git_not_a_repo);
	RUN(test_render_golden);
	RUN(test_render_pipe_gutter);
	RUN(test_render_color);
	RUN(test_width_ascii);
	RUN(test_width_wide);
	RUN(test_width_combining);
	RUN(test_utf8_decode);

	fprintf(stderr, "\n%d checks, %d failures\n", fussy_checks,
	        fussy_failures);
	return fussy_failures == 0 ? 0 : 1;
}
