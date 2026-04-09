#include "scoreboard-core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_home_penalty_add(void)
{
	scoreboard_reset_state_for_tests();
	int slot = scoreboard_home_penalty_add(12, 120);
	assert(slot == 0);

	const struct scoreboard_penalty *p = scoreboard_get_home_penalty(0);
	assert(p != NULL);
	assert(p->active);
	assert(p->player_number == 12);
	assert(p->remaining_tenths == 1200);
}

static void test_home_penalty_add_second_slot(void)
{
	scoreboard_reset_state_for_tests();
	int s1 = scoreboard_home_penalty_add(12, 120);
	assert(s1 == 0);
	int s2 = scoreboard_home_penalty_add(7, 120);
	assert(s2 == 1);

	const struct scoreboard_penalty *p1 = scoreboard_get_home_penalty(0);
	const struct scoreboard_penalty *p2 = scoreboard_get_home_penalty(1);
	assert(p1->active && p1->player_number == 12);
	assert(p2->active && p2->player_number == 7);
}

static void test_home_penalty_full(void)
{
	scoreboard_reset_state_for_tests();
	for (int i = 0; i < SCOREBOARD_MAX_PENALTIES; i++)
		assert(scoreboard_home_penalty_add(i + 1, 120) == i);
	int slot = scoreboard_home_penalty_add(99, 120);
	assert(slot == -1);
}

static void test_home_penalty_clear(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	assert(scoreboard_get_home_penalty(0)->active);
	scoreboard_home_penalty_clear(0);
	assert(!scoreboard_get_home_penalty(0)->active);
	assert(scoreboard_get_home_penalty(0)->player_number == 0);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 0);
}

static void test_home_penalty_clear_invalid(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_clear(-1);
	scoreboard_home_penalty_clear(SCOREBOARD_MAX_PENALTIES);
}

static void test_home_penalty_get_invalid(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_penalty(-1) == NULL);
	assert(scoreboard_get_home_penalty(SCOREBOARD_MAX_PENALTIES) == NULL);
}

static void test_away_penalty_add(void)
{
	scoreboard_reset_state_for_tests();
	int slot = scoreboard_away_penalty_add(22, 300);
	assert(slot == 0);

	const struct scoreboard_penalty *p = scoreboard_get_away_penalty(0);
	assert(p != NULL);
	assert(p->active);
	assert(p->player_number == 22);
	assert(p->remaining_tenths == 3000);
}

static void test_away_penalty_add_second_slot(void)
{
	scoreboard_reset_state_for_tests();
	int s1 = scoreboard_away_penalty_add(22, 120);
	assert(s1 == 0);
	int s2 = scoreboard_away_penalty_add(15, 120);
	assert(s2 == 1);
}

static void test_away_penalty_full(void)
{
	scoreboard_reset_state_for_tests();
	for (int i = 0; i < SCOREBOARD_MAX_PENALTIES; i++)
		assert(scoreboard_away_penalty_add(i + 1, 120) == i);
	int slot = scoreboard_away_penalty_add(99, 120);
	assert(slot == -1);
}

static void test_away_penalty_clear(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(22, 120);
	scoreboard_away_penalty_clear(0);
	assert(!scoreboard_get_away_penalty(0)->active);
}

static void test_away_penalty_clear_invalid(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_clear(-1);
	scoreboard_away_penalty_clear(SCOREBOARD_MAX_PENALTIES);
}

static void test_away_penalty_get_invalid(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_away_penalty(-1) == NULL);
	assert(scoreboard_get_away_penalty(SCOREBOARD_MAX_PENALTIES) == NULL);
}

static void test_penalty_tick(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 10);
	scoreboard_away_penalty_add(22, 5);

	scoreboard_penalty_tick(30);

	assert(scoreboard_get_home_penalty(0)->active);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 70);

	assert(scoreboard_get_away_penalty(0)->active);
	assert(scoreboard_get_away_penalty(0)->remaining_tenths == 20);
}

static void test_penalty_tick_expire(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 1);
	scoreboard_away_penalty_add(22, 1);

	scoreboard_penalty_tick(10);
	assert(!scoreboard_get_home_penalty(0)->active);
	assert(!scoreboard_get_away_penalty(0)->active);
}

