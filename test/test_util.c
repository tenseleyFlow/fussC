#include "test.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

void test_xstrdup(void)
{
	char *s = xstrdup("hello");
	CHECK(s != NULL);
	CHECK_STR_EQ(s, "hello");
	/* Independent copy: mutating it must not be undefined behaviour. */
	s[0] = 'H';
	CHECK_STR_EQ(s, "Hello");
	free(s);

	char *empty = xstrdup("");
	CHECK(empty != NULL);
	CHECK(empty[0] == '\0');
	free(empty);
}

void test_xmalloc_zero(void)
{
	/* xmalloc(0) must not abort; the return may be NULL or a valid ptr. */
	void *p = xmalloc(0);
	free(p);
	CHECK(1);
}
