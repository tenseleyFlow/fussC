#include "test.h"

int fussy_checks = 0;
int fussy_failures = 0;

/* Declared in their respective test_*.c files. */
void test_xstrdup(void);
void test_xmalloc_zero(void);

int main(void)
{
	RUN(test_xstrdup);
	RUN(test_xmalloc_zero);

	fprintf(stderr, "\n%d checks, %d failures\n", fussy_checks,
	        fussy_failures);
	return fussy_failures == 0 ? 0 : 1;
}
