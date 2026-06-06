#include "test.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

void test_xstrdup(void)
{
	/* xstrdup is returns_nonnull, so a `!= NULL` check would be redundant
	 * (the compiler/analyzer flag it). We exercise behaviour instead. */
	char *s = xstrdup("hello");
	CHECK_STR_EQ(s, "hello");
	/* Independent copy: mutating it must not be undefined behaviour. */
	s[0] = 'H';
	CHECK_STR_EQ(s, "Hello");
	free(s);

	char *empty = xstrdup("");
	CHECK(empty[0] == '\0');
	free(empty);
}

void test_xmalloc_zero(void)
{
	/* xmalloc(0) is bumped to a 1-byte allocation: a usable, freeable,
	 * non-NULL pointer (the returns_nonnull guarantee). Prove it by use. */
	unsigned char *p = xmalloc(0);
	p[0] = 0x7F;
	CHECK(p[0] == 0x7F);
	free(p);
}
