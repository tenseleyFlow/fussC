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
void test_render_golden(void);
void test_render_pipe_gutter(void);
void test_render_color(void);
void test_width_ascii(void);
void test_width_wide(void);
void test_width_combining(void);
void test_utf8_decode(void);
void test_key_arrows(void);
void test_key_lone_esc(void);
void test_key_controls(void);
void test_key_printable(void);
void test_nav_updown(void);
void test_nav_right_left(void);
void test_nav_dotfiles(void);
void test_toggle_matches_oracle(void);
void test_nav_filter_buffer(void);
void test_render_frame_layout(void);
void test_render_frame_selection(void);
void test_render_frame_filter_in_header(void);
void test_frame_diff_minimal(void);
void test_frame_diff_full_paint(void);
void test_input_navigation(void);
void test_input_case_is_mode(void);
void test_input_editing_and_quit(void);
void test_fuzzy_tiers(void);
void test_fuzzy_ordering(void);
void test_fuzzy_best_match(void);
void test_fuzzy_auto_expand(void);
void test_fuzzy_no_match_flag(void);
void test_fuzzy_fuzz(void);
void test_engine_basic(void);
void test_engine_convergence(void);

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
	RUN(test_render_golden);
	RUN(test_render_pipe_gutter);
	RUN(test_render_color);
	RUN(test_width_ascii);
	RUN(test_width_wide);
	RUN(test_width_combining);
	RUN(test_utf8_decode);
	RUN(test_key_arrows);
	RUN(test_key_lone_esc);
	RUN(test_key_controls);
	RUN(test_key_printable);
	RUN(test_nav_updown);
	RUN(test_nav_right_left);
	RUN(test_nav_dotfiles);
	RUN(test_toggle_matches_oracle);
	RUN(test_nav_filter_buffer);
	RUN(test_render_frame_layout);
	RUN(test_render_frame_selection);
	RUN(test_render_frame_filter_in_header);
	RUN(test_frame_diff_minimal);
	RUN(test_frame_diff_full_paint);
	RUN(test_input_navigation);
	RUN(test_input_case_is_mode);
	RUN(test_input_editing_and_quit);
	RUN(test_fuzzy_tiers);
	RUN(test_fuzzy_ordering);
	RUN(test_fuzzy_best_match);
	RUN(test_fuzzy_auto_expand);
	RUN(test_fuzzy_no_match_flag);
	RUN(test_fuzzy_fuzz);
	RUN(test_engine_basic);
	RUN(test_engine_convergence);

	fprintf(stderr, "\n%d checks, %d failures\n", fussy_checks,
	        fussy_failures);
	return fussy_failures == 0 ? 0 : 1;
}
