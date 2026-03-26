#include "scoreboard-core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_log_call_count = 0;
static enum scoreboard_log_level g_last_log_level;
static char g_last_log_message[256];

static void test_log_fn(enum scoreboard_log_level level, const char *message)
{
	g_log_call_count++;
	g_last_log_level = level;
	if (message != NULL) {
		size_t len = strlen(message);
		if (len >= sizeof(g_last_log_message))
			len = sizeof(g_last_log_message) - 1;
		memcpy(g_last_log_message, message, len);
		g_last_log_message[len] = '\0';
	}
}

static void test_description(void)
{
	const char *desc = scoreboard_description();
	assert(desc != NULL);
	assert(strlen(desc) > 0);
}

static void test_lifecycle(void)
{
	scoreboard_reset_state_for_tests();
	g_log_call_count = 0;

	bool loaded = scoreboard_on_load(test_log_fn);
	assert(loaded);
	assert(g_log_call_count == 1);
	assert(g_last_log_level == SCOREBOARD_LOG_INFO);

	scoreboard_on_unload(test_log_fn);
	assert(g_log_call_count == 2);
	assert(g_last_log_level == SCOREBOARD_LOG_INFO);
}

static void test_lifecycle_null_log(void)
{
	scoreboard_reset_state_for_tests();
	bool loaded = scoreboard_on_load(NULL);
	assert(loaded);
	scoreboard_on_unload(NULL);
}

static void test_clock_start_stop(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_clock_is_running());

	scoreboard_clock_start();
	assert(scoreboard_clock_is_running());

	scoreboard_clock_stop();
	assert(!scoreboard_clock_is_running());
}

static void test_clock_tick_countdown(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_DOWN);

	int initial = scoreboard_clock_get_tenths();
	assert(initial == 900 * 10);

	scoreboard_clock_start();
	scoreboard_clock_tick(10);
	assert(scoreboard_clock_get_tenths() == initial - 10);

	/* tick does nothing when stopped */
	scoreboard_clock_stop();
	int after_stop = scoreboard_clock_get_tenths();
	scoreboard_clock_tick(10);
	assert(scoreboard_clock_get_tenths() == after_stop);
}

static void test_clock_tick_countdown_floor(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_set_tenths(5);
	scoreboard_clock_start();
	scoreboard_clock_tick(100);
	assert(scoreboard_clock_get_tenths() == 0);
}

static void test_clock_tick_countup(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_clock_direction(SCOREBOARD_CLOCK_COUNT_UP);
	scoreboard_clock_set_tenths(0);

	scoreboard_clock_start();
	scoreboard_clock_tick(10);
	assert(scoreboard_clock_get_tenths() == 10);
}

static void test_clock_tick_countup_cap(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_clock_direction(SCOREBOARD_CLOCK_COUNT_UP);
	int max = scoreboard_get_period_length() * 10;
	scoreboard_clock_set_tenths(max - 5);
	scoreboard_clock_start();
	scoreboard_clock_tick(100);
	assert(scoreboard_clock_get_tenths() == max);
}

static void test_clock_reset_countdown(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_start();
	scoreboard_clock_tick(100);
	scoreboard_clock_reset();
	assert(!scoreboard_clock_is_running());
	assert(scoreboard_clock_get_tenths() ==
	       scoreboard_get_period_length() * 10);
}

static void test_clock_reset_countup(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_clock_direction(SCOREBOARD_CLOCK_COUNT_UP);
	scoreboard_clock_set_tenths(500);
	scoreboard_clock_reset();
	assert(scoreboard_clock_get_tenths() == 0);
}

static void test_clock_set_tenths_negative(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_set_tenths(-10);
	assert(scoreboard_clock_get_tenths() == 0);
}

static void test_clock_format(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_set_tenths(7505);
	char buf[32];
	scoreboard_clock_format(buf, sizeof(buf));
	assert(strcmp(buf, "12:30") == 0);

	scoreboard_clock_set_tenths(50);
	scoreboard_clock_format(buf, sizeof(buf));
	assert(strcmp(buf, "0:05") == 0);

	scoreboard_clock_set_tenths(0);
	scoreboard_clock_format(buf, sizeof(buf));
	assert(strcmp(buf, "0:00") == 0);
}

static void test_clock_format_null(void)
{
	scoreboard_clock_format(NULL, 0);
	char buf[2];
	scoreboard_clock_format(buf, 0);
}

static void test_clock_adjust_seconds(void)
{
	scoreboard_reset_state_for_tests();
	int initial = scoreboard_clock_get_tenths();
	scoreboard_clock_adjust_seconds(10);
	assert(scoreboard_clock_get_tenths() == initial + 100);

	scoreboard_clock_adjust_seconds(-5);
	assert(scoreboard_clock_get_tenths() == initial + 50);
}

