#include "status.h"
#include "test.h"

void test_status_merge(void)
{
	file_status s = 0;
	CHECK(s == 0);

	s = status_merge(s, ST_STAGED);
	CHECK(s == ST_STAGED);

	/* Merging is bitwise-or: a file can be staged and unstaged at once. */
	s = status_merge(s, ST_UNSTAGED);
	CHECK((s & ST_STAGED) != 0);
	CHECK((s & ST_UNSTAGED) != 0);

	/* Idempotent. */
	CHECK(status_merge(s, ST_STAGED) == s);
}

void test_status_is_dirty(void)
{
	CHECK(status_is_dirty(ST_STAGED));
	CHECK(status_is_dirty(ST_UNSTAGED));
	CHECK(status_is_dirty(ST_UNTRACKED));
	CHECK(status_is_dirty(ST_STAGED | ST_GITIGNORED));

	/* Informational-only states are not dirty. */
	CHECK(!status_is_dirty(0));
	CHECK(!status_is_dirty(ST_INCOMING));
	CHECK(!status_is_dirty(ST_GITIGNORED));
	CHECK(!status_is_dirty(ST_INCOMING | ST_GITIGNORED));
}