static void test_penalty_tick_no_active(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_penalty_tick(10);
}

static void test_clock_tick_drives_penalty_tick(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 2);
	scoreboard_clock_start();
	scoreboard_clock_tick(10);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 10);
}

static void test_format_penalty_number(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	char buf[16];
	scoreboard_format_penalty_number(0, true, buf, sizeof(buf));
	assert(strcmp(buf, "#12") == 0);
}

static void test_format_penalty_number_inactive(void)
{
	scoreboard_reset_state_for_tests();
	char buf[16];
	buf[0] = 'x';
	scoreboard_format_penalty_number(0, true, buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);
}

static void test_format_penalty_number_invalid_slot(void)
{
	scoreboard_reset_state_for_tests();
	char buf[16];
	buf[0] = 'x';
	scoreboard_format_penalty_number(-1, true, buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);
	scoreboard_format_penalty_number(SCOREBOARD_MAX_PENALTIES, true, buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);
}

static void test_format_penalty_number_null(void)
{
	scoreboard_format_penalty_number(0, true, NULL, 0);
	char buf[2];
	scoreboard_format_penalty_number(0, true, buf, 0);
}

static void test_format_penalty_number_away(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(5, 60);
	char buf[16];
	scoreboard_format_penalty_number(0, false, buf, sizeof(buf));
	assert(strcmp(buf, "#5") == 0);
}

static void test_format_penalty_time(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	char buf[16];
	scoreboard_format_penalty_time(0, true, buf, sizeof(buf));
	assert(strcmp(buf, "2:00") == 0);
}

static void test_format_penalty_time_partial(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 90);
	scoreboard_penalty_tick(5);
	char buf[16];
	scoreboard_format_penalty_time(0, true, buf, sizeof(buf));
	assert(strcmp(buf, "1:29") == 0);
}

static void test_format_penalty_time_inactive(void)
{
	scoreboard_reset_state_for_tests();
	char buf[16];
	buf[0] = 'x';
	scoreboard_format_penalty_time(0, true, buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);
}

static void test_format_penalty_time_invalid_slot(void)
{
	scoreboard_reset_state_for_tests();
	char buf[16];
	buf[0] = 'x';
	scoreboard_format_penalty_time(-1, true, buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);
	scoreboard_format_penalty_time(SCOREBOARD_MAX_PENALTIES, false, buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);
}

static void test_format_penalty_time_null(void)
{
	scoreboard_format_penalty_time(0, true, NULL, 0);
	char buf[2];
	scoreboard_format_penalty_time(0, true, buf, 0);
}

static void test_format_penalty_time_away(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(22, 60);
	char buf[16];
	scoreboard_format_penalty_time(0, false, buf, sizeof(buf));
	assert(strcmp(buf, "1:00") == 0);
}

static void test_home_penalty_count(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_penalty_count() == 0);
	scoreboard_home_penalty_add(12, 120);
	assert(scoreboard_get_home_penalty_count() == 1);
	scoreboard_home_penalty_add(7, 60);
	assert(scoreboard_get_home_penalty_count() == 2);
	scoreboard_home_penalty_clear(0);
	assert(scoreboard_get_home_penalty_count() == 1);
}

static void test_away_penalty_count(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_away_penalty_count() == 0);
	scoreboard_away_penalty_add(22, 120);
	assert(scoreboard_get_away_penalty_count() == 1);
	scoreboard_away_penalty_add(15, 60);
	scoreboard_away_penalty_add(9, 300);
	assert(scoreboard_get_away_penalty_count() == 3);
}

static void test_format_penalty_number_zero(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(0, 120);
	char buf[16];
	scoreboard_format_penalty_number(0, true, buf, sizeof(buf));
	assert(strcmp(buf, " ") == 0);
}

static void test_default_penalty_duration(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_default_penalty_duration() == 120);
	scoreboard_set_default_penalty_duration(300);
	assert(scoreboard_get_default_penalty_duration() == 300);
	scoreboard_set_default_penalty_duration(0);
	assert(scoreboard_get_default_penalty_duration() == 1);
}