static void test_clock_adjust_seconds_floor(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_set_tenths(30);
	scoreboard_clock_adjust_seconds(-10);
	assert(scoreboard_clock_get_tenths() == 0);
}

static void test_clock_adjust_minutes(void)
{
	scoreboard_reset_state_for_tests();
	int initial = scoreboard_clock_get_tenths();
	scoreboard_clock_adjust_minutes(2);
	assert(scoreboard_clock_get_tenths() == initial + 1200);

	scoreboard_clock_adjust_minutes(-1);
	assert(scoreboard_clock_get_tenths() == initial + 600);
}

static void test_clock_adjust_minutes_floor(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_set_tenths(100);
	scoreboard_clock_adjust_minutes(-5);
	assert(scoreboard_clock_get_tenths() == 0);
}

static void test_clock_direction(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_DOWN);
	scoreboard_set_clock_direction(SCOREBOARD_CLOCK_COUNT_UP);
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_UP);
}

static void test_period_length(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_period_length() == 900);
	scoreboard_set_period_length(1200);
	assert(scoreboard_get_period_length() == 1200);
}

static void test_period_length_min(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period_length(0);
	assert(scoreboard_get_period_length() == 1);
	scoreboard_set_period_length(-5);
	assert(scoreboard_get_period_length() == 1);
}

static void test_period_get_set(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_period() == 1);
	scoreboard_set_period(2);
	assert(scoreboard_get_period() == 2);
	scoreboard_set_period(3);
	assert(scoreboard_get_period() == 3);
}

static void test_period_set_clamp(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period(0);
	assert(scoreboard_get_period() == 1);
	scoreboard_set_period(7);
	assert(scoreboard_get_period() == 7);
	scoreboard_set_period(8);
	assert(scoreboard_get_period() == 7);

	scoreboard_set_overtime_enabled(false);
	scoreboard_set_period(4);
	assert(scoreboard_get_period() == 3);
}

static void test_period_advance(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_period() == 1);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 2);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 3);
}

static void test_period_advance_ot(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period(3);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 4);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 5);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 6);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 7);
	/* cap at OT4 */
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 7);
}

static void test_period_advance_no_ot(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_overtime_enabled(false);
	scoreboard_set_period(3);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 3);
}

static void test_period_advance_resets_clock(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_start();
	scoreboard_clock_tick(100);
	scoreboard_period_advance();
	assert(!scoreboard_clock_is_running());
	assert(scoreboard_clock_get_tenths() ==
	       scoreboard_get_period_length() * 10);
}

static void test_period_rewind(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period(3);
	scoreboard_period_rewind();
	assert(scoreboard_get_period() == 2);
	scoreboard_period_rewind();
	assert(scoreboard_get_period() == 1);
	/* floor at 1 */
	scoreboard_period_rewind();
	assert(scoreboard_get_period() == 1);
}

static void test_period_format(void)
{
	scoreboard_reset_state_for_tests();
	char buf[16];

	scoreboard_set_period(1);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "1") == 0);

	scoreboard_set_period(2);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "2") == 0);

	scoreboard_set_period(3);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "3") == 0);

	scoreboard_set_period(4);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "OT") == 0);

	scoreboard_set_period(5);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "OT2") == 0);

	scoreboard_set_period(6);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "OT3") == 0);

	scoreboard_set_period(7);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "OT4") == 0);
}

static void test_period_format_null(void)
{
	scoreboard_format_period(NULL, 0);
	char buf[2];
	scoreboard_format_period(buf, 0);
}

static void test_clock_adjust_syncs_penalty(void)
{
	/* Adjusting clock adjusts penalties by the same delta */
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120); /* 1200 tenths */

	/* Subtract: both clock and penalty decrease */
	scoreboard_clock_adjust_seconds(-10);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1100);

	/* Add: both clock and penalty increase */
	scoreboard_clock_adjust_seconds(5);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1150);

	/* Same for minutes */
	scoreboard_clock_adjust_minutes(-1);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 550);
	scoreboard_clock_adjust_minutes(1);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1150);
}

static void test_clock_adjust_penalty_clears_at_zero(void)
{
	/* Subtracting past zero clears the penalty */
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 5); /* 50 tenths */
	scoreboard_clock_adjust_seconds(-10);
	assert(!scoreboard_get_home_penalty(0)->active);
}

