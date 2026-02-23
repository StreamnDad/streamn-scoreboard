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
	/* Add 3, clear middle, verify remaining order is stable */
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add(20, 60);
	scoreboard_home_penalty_add(30, 300);

	char nums[256], times[256];
	scoreboard_format_all_penalty_numbers(true, nums, sizeof(nums));
	scoreboard_format_all_penalty_times(true, times, sizeof(times));
	assert(strcmp(nums, "#10\n#20\n#30") == 0);
	assert(strcmp(times, "2:00\n1:00\n5:00") == 0);

	/* clear middle slot */
	scoreboard_home_penalty_clear(1);
	scoreboard_format_all_penalty_numbers(true, nums, sizeof(nums));
	scoreboard_format_all_penalty_times(true, times, sizeof(times));
	assert(strcmp(nums, "#10\n#30") == 0);
	assert(strcmp(times, "2:00\n5:00") == 0);

	/* numbers and times stay paired */
	scoreboard_home_penalty_clear(0);
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
	test_format_all_penalty_numbers();
	test_format_all_penalty_numbers_away();
	test_format_all_penalty_numbers_null();
	test_format_all_penalty_times();
	test_format_all_penalty_times_away();
	test_format_all_penalty_times_null();
	test_format_all_penalties_order_stable();
	test_format_all_penalty_numbers_small_buf();
	test_format_all_penalty_times_small_buf();

	printf("All scoreboard-core penalty tests passed.\n");
	return 0;
}