static void test_default_major_penalty_duration(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_default_major_penalty_duration() == 300);
	scoreboard_set_default_major_penalty_duration(600);
	assert(scoreboard_get_default_major_penalty_duration() == 600);
	scoreboard_set_default_major_penalty_duration(0);
	assert(scoreboard_get_default_major_penalty_duration() == 1);
}

static void test_format_all_penalty_numbers(void)
{
	scoreboard_reset_state_for_tests();
	char buf[256];

	/* empty when no penalties */
	scoreboard_format_all_penalty_numbers(true, buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);

	/* single penalty */
	scoreboard_home_penalty_add(12, 120);
	scoreboard_format_all_penalty_numbers(true, buf, sizeof(buf));
	assert(strcmp(buf, "#12") == 0);

	/* two penalties — newline separated, slot order */
	scoreboard_home_penalty_add(7, 60);
	scoreboard_format_all_penalty_numbers(true, buf, sizeof(buf));
	assert(strcmp(buf, "#12\n#7") == 0);

	/* clear first — second remains, no gap */
	scoreboard_home_penalty_clear(0);
	scoreboard_format_all_penalty_numbers(true, buf, sizeof(buf));
	assert(strcmp(buf, "#7") == 0);

	/* player number 0 shows space */
	scoreboard_home_penalty_add(0, 120);
	scoreboard_format_all_penalty_numbers(true, buf, sizeof(buf));
	assert(strcmp(buf, " \n#7") == 0);
}

static void test_format_all_penalty_numbers_away(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(22, 120);
	scoreboard_away_penalty_add(15, 60);
	char buf[256];
	scoreboard_format_all_penalty_numbers(false, buf, sizeof(buf));
	assert(strcmp(buf, "#22\n#15") == 0);
}

static void test_format_all_penalty_numbers_null(void)
{
	scoreboard_format_all_penalty_numbers(true, NULL, 0);
	char buf[2];
	scoreboard_format_all_penalty_numbers(true, buf, 0);
}

static void test_format_all_penalty_times(void)
{
	scoreboard_reset_state_for_tests();
	char buf[256];

	/* empty when no penalties */
	scoreboard_format_all_penalty_times(true, buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);

	/* single penalty */
	scoreboard_home_penalty_add(12, 120);
	scoreboard_format_all_penalty_times(true, buf, sizeof(buf));
	assert(strcmp(buf, "2:00") == 0);

	/* two penalties — newline separated */
	scoreboard_home_penalty_add(7, 90);
	scoreboard_format_all_penalty_times(true, buf, sizeof(buf));
	assert(strcmp(buf, "2:00\n1:30") == 0);

	/* clear first — second remains */
	scoreboard_home_penalty_clear(0);
	scoreboard_format_all_penalty_times(true, buf, sizeof(buf));
	assert(strcmp(buf, "1:30") == 0);
}

static void test_format_all_penalty_times_away(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(22, 300);
	scoreboard_away_penalty_add(15, 60);
	char buf[256];
	scoreboard_format_all_penalty_times(false, buf, sizeof(buf));
	assert(strcmp(buf, "5:00\n1:00") == 0);
}

static void test_format_all_penalty_times_null(void)
{
	scoreboard_format_all_penalty_times(true, NULL, 0);
	char buf[2];
	scoreboard_format_all_penalty_times(true, buf, 0);
}

static void test_format_all_penalties_order_stable(void)
{
	/* Add 3, format only shows first 2 running */
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add(20, 60);
	scoreboard_home_penalty_add(30, 300);

	char nums[256], times[256];
	scoreboard_format_all_penalty_numbers(true, nums, sizeof(nums));
	scoreboard_format_all_penalty_times(true, times, sizeof(times));
	assert(strcmp(nums, "#10\n#20") == 0);
	assert(strcmp(times, "2:00\n1:00") == 0);

	/* clear first slot — third promotes to running */
	scoreboard_home_penalty_clear(0);
	scoreboard_format_all_penalty_numbers(true, nums, sizeof(nums));
	scoreboard_format_all_penalty_times(true, times, sizeof(times));
	assert(strcmp(nums, "#20\n#30") == 0);
	assert(strcmp(times, "1:00\n5:00") == 0);

	/* clear second — only third remains */
	scoreboard_home_penalty_clear(1);
	scoreboard_format_all_penalty_numbers(true, nums, sizeof(nums));
	scoreboard_format_all_penalty_times(true, times, sizeof(times));
	assert(strcmp(nums, "#30") == 0);
	assert(strcmp(times, "5:00") == 0);
}