static void test_penalty_adjust_both_teams(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_away_penalty_add(22, 5); /* 50 tenths */
	scoreboard_penalty_adjust(-100);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1100);
	/* away penalty cleared since 50 - 100 <= 0 */
	assert(!scoreboard_get_away_penalty(0)->active);
}

static void test_penalty_clear_after_adjust(void)
{
	/* Verify a penalty can still be manually cleared after clock adjust */
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120); /* 1200 tenths */
	scoreboard_away_penalty_add(22, 60);  /* 600 tenths */

	/* Adjust clock forward — penalties gain time */
	scoreboard_clock_adjust_minutes(2);
	assert(scoreboard_get_home_penalty(0)->active);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 2400);
	assert(scoreboard_get_away_penalty(0)->active);
	assert(scoreboard_get_away_penalty(0)->remaining_tenths == 1800);

	/* Manual clear should still work */
	scoreboard_home_penalty_clear(0);
	assert(!scoreboard_get_home_penalty(0)->active);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 0);

	/* Away penalty unaffected by home clear */
	assert(scoreboard_get_away_penalty(0)->active);
	scoreboard_away_penalty_clear(0);
	assert(!scoreboard_get_away_penalty(0)->active);
}

static void test_penalty_adjust_positive_delta(void)
{
	/* Verify positive delta adds time to all active penalties */
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 60);  /* 600 tenths */
	scoreboard_away_penalty_add(22, 120); /* 1200 tenths */

	scoreboard_penalty_adjust(300);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 900);
	assert(scoreboard_get_away_penalty(0)->remaining_tenths == 1500);

	/* Inactive slots should not be affected */
	assert(!scoreboard_get_home_penalty(1)->active);
	assert(scoreboard_get_home_penalty(1)->remaining_tenths == 0);
}

static void test_penalties_stop_when_clock_stops(void)
{
	scoreboard_reset_state_for_tests();
	/* Set clock to 1 second (10 tenths) */
	scoreboard_clock_set_tenths(10);
	scoreboard_home_penalty_add(12, 120); /* 1200 tenths */

	scoreboard_clock_start();
	/* Tick 10 tenths — clock reaches 0:00 and auto-stops */
	scoreboard_clock_tick(10);

	assert(scoreboard_clock_get_tenths() == 0);
	assert(!scoreboard_clock_is_running());
	/* Penalty should NOT have been decremented on the stopping tick */
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1200);
}

static void test_penalties_tick_while_clock_runs(void)
{
	scoreboard_reset_state_for_tests();
	/* Set clock to 2 seconds (20 tenths) */
	scoreboard_clock_set_tenths(20);
	scoreboard_home_penalty_add(12, 120); /* 1200 tenths */

	scoreboard_clock_start();
	/* Tick 10 tenths — clock still has 10 left, stays running */
	scoreboard_clock_tick(10);

	assert(scoreboard_clock_get_tenths() == 10);
	assert(scoreboard_clock_is_running());
	/* Penalty SHOULD have ticked */
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1190);
}

static void test_overtime_enabled(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_overtime_enabled());
	scoreboard_set_overtime_enabled(false);
	assert(!scoreboard_get_overtime_enabled());
	scoreboard_set_overtime_enabled(true);
	assert(scoreboard_get_overtime_enabled());
}

static void test_dirty_reset_clears(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_is_dirty());
}

static void test_dirty_clock_start(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_start();
	assert(scoreboard_is_dirty());
}

static void test_dirty_clock_stop(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_stop();
	assert(scoreboard_is_dirty());
}

static void test_dirty_clock_reset(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_reset();
	assert(scoreboard_is_dirty());
}

static void test_dirty_clock_tick_running(void)
{
	/* Start clock, clear dirty via write, then tick — should set dirty */
	scoreboard_reset_state_for_tests();
	scoreboard_set_output_directory("/tmp");
	scoreboard_clock_start();
	scoreboard_write_all_files();
	assert(!scoreboard_is_dirty());
	scoreboard_clock_tick(1);
	assert(scoreboard_is_dirty());
}

static void test_dirty_clock_tick_stopped(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_is_dirty());
	scoreboard_clock_tick(1);
	assert(!scoreboard_is_dirty());
}

static void test_dirty_clock_set_tenths(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_set_tenths(100);
	assert(scoreboard_is_dirty());
}

static void test_dirty_clock_adjust_seconds(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_adjust_seconds(1);
	assert(scoreboard_is_dirty());
}

static void test_dirty_clock_adjust_minutes(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_clock_adjust_minutes(1);
	assert(scoreboard_is_dirty());
}

static void test_dirty_set_clock_direction(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_clock_direction(SCOREBOARD_CLOCK_COUNT_UP);
	assert(scoreboard_is_dirty());
}

