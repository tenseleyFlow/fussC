#include "test.h"

int fussy_checks = 0;
int fussy_failures = 0;

/* Declared in their respective test_*.c files. */
void test_xstrdup(void);
void test_xmalloc_zero(void);
void test_status_merge(void);
void test_status_is_dirty(void);

int main(void)
{
	RUN(test_xstrdup);
	RUN(test_xmalloc_zero);
	RUN(test_status_merge);
	RUN(test_status_is_dirty);

	fprintf(stderr, "\n%d checks, %d failures\n", fussy_checks,
	        fussy_failures);
	return fussy_failures == 0 ? 0 : 1;
}