static void test_format_all_penalty_numbers_small_buf(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_home_penalty_add(7, 60);
	/* buffer too small for both — only first fits */
	char buf[5]; /* "#12\0" = 4 chars, no room for \n#7 */
	scoreboard_format_all_penalty_numbers(true, buf, sizeof(buf));
	assert(strcmp(buf, "#12") == 0);
}

static void test_format_all_penalty_times_small_buf(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_home_penalty_add(7, 60);
	/* buffer too small for both */
	char buf[5]; /* "2:00\0" = 5 chars, no room for \n1:30 */
	scoreboard_format_all_penalty_times(true, buf, sizeof(buf));
	assert(strcmp(buf, "2:00") == 0);
}

static void test_dirty_home_penalty_add(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	assert(scoreboard_is_dirty());
}

static void test_dirty_home_penalty_clear(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	/* penalty_add sets dirty; clear also sets dirty */
	scoreboard_home_penalty_clear(0);
	assert(scoreboard_is_dirty());
}

static void test_dirty_away_penalty_add(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(22, 120);
	assert(scoreboard_is_dirty());
}

static void test_dirty_away_penalty_clear(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(22, 120);
	scoreboard_away_penalty_clear(0);
	assert(scoreboard_is_dirty());
}

static void test_dirty_penalty_tick_active(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_set_output_directory("/tmp");
	scoreboard_mark_dirty();
	scoreboard_write_all_files();
	assert(!scoreboard_is_dirty());
	scoreboard_penalty_tick(10);
	assert(scoreboard_is_dirty());
}

static void test_dirty_penalty_tick_no_active(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_is_dirty());
	scoreboard_penalty_tick(10);
	assert(!scoreboard_is_dirty());
}

static void test_dirty_penalty_adjust(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_set_output_directory("/tmp");
	scoreboard_mark_dirty();
	scoreboard_write_all_files();
	assert(!scoreboard_is_dirty());
	scoreboard_penalty_adjust(100);
	assert(scoreboard_is_dirty());
}

static void test_only_two_penalties_tick(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120); /* slot 0: 1200 tenths */
	scoreboard_home_penalty_add(20, 120); /* slot 1: 1200 tenths */
	scoreboard_home_penalty_add(30, 120); /* slot 2: 1200 tenths (queued) */

	scoreboard_penalty_tick(10);

	/* First 2 should tick */
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1190);
	assert(scoreboard_get_home_penalty(1)->remaining_tenths == 1190);
	/* Third should NOT tick — it's queued */
	assert(scoreboard_get_home_penalty(2)->remaining_tenths == 1200);
}

static void test_only_two_away_penalties_tick(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(10, 120);
	scoreboard_away_penalty_add(20, 120);
	scoreboard_away_penalty_add(30, 120);

	scoreboard_penalty_tick(10);

	assert(scoreboard_get_away_penalty(0)->remaining_tenths == 1190);
	assert(scoreboard_get_away_penalty(1)->remaining_tenths == 1190);
	assert(scoreboard_get_away_penalty(2)->remaining_tenths == 1200);
}

static void test_queued_penalty_starts_after_expiry(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 1);   /* slot 0: 10 tenths */
	scoreboard_home_penalty_add(20, 120); /* slot 1: 1200 tenths */
	scoreboard_home_penalty_add(30, 120); /* slot 2: queued */

	/* Tick enough to expire slot 0 */
	scoreboard_penalty_tick(10);

	/* After compact: slot 1 (#20) moved to slot 0, slot 2 (#30) to slot 1 */
	assert(scoreboard_get_home_penalty(0)->player_number == 20);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1190);
	/* Slot 1 (was queued) didn't tick this round */
	assert(scoreboard_get_home_penalty(1)->player_number == 30);
	assert(scoreboard_get_home_penalty(1)->remaining_tenths == 1200);
	/* Slot 2 now empty */
	assert(!scoreboard_get_home_penalty(2)->active);

	/* Next tick: both slot 0 and slot 1 are now running */
	scoreboard_penalty_tick(10);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1180);
	assert(scoreboard_get_home_penalty(1)->remaining_tenths == 1190);
}