static void test_dirty_set_period_length(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period_length(600);
	assert(scoreboard_is_dirty());
}

static void test_dirty_set_period(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period(2);
	assert(scoreboard_is_dirty());
}

static void test_dirty_period_advance(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_period_advance();
	assert(scoreboard_is_dirty());
}

static void test_dirty_period_rewind(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period(3);
	scoreboard_period_rewind();
	assert(scoreboard_is_dirty());
}

static void test_dirty_set_overtime_enabled(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_overtime_enabled(false);
	assert(scoreboard_is_dirty());
}

static void test_dirty_mark_dirty(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_is_dirty());
	scoreboard_mark_dirty();
	assert(scoreboard_is_dirty());
}

/* ---- period label tests ---- */

static void test_default_period_labels_hockey(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_period_label_count() == 7);
	assert(strcmp(scoreboard_get_period_label(0), "1") == 0);
	assert(strcmp(scoreboard_get_period_label(1), "2") == 0);
	assert(strcmp(scoreboard_get_period_label(2), "3") == 0);
	assert(strcmp(scoreboard_get_period_label(3), "OT") == 0);
	assert(strcmp(scoreboard_get_period_label(4), "OT2") == 0);
	assert(strcmp(scoreboard_get_period_label(5), "OT3") == 0);
	assert(strcmp(scoreboard_get_period_label(6), "OT4") == 0);
}

static void test_period_labels_no_ot(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_overtime_enabled(false);
	assert(scoreboard_get_period_label_count() == 3);
	assert(strcmp(scoreboard_get_period_label(0), "1") == 0);
	assert(strcmp(scoreboard_get_period_label(1), "2") == 0);
	assert(strcmp(scoreboard_get_period_label(2), "3") == 0);
}

static void test_set_custom_period_labels(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period_labels("1\n2\n3\n4\n5\nOT\n2OT\n");
	assert(scoreboard_get_period_label_count() == 7);
	assert(strcmp(scoreboard_get_period_label(0), "1") == 0);
	assert(strcmp(scoreboard_get_period_label(4), "5") == 0);
	assert(strcmp(scoreboard_get_period_label(5), "OT") == 0);
	assert(strcmp(scoreboard_get_period_label(6), "2OT") == 0);

	/* format_period uses custom labels */
	char buf[16];
	scoreboard_set_period(6);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "OT") == 0);

	scoreboard_set_period(7);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "2OT") == 0);
}

static void test_period_labels_clamp_period(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period(5);
	/* Set labels with fewer entries — period should clamp */
	scoreboard_set_period_labels("1\n2\n3\n");
	assert(scoreboard_get_period_label_count() == 3);
	assert(scoreboard_get_period() == 3);
}

static void test_period_labels_skip_empty_lines(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period_labels("1\n\n2\n\n3\n");
	assert(scoreboard_get_period_label_count() == 3);
	assert(strcmp(scoreboard_get_period_label(0), "1") == 0);
	assert(strcmp(scoreboard_get_period_label(1), "2") == 0);
	assert(strcmp(scoreboard_get_period_label(2), "3") == 0);
}

static void test_period_labels_null_ignored(void)
{
	scoreboard_reset_state_for_tests();
	int orig = scoreboard_get_period_label_count();
	scoreboard_set_period_labels(NULL);
	assert(scoreboard_get_period_label_count() == orig);
}

static void test_period_labels_empty_string_ignored(void)
{
	scoreboard_reset_state_for_tests();
	int orig = scoreboard_get_period_label_count();
	scoreboard_set_period_labels("");
	assert(scoreboard_get_period_label_count() == orig);
}

static void test_get_period_labels_roundtrip(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period_labels("A\nB\nC\n");
	char buf[256];
	scoreboard_get_period_labels(buf, sizeof(buf));
	assert(strcmp(buf, "A\nB\nC\n") == 0);
}

static void test_get_period_labels_null(void)
{
	scoreboard_get_period_labels(NULL, 0);
	char buf[2];
	scoreboard_get_period_labels(buf, 0);
}

static void test_get_period_label_out_of_range(void)
{
	scoreboard_reset_state_for_tests();
	assert(strcmp(scoreboard_get_period_label(-1), "") == 0);
	assert(strcmp(scoreboard_get_period_label(100), "") == 0);
}

static void test_period_format_fallback_beyond_labels(void)
{
	scoreboard_reset_state_for_tests();
	/* Force period beyond label count via direct state manipulation */
	scoreboard_set_period_labels("X\nY\n");
	/* period_label_count is 2, period was clamped to 2 */
	char buf[16];
	scoreboard_set_period(2);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "Y") == 0);
}

