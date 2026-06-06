#include "input.h"
#include "term.h"
#include "test.h"

void test_input_navigation(void)
{
	CHECK(input_classify(KEY_UP).kind == ACT_UP);
	CHECK(input_classify(KEY_DOWN).kind == ACT_DOWN);
	CHECK(input_classify(KEY_LEFT).kind == ACT_LEFT);
	CHECK(input_classify(KEY_RIGHT).kind == ACT_RIGHT);
	/* fzf-style Ctrl aliases. */
	CHECK(input_classify(KEY_CTRL('N')).kind == ACT_DOWN);
	CHECK(input_classify(KEY_CTRL('P')).kind == ACT_UP);
	CHECK(input_classify(KEY_CTRL('F')).kind == ACT_RIGHT);
	CHECK(input_classify(KEY_CTRL('B')).kind == ACT_LEFT);
	CHECK(input_classify(' ').kind == ACT_TOGGLE);
}

void test_input_case_is_mode(void)
{
	/* lowercase + digits + punctuation + unicode -> filter */
	action a = input_classify('a');
	CHECK(a.kind == ACT_FILTER_PUSH && a.cp == 'a');
	CHECK(input_classify('z').kind == ACT_FILTER_PUSH);
	CHECK(input_classify('5').kind == ACT_FILTER_PUSH);
	CHECK(input_classify('.').kind == ACT_FILTER_PUSH);
	CHECK(input_classify('-').kind == ACT_FILTER_PUSH);
	CHECK(input_classify(0x4E2D).kind == ACT_FILTER_PUSH); /* 中 */

	/* uppercase -> command (Sprint 4), except special-cased ones */
	a = input_classify('A');
	CHECK(a.kind == ACT_COMMAND && a.cp == 'A');
	CHECK(input_classify('Z').kind == ACT_COMMAND);
	CHECK(input_classify('Q').kind == ACT_QUIT);
	CHECK(input_classify('H').kind == ACT_TOGGLE_DOTFILES);
	CHECK(input_classify('?').kind == ACT_HELP); /* not a filter char */
}

void test_input_editing_and_quit(void)
{
	CHECK(input_classify(KEY_ESC).kind == ACT_FILTER_CLEAR);
	CHECK(input_classify(KEY_BACKSPACE).kind == ACT_FILTER_BACKSPACE);
	CHECK(input_classify(KEY_CTRL('Q')).kind == ACT_QUIT);
	CHECK(input_classify(KEY_EOF).kind == ACT_QUIT);
	CHECK(input_classify(KEY_RESIZE).kind == ACT_REDRAW);
	/* reserved keys do nothing yet */
	CHECK(input_classify(KEY_ENTER).kind == ACT_NONE);
	CHECK(input_classify(KEY_TAB).kind == ACT_NONE);
}