static void test_penalty_adjust_only_running(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120); /* slot 0: 1200 tenths */
	scoreboard_home_penalty_add(20, 120); /* slot 1: 1200 tenths */
	scoreboard_home_penalty_add(30, 120); /* slot 2: queued */

	scoreboard_penalty_adjust(-100);

	/* First 2 adjusted */
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1100);
	assert(scoreboard_get_home_penalty(1)->remaining_tenths == 1100);
	/* Queued penalty NOT adjusted */
	assert(scoreboard_get_home_penalty(2)->remaining_tenths == 1200);
}

static void test_format_only_running_penalties(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add(20, 60);
	scoreboard_home_penalty_add(30, 300);

	char nums[256], times[256];
	/* Format should only show first 2 running penalties */
	scoreboard_format_all_penalty_numbers(true, nums, sizeof(nums));
	scoreboard_format_all_penalty_times(true, times, sizeof(times));
	assert(strcmp(nums, "#10\n#20") == 0);
	assert(strcmp(times, "2:00\n1:00") == 0);
}

/* ---- compact tests ---- */

static void test_compact_fills_gap(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add(20, 60);
	scoreboard_home_penalty_add(30, 300);
	scoreboard_home_penalty_clear(0);
	scoreboard_penalty_compact();

	assert(scoreboard_get_home_penalty(0)->active);
	assert(scoreboard_get_home_penalty(0)->player_number == 20);
	assert(scoreboard_get_home_penalty(1)->active);
	assert(scoreboard_get_home_penalty(1)->player_number == 30);
	assert(!scoreboard_get_home_penalty(2)->active);
}

static void test_compact_no_gaps(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add(20, 60);
	scoreboard_home_penalty_add(30, 300);
	scoreboard_penalty_compact();

	assert(scoreboard_get_home_penalty(0)->player_number == 10);
	assert(scoreboard_get_home_penalty(1)->player_number == 20);
	assert(scoreboard_get_home_penalty(2)->player_number == 30);
}

static void test_compact_empty(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_penalty_compact();
	assert(scoreboard_get_home_penalty_count() == 0);
	assert(scoreboard_get_away_penalty_count() == 0);
}

static void test_compact_multiple_gaps(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add(20, 60);
	scoreboard_home_penalty_add(30, 300);
	scoreboard_home_penalty_add(40, 120);
	scoreboard_home_penalty_clear(0);
	scoreboard_home_penalty_clear(2);
	scoreboard_penalty_compact();

	assert(scoreboard_get_home_penalty(0)->player_number == 20);
	assert(scoreboard_get_home_penalty(1)->player_number == 40);
	assert(!scoreboard_get_home_penalty(2)->active);
	assert(!scoreboard_get_home_penalty(3)->active);
}

static void test_compact_away(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(10, 120);
	scoreboard_away_penalty_add(20, 60);
	scoreboard_away_penalty_clear(0);
	scoreboard_penalty_compact();

	assert(scoreboard_get_away_penalty(0)->player_number == 20);
	assert(!scoreboard_get_away_penalty(1)->active);
}

static void test_tick_expire_compacts(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 1);   /* 10 tenths — expires in 1 tick */
	scoreboard_home_penalty_add(20, 120); /* running */
	scoreboard_home_penalty_add(30, 120); /* queued */

	scoreboard_penalty_tick(10);

	/* After expire + compact: #20 in slot 0, #30 in slot 1 */
	assert(scoreboard_get_home_penalty(0)->player_number == 20);
	assert(scoreboard_get_home_penalty(1)->player_number == 30);
	assert(!scoreboard_get_home_penalty(2)->active);
}

