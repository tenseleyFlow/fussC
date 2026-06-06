#include "test.h"
#include "width.h"

#include <string.h>

void test_width_ascii(void)
{
	CHECK(display_width("") == 0);
	CHECK(display_width("abc") == 3);
	CHECK(display_width("README.md") == 9);
}

void test_width_wide(void)
{
	/* Two CJK ideographs, 2 columns each. */
	CHECK(display_width("\344\270\255\346\226\207") == 4); /* 中文 */
	/* Emoji (U+1F600) is wide. */
	CHECK(display_width("\360\237\230\200") == 2); /* 😀 */
	/* Mixed ascii + CJK. */
	CHECK(display_width("a\344\270\255b") == 4);
}

void test_width_combining(void)
{
	/* 'e' + combining acute (U+0301): one base column, mark adds zero. */
	CHECK(display_width("e\314\201") == 1);
	/* A variation selector (U+FE0F) is zero-width. */
	CHECK(display_width("x\357\270\217") == 1);
}

void test_utf8_decode(void)
{
	uint32_t cp;
	CHECK(utf8_decode("A", &cp) == 1 && cp == 0x41);
	CHECK(utf8_decode("\303\251", &cp) == 2 && cp == 0xE9);       /* é */
	CHECK(utf8_decode("\344\270\255", &cp) == 3 && cp == 0x4E2D); /* 中 */
	CHECK(utf8_decode("\360\237\230\200", &cp) == 4 &&
	      cp == 0x1F600); /* 😀 */

	/* Truncated lead byte: consumes 1, no over-read past NUL. */
	CHECK(utf8_decode("\344", &cp) == 1);
}