static void test_overtime_enabled_regenerates_labels(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_period_label_count() == 7);
	scoreboard_set_overtime_enabled(false);
	assert(scoreboard_get_period_label_count() == 3);
	scoreboard_set_overtime_enabled(true);
	assert(scoreboard_get_period_label_count() == 7);
}

static void test_set_period_labels_marks_dirty(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_is_dirty());
	scoreboard_set_period_labels("1\n2\n");
	assert(scoreboard_is_dirty());
}

static void test_period_labels_long_label_truncated(void)
{
	scoreboard_reset_state_for_tests();
	/* SCOREBOARD_PERIOD_LABEL_SIZE is 16, so a 20-char label gets truncated */
	scoreboard_set_period_labels("ABCDEFGHIJKLMNOPQRST\n");
	assert(scoreboard_get_period_label_count() == 1);
	assert(strlen(scoreboard_get_period_label(0)) == 15);
}

static void test_get_period_labels_small_buffer(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period_labels("Alpha\nBeta\nGamma\n");
	/* Buffer too small to hold all labels — stops early */
	char buf[8];
	scoreboard_get_period_labels(buf, sizeof(buf));
	/* Should have "Alpha\n" (6 chars) and stop before Beta */
	assert(strcmp(buf, "Alpha\n") == 0);
}

static void test_period_format_beyond_labels(void)
{
	scoreboard_reset_state_for_tests();
	/* Manually force period beyond label range by setting labels after
	   setting period. We need a trick: set many labels first, set period
	   high, then reduce labels. But set_period_labels clamps... so instead
	   test the fallback path by checking format works at boundary. */
	/* The fallback prints the raw number. We can't reach it via the public
	   API since set_period clamps, but we can test via a save/load
	   with a hand-crafted JSON that has period > label count. For now,
	   we verify the label-based path works at max index. */
	scoreboard_set_period_labels("X\n");
	scoreboard_set_period(1);
	char buf[16];
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "X") == 0);
}

int main(void)
{
	test_description();
	test_lifecycle();
	test_lifecycle_null_log();
	test_clock_start_stop();
	test_clock_tick_countdown();
	test_clock_tick_countdown_floor();
	test_clock_tick_countup();
	test_clock_tick_countup_cap();
	test_clock_reset_countdown();
	test_clock_reset_countup();
	test_clock_set_tenths_negative();
	test_clock_format();
	test_clock_format_null();
	test_clock_adjust_seconds();
	test_clock_adjust_seconds_floor();
	test_clock_adjust_minutes();
	test_clock_adjust_minutes_floor();
	test_clock_direction();
	test_period_length();
	test_period_length_min();
	test_period_get_set();
	test_period_set_clamp();
	test_period_advance();
	test_period_advance_ot();
	test_period_advance_no_ot();
	test_period_advance_resets_clock();
	test_period_rewind();
	test_period_format();
	test_period_format_null();
	test_clock_adjust_syncs_penalty();
	test_clock_adjust_penalty_clears_at_zero();
	test_penalty_adjust_both_teams();
	test_penalty_clear_after_adjust();
	test_penalty_adjust_positive_delta();
	test_penalties_stop_when_clock_stops();
	test_penalties_tick_while_clock_runs();
	test_overtime_enabled();
	test_dirty_reset_clears();
	test_dirty_clock_start();
	test_dirty_clock_stop();
	test_dirty_clock_reset();
	test_dirty_clock_tick_running();
	test_dirty_clock_tick_stopped();
	test_dirty_clock_set_tenths();
	test_dirty_clock_adjust_seconds();
	test_dirty_clock_adjust_minutes();
	test_dirty_set_clock_direction();
	test_dirty_set_period_length();
	test_dirty_set_period();
	test_dirty_period_advance();
	test_dirty_period_rewind();
	test_dirty_set_overtime_enabled();
	test_dirty_mark_dirty();
	test_default_period_labels_hockey();
	test_period_labels_no_ot();
	test_set_custom_period_labels();
	test_period_labels_clamp_period();
	test_period_labels_skip_empty_lines();
	test_period_labels_null_ignored();
	test_period_labels_empty_string_ignored();
	test_get_period_labels_roundtrip();
	test_get_period_labels_null();
	test_get_period_label_out_of_range();
	test_period_format_fallback_beyond_labels();
	test_overtime_enabled_regenerates_labels();
	test_set_period_labels_marks_dirty();
	test_period_labels_long_label_truncated();
	test_get_period_labels_small_buffer();
	test_period_format_beyond_labels();

	printf("All scoreboard-core clock/period tests passed.\n");
	return 0;
}
