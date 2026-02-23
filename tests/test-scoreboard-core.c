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

static void test_overtime_enabled(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_overtime_enabled());
	scoreboard_set_overtime_enabled(false);
	assert(!scoreboard_get_overtime_enabled());
	scoreboard_set_overtime_enabled(true);
	assert(scoreboard_get_overtime_enabled());
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
	test_overtime_enabled();

	printf("All scoreboard-core clock/period tests passed.\n");
	return 0;
}
