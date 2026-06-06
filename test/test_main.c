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

	fprintf(stderr, "\n%d checks, %d failures\n", fussy_checks,
	        fussy_failures);
	return fussy_failures == 0 ? 0 : 1;
}
