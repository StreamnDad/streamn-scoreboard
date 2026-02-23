#include "scoreboard-core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_home_score(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_score() == 0);
	scoreboard_increment_home_score();
	assert(scoreboard_get_home_score() == 1);
	scoreboard_increment_home_score();
	assert(scoreboard_get_home_score() == 2);
	scoreboard_decrement_home_score();
	assert(scoreboard_get_home_score() == 1);
}

static void test_home_score_floor(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_decrement_home_score();
	assert(scoreboard_get_home_score() == 0);
}

static void test_home_score_set(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_score(5);
	assert(scoreboard_get_home_score() == 5);
	scoreboard_set_home_score(-1);
	assert(scoreboard_get_home_score() == 0);
}

static void test_away_score(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_away_score() == 0);
	scoreboard_increment_away_score();
	assert(scoreboard_get_away_score() == 1);
	scoreboard_increment_away_score();
	assert(scoreboard_get_away_score() == 2);
	scoreboard_decrement_away_score();
	assert(scoreboard_get_away_score() == 1);
}

static void test_away_score_floor(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_decrement_away_score();
	assert(scoreboard_get_away_score() == 0);
}

static void test_away_score_set(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_away_score(3);
	assert(scoreboard_get_away_score() == 3);
	scoreboard_set_away_score(-2);
	assert(scoreboard_get_away_score() == 0);
}

static void test_home_shots(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_shots() == 0);
	scoreboard_increment_home_shots();
	assert(scoreboard_get_home_shots() == 1);
	scoreboard_increment_home_shots();
	assert(scoreboard_get_home_shots() == 2);
	scoreboard_decrement_home_shots();
	assert(scoreboard_get_home_shots() == 1);
}

static void test_home_shots_floor(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_decrement_home_shots();
	assert(scoreboard_get_home_shots() == 0);
}

static void test_home_shots_set(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_shots(10);
	assert(scoreboard_get_home_shots() == 10);
	scoreboard_set_home_shots(-3);
	assert(scoreboard_get_home_shots() == 0);
}

static void test_away_shots(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_away_shots() == 0);
	scoreboard_increment_away_shots();
	assert(scoreboard_get_away_shots() == 1);
	scoreboard_decrement_away_shots();
	assert(scoreboard_get_away_shots() == 0);
}

static void test_away_shots_floor(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_decrement_away_shots();
	assert(scoreboard_get_away_shots() == 0);
}

static void test_away_shots_set(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_away_shots(7);
	assert(scoreboard_get_away_shots() == 7);
	scoreboard_set_away_shots(-1);
	assert(scoreboard_get_away_shots() == 0);
}

static void test_home_name(void)
{
	scoreboard_reset_state_for_tests();
	assert(strcmp(scoreboard_get_home_name(), "Home") == 0);
	scoreboard_set_home_name("Eagles");
	assert(strcmp(scoreboard_get_home_name(), "Eagles") == 0);
}

static void test_away_name(void)
{
	scoreboard_reset_state_for_tests();
	assert(strcmp(scoreboard_get_away_name(), "Away") == 0);
	scoreboard_set_away_name("Hawks");
	assert(strcmp(scoreboard_get_away_name(), "Hawks") == 0);
}

static void test_name_null(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_name(NULL);
	assert(strcmp(scoreboard_get_home_name(), "") == 0);
	scoreboard_set_away_name(NULL);
	assert(strcmp(scoreboard_get_away_name(), "") == 0);
}

static void test_new_game(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_name("Eagles");
	scoreboard_set_away_name("Hawks");
	scoreboard_set_home_score(3);
	scoreboard_set_away_score(2);
	scoreboard_set_home_shots(25);
	scoreboard_set_away_shots(18);
	scoreboard_set_period(3);
	scoreboard_clock_start();
	scoreboard_clock_tick(100);
	scoreboard_home_penalty_add(12, 120);

	scoreboard_new_game();

	assert(scoreboard_get_home_score() == 0);
	assert(scoreboard_get_away_score() == 0);
	assert(scoreboard_get_home_shots() == 0);
	assert(scoreboard_get_away_shots() == 0);
	assert(scoreboard_get_period() == 1);
	assert(!scoreboard_clock_is_running());
	assert(scoreboard_clock_get_tenths() ==
	       scoreboard_get_period_length() * 10);
	/* names preserved */
	assert(strcmp(scoreboard_get_home_name(), "Eagles") == 0);
	assert(strcmp(scoreboard_get_away_name(), "Hawks") == 0);
	/* penalties cleared */
	assert(!scoreboard_get_home_penalty(0)->active);
}

static void test_name_truncation(void)
{
	scoreboard_reset_state_for_tests();
	/* 65 bytes = SCOREBOARD_MAX_NAME, so a 70-char string exercises
	   the safe_copy truncation path (len >= dst_size). */
	const char *long_name =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789-extra-chars!!";
	assert(strlen(long_name) > 64);
	scoreboard_set_home_name(long_name);
	/* name must be truncated to 64 chars */
	assert(strlen(scoreboard_get_home_name()) == 64);
}

static void test_new_game_countup(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_clock_direction(SCOREBOARD_CLOCK_COUNT_UP);
	scoreboard_clock_set_tenths(5000);
	scoreboard_new_game();
	assert(scoreboard_clock_get_tenths() == 0);
}

int main(void)
{
	test_home_score();
	test_home_score_floor();
	test_home_score_set();
	test_away_score();
	test_away_score_floor();
	test_away_score_set();
	test_home_shots();
	test_home_shots_floor();
	test_home_shots_set();
	test_away_shots();
	test_away_shots_floor();
	test_away_shots_set();
	test_home_name();
	test_away_name();
	test_name_null();
	test_name_truncation();
	test_new_game();
	test_new_game_countup();

	printf("All scoreboard-core scoring tests passed.\n");
	return 0;
}