static void test_adjust_expire_compacts(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 5);   /* 50 tenths */
	scoreboard_home_penalty_add(20, 120); /* running */
	scoreboard_home_penalty_add(30, 120); /* queued */

	scoreboard_penalty_adjust(-50);

	/* #10 expired + compact: #20 in slot 0, #30 in slot 1 */
	assert(scoreboard_get_home_penalty(0)->player_number == 20);
	assert(scoreboard_get_home_penalty(1)->player_number == 30);
	assert(!scoreboard_get_home_penalty(2)->active);
}

static void test_compact_preserves_phase2(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add_compound(20, 120, 120);
	scoreboard_home_penalty_clear(0);
	scoreboard_penalty_compact();

	assert(scoreboard_get_home_penalty(0)->player_number == 20);
	assert(scoreboard_get_home_penalty(0)->phase2_tenths == 1200);
}

/* ---- set_time tests ---- */

static void test_home_penalty_set_time(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_home_penalty_set_time(0, 60);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 600);
}

static void test_away_penalty_set_time(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(22, 120);
	scoreboard_away_penalty_set_time(0, 60);
	assert(scoreboard_get_away_penalty(0)->remaining_tenths == 600);
}

static void test_penalty_set_time_invalid_slot(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_set_time(-1, 60);
	scoreboard_home_penalty_set_time(SCOREBOARD_MAX_PENALTIES, 60);
	scoreboard_away_penalty_set_time(-1, 60);
	scoreboard_away_penalty_set_time(SCOREBOARD_MAX_PENALTIES, 60);
}

static void test_penalty_set_time_inactive_slot(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_set_time(0, 60);
	assert(!scoreboard_get_home_penalty(0)->active);
}

static void test_penalty_set_time_zero_clears(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_home_penalty_add(20, 60);
	scoreboard_home_penalty_set_time(0, 0);
	/* After clear + compact: #20 moves to slot 0 */
	assert(scoreboard_get_home_penalty(0)->player_number == 20);
	assert(!scoreboard_get_home_penalty(1)->active);
}

static void test_dirty_penalty_set_time(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_set_output_directory("/tmp");
	scoreboard_mark_dirty();
	scoreboard_write_all_files();
	assert(!scoreboard_is_dirty());
	scoreboard_home_penalty_set_time(0, 60);
	assert(scoreboard_is_dirty());
}

/* ---- compound penalty tests ---- */

static void test_compound_add_home(void)
{
	scoreboard_reset_state_for_tests();
	int slot = scoreboard_home_penalty_add_compound(12, 120, 120);
	assert(slot == 0);
	const struct scoreboard_penalty *p = scoreboard_get_home_penalty(0);
	assert(p->active);
	assert(p->player_number == 12);
	assert(p->remaining_tenths == 1200);
	assert(p->phase2_tenths == 1200);
}

static void test_compound_add_away(void)
{
	scoreboard_reset_state_for_tests();
	int slot = scoreboard_away_penalty_add_compound(22, 120, 300);
	assert(slot == 0);
	const struct scoreboard_penalty *p = scoreboard_get_away_penalty(0);
	assert(p->active);
	assert(p->player_number == 22);
	assert(p->remaining_tenths == 1200);
	assert(p->phase2_tenths == 3000);
}

static void test_compound_add_full(void)
{
	scoreboard_reset_state_for_tests();
	for (int i = 0; i < SCOREBOARD_MAX_PENALTIES; i++)
		scoreboard_home_penalty_add(i + 1, 120);
	assert(scoreboard_home_penalty_add_compound(99, 120, 120) == -1);
}

static void test_compound_phase_transition(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add_compound(12, 120, 120);

	/* Tick all of phase 1 */
	scoreboard_penalty_tick(1200);

	/* Penalty should still be active — now in phase 2 */
	const struct scoreboard_penalty *p = scoreboard_get_home_penalty(0);
	assert(p->active);
	assert(p->remaining_tenths == 1200);
	assert(p->phase2_tenths == 0);
}

