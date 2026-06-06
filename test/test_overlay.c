#include "overlay.h"
#include "test.h"

#include <string.h>

void test_overlay_commit_edit(void)
{
	overlay o;
	overlay_close(&o);
	CHECK(!overlay_active(&o));

	overlay_open_commit(&o, false, NULL);
	CHECK(overlay_active(&o));
	CHECK(o.kind == OV_COMMIT && !o.amend);

	overlay_insert(&o, 'h');
	overlay_insert(&o, 'i');
	CHECK_STR_EQ(o.text, "hi");
	CHECK(o.cursor == 2);

	overlay_left(&o); /* cursor between h and i */
	overlay_insert(&o, 'X');
	CHECK_STR_EQ(o.text, "hXi");
	overlay_backspace(&o); /* remove the X */
	CHECK_STR_EQ(o.text, "hi");

	overlay_close(&o);
	CHECK(!overlay_active(&o));
}

void test_overlay_amend_and_rename_prefill(void)
{
	overlay o;
	overlay_open_commit(&o, true, "previous message");
	CHECK(o.amend);
	CHECK_STR_EQ(o.text, "previous message");
	CHECK(o.cursor == strlen("previous message")); /* cursor at end */

	overlay_open_rename(&o, "old.txt");
	CHECK(o.kind == OV_RENAME);
	CHECK_STR_EQ(o.text, "old.txt");
}

void test_overlay_utf8_edit(void)
{
	overlay o;
	overlay_open_tag(&o);
	overlay_insert(&o, 0x4E2D); /* 中, 3 bytes */
	overlay_insert(&o, 'x');
	CHECK(o.len == 4);

	overlay_left(&o);      /* between 中 and x */
	overlay_backspace(&o); /* removes the whole 中 codepoint */
	CHECK_STR_EQ(o.text, "x");
	CHECK(o.len == 1);
}

void test_overlay_confirm_is_not_editable(void)
{
	overlay o;
	overlay_open_confirm(&o, "Discard changes? (y/n)");
	CHECK(o.kind == OV_CONFIRM);
	CHECK_STR_EQ(o.title, "Discard changes? (y/n)");

	overlay_insert(&o, 'a'); /* editing is a no-op for confirm */
	CHECK(o.len == 0);
}
