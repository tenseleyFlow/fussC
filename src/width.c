#include "width.h"

struct interval {
	uint32_t lo;
	uint32_t hi;
};

/* Zero-width: combining marks and format characters (Kuhn-style subset covering
 * the blocks that turn up in real filenames/emoji sequences). */
static const struct interval zero_width[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A},
    {0x064B, 0x065F}, {0x0670, 0x0670}, {0x06D6, 0x06DC}, {0x0E31, 0x0E31},
    {0x0E34, 0x0E3A}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F},
    {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xE0100, 0xE01EF},
};

/* Wide (2 columns): East Asian Wide/Fullwidth and emoji. */
static const struct interval wide[] = {
    {0x1100, 0x115F},   {0x2329, 0x232A},   {0x2E80, 0x303E},
    {0x3041, 0x33FF},   {0x3400, 0x4DBF},   {0x4E00, 0x9FFF},
    {0xA000, 0xA4CF},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},
    {0xFE10, 0xFE19},   {0xFE30, 0xFE6F},   {0xFF00, 0xFF60},
    {0xFFE0, 0xFFE6},   {0x1F300, 0x1F64F}, {0x1F900, 0x1FAFF},
    {0x20000, 0x3FFFD},
};

static int in_table(uint32_t cp, const struct interval *t, size_t n)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (cp < t[mid].lo)
			hi = mid;
		else if (cp > t[mid].hi)
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

int utf8_decode(const char *s, uint32_t *cp)
{
	const unsigned char *u = (const unsigned char *)s;
	unsigned char c = u[0];

	if (c < 0x80) {
		*cp = c;
		return 1;
	}
	if ((c & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
		*cp = (uint32_t)((c & 0x1F) << 6) | (u[1] & 0x3Fu);
		return 2;
	}
	if ((c & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 &&
	    (u[2] & 0xC0) == 0x80) {
		*cp = (uint32_t)((c & 0x0F) << 12) |
		      (uint32_t)((u[1] & 0x3F) << 6) | (u[2] & 0x3Fu);
		return 3;
	}
	if ((c & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 &&
	    (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
		*cp = (uint32_t)((c & 0x07) << 18) |
		      (uint32_t)((u[1] & 0x3F) << 12) |
		      (uint32_t)((u[2] & 0x3F) << 6) | (u[3] & 0x3Fu);
		return 4;
	}

	*cp = c; /* invalid lead or truncated: treat as a single latin1 byte */
	return 1;
}

int cp_width(uint32_t cp)
{
	if (cp == 0)
		return 0;
	if (in_table(cp, zero_width, sizeof(zero_width) / sizeof(*zero_width)))
		return 0;
	if (in_table(cp, wide, sizeof(wide) / sizeof(*wide)))
		return 2;
	return 1;
}

size_t display_width(const char *s)
{
	size_t w = 0;
	while (*s != '\0') {
		uint32_t cp;
		s += utf8_decode(s, &cp);
		w += (size_t)cp_width(cp);
	}
	return w;
}