static void test_compound_phase2_expires(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add_compound(12, 120, 120);

	/* Tick through both phases */
	scoreboard_penalty_tick(1200);
	scoreboard_penalty_tick(1200);

	assert(!scoreboard_get_home_penalty(0)->active);
}

static void test_compound_holds_slot(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add_compound(12, 120, 120); /* slot 0 */
	scoreboard_home_penalty_add(20, 60);                /* slot 1 */

	/* Tick to expire phase 1 of compound */
	scoreboard_penalty_tick(1200);

	/* Compound still in slot 0, regular in slot 1 expired */
	assert(scoreboard_get_home_penalty(0)->active);
	assert(scoreboard_get_home_penalty(0)->player_number == 12);
	assert(!scoreboard_get_home_penalty(1)->active);
}

static void test_compound_clear_removes_both(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add_compound(12, 120, 120);
	scoreboard_home_penalty_clear(0);
	assert(!scoreboard_get_home_penalty(0)->active);
	assert(scoreboard_get_home_penalty(0)->phase2_tenths == 0);
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 0);
}

static void test_compound_adjust_phase_transition(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add_compound(12, 5, 120);

	scoreboard_penalty_adjust(-50);

	const struct scoreboard_penalty *p = scoreboard_get_home_penalty(0);
	assert(p->active);
	assert(p->remaining_tenths == 1200);
	assert(p->phase2_tenths == 0);
}

static void test_compound_away_phase_transition(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add_compound(22, 120, 300);

	scoreboard_penalty_tick(1200);

	const struct scoreboard_penalty *p = scoreboard_get_away_penalty(0);
	assert(p->active);
	assert(p->remaining_tenths == 3000);
	assert(p->phase2_tenths == 0);
}

static void test_compound_away_adjust_transition(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add_compound(22, 5, 120);

	scoreboard_penalty_adjust(-50);

	const struct scoreboard_penalty *p = scoreboard_get_away_penalty(0);
	assert(p->active);
	assert(p->remaining_tenths == 1200);
	assert(p->phase2_tenths == 0);
}

static void test_compound_counts_as_one_running(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add_compound(12, 120, 120); /* running */
	scoreboard_home_penalty_add(20, 120);               /* running */
	scoreboard_home_penalty_add(30, 120);               /* queued */

	scoreboard_penalty_tick(10);

	/* Compound and #20 ticked, #30 queued */
	assert(scoreboard_get_home_penalty(0)->remaining_tenths == 1190);
	assert(scoreboard_get_home_penalty(1)->remaining_tenths == 1190);
	assert(scoreboard_get_home_penalty(2)->remaining_tenths == 1200);
}

static void test_compound_new_game_clears_phase2(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add_compound(12, 120, 120);
	scoreboard_new_game();
	assert(!scoreboard_get_home_penalty(0)->active);
	assert(scoreboard_get_home_penalty(0)->phase2_tenths == 0);
}

static void test_compound_add_away_full(void)
{
	scoreboard_reset_state_for_tests();
	for (int i = 0; i < SCOREBOARD_MAX_PENALTIES; i++)
		scoreboard_away_penalty_add(i + 1, 120);
	assert(scoreboard_away_penalty_add_compound(99, 120, 120) == -1);
}

static void test_away_penalty_set_time_inactive(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_set_time(0, 60);
	assert(!scoreboard_get_away_penalty(0)->active);
}

static void test_away_penalty_set_time_zero_clears(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add(22, 120);
	scoreboard_away_penalty_add(15, 60);
	scoreboard_away_penalty_set_time(0, 0);
	/* After clear + compact: #15 moves to slot 0 */
	assert(scoreboard_get_away_penalty(0)->player_number == 15);
	assert(!scoreboard_get_away_penalty(1)->active);
}

static void test_set_time_zero_compound_transitions(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add_compound(12, 120, 120);
	scoreboard_home_penalty_set_time(0, 0);
	/* Should transition to phase 2, not clear */
	const struct scoreboard_penalty *p = scoreboard_get_home_penalty(0);
	assert(p->active);
	assert(p->remaining_tenths == 1200);
	assert(p->phase2_tenths == 0);
}

static void test_set_time_zero_compound_away_transitions(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_away_penalty_add_compound(22, 120, 300);
	scoreboard_away_penalty_set_time(0, 0);
	const struct scoreboard_penalty *p = scoreboard_get_away_penalty(0);
	assert(p->active);
	assert(p->remaining_tenths == 3000);
	assert(p->phase2_tenths == 0);
}

static void test_set_time_zero_regular_still_clears(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_home_penalty_set_time(0, 0);
	assert(!scoreboard_get_home_penalty(0)->active);
}

static void test_regular_penalty_has_zero_phase2(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(12, 120);
	assert(scoreboard_get_home_penalty(0)->phase2_tenths == 0);
}

int main(void)
{
	test_home_penalty_add();
	test_home_penalty_add_second_slot();
	test_home_penalty_full();
	test_home_penalty_clear();
	test_home_penalty_clear_invalid();
	test_home_penalty_get_invalid();
	test_away_penalty_add();
	test_away_penalty_add_second_slot();
	test_away_penalty_full();
	test_away_penalty_clear();
	test_away_penalty_clear_invalid();
	test_away_penalty_get_invalid();
	test_penalty_tick();
	test_penalty_tick_expire();
	test_penalty_tick_no_active();
	test_clock_tick_drives_penalty_tick();
	test_format_penalty_number();
	test_format_penalty_number_inactive();
	test_format_penalty_number_invalid_slot();
	test_format_penalty_number_null();
	test_format_penalty_number_away();
	test_format_penalty_time();
	test_format_penalty_time_partial();
	test_format_penalty_time_inactive();
	test_format_penalty_time_invalid_slot();
	test_format_penalty_time_null();
	test_format_penalty_time_away();
	test_home_penalty_count();
	test_away_penalty_count();
	test_format_penalty_number_zero();
	test_default_penalty_duration();
	test_default_major_penalty_duration();
	test_format_all_penalty_numbers();
	test_format_all_penalty_numbers_away();
	test_format_all_penalty_numbers_null();
	test_format_all_penalty_times();
	test_format_all_penalty_times_away();
	test_format_all_penalty_times_null();
	test_format_all_penalties_order_stable();
	test_format_all_penalty_numbers_small_buf();
	test_format_all_penalty_times_small_buf();
	test_dirty_home_penalty_add();
	test_dirty_home_penalty_clear();
	test_dirty_away_penalty_add();
	test_dirty_away_penalty_clear();
	test_dirty_penalty_tick_active();
	test_dirty_penalty_tick_no_active();
	test_dirty_penalty_adjust();
	test_only_two_penalties_tick();
	test_only_two_away_penalties_tick();
	test_queued_penalty_starts_after_expiry();
	test_penalty_adjust_only_running();
	test_format_only_running_penalties();

	/* compact */
	test_compact_fills_gap();
	test_compact_no_gaps();
	test_compact_empty();
	test_compact_multiple_gaps();
	test_compact_away();
	test_tick_expire_compacts();
	test_adjust_expire_compacts();
	test_compact_preserves_phase2();

	/* set_time */
	test_home_penalty_set_time();
	test_away_penalty_set_time();
	test_penalty_set_time_invalid_slot();
	test_penalty_set_time_inactive_slot();
	test_penalty_set_time_zero_clears();
	test_dirty_penalty_set_time();

	/* compound penalties */
	test_compound_add_home();
	test_compound_add_away();
	test_compound_add_full();
	test_compound_phase_transition();
	test_compound_phase2_expires();
	test_compound_holds_slot();
	test_compound_clear_removes_both();
	test_compound_adjust_phase_transition();
	test_compound_away_phase_transition();
	test_compound_away_adjust_transition();
	test_compound_counts_as_one_running();
	test_compound_new_game_clears_phase2();
	test_compound_add_away_full();
	test_away_penalty_set_time_inactive();
	test_away_penalty_set_time_zero_clears();
	test_set_time_zero_compound_transitions();
	test_set_time_zero_compound_away_transitions();
	test_set_time_zero_regular_still_clears();
	test_regular_penalty_has_zero_phase2();

	printf("All scoreboard-core penalty tests passed.\n");
	return 0;
}
