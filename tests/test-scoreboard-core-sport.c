#include "scoreboard-core.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#define getpid() _getpid()
#else
#include <unistd.h>
#endif

static int g_tmp_counter = 0;

static void make_tmp_dir(char *buf, size_t size)
{
#ifdef _WIN32
	snprintf(buf, size, "%s\\sb_sport_%d_%d",
		 getenv("TEMP") ? getenv("TEMP") : ".", (int)getpid(),
		 g_tmp_counter++);
#else
	snprintf(buf, size, "/tmp/sb_sport_%d_%d", (int)getpid(),
		 g_tmp_counter++);
#endif
	mkdir(buf, 0755);
}

static void make_tmp_file(char *buf, size_t size)
{
#ifdef _WIN32
	snprintf(buf, size, "%s\\sb_sport_%d_%d.json",
		 getenv("TEMP") ? getenv("TEMP") : ".", (int)getpid(),
		 g_tmp_counter++);
#else
	snprintf(buf, size, "/tmp/sb_sport_%d_%d.json", (int)getpid(),
		 g_tmp_counter++);
#endif
}

static void cleanup_dir(const char *dir)
{
	char cmd[512];
#ifdef _WIN32
	snprintf(cmd, sizeof(cmd), "rmdir /S /Q \"%s\"", dir);
#else
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
#endif
	system(cmd);
}

static void test_default_sport_is_hockey(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_HOCKEY);
	assert(strcmp(scoreboard_get_segment_name(), "Period") == 0);
	assert(scoreboard_get_has_shots() == true);
	assert(scoreboard_get_has_penalties() == true);
}

static void test_set_sport_hockey(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	const struct scoreboard_sport_preset *p = scoreboard_get_sport_preset();
	assert(p->sport == SCOREBOARD_SPORT_HOCKEY);
	assert(strcmp(p->segment_name, "Period") == 0);
	assert(p->segment_count == 3);
	assert(p->duration_seconds == 900);
	assert(p->ot_max == 4);
	assert(p->has_shots == true);
	assert(p->has_penalties == true);
	assert(p->default_direction == SCOREBOARD_CLOCK_COUNT_DOWN);
	assert(p->default_penalty_secs == 120);
	assert(p->default_major_penalty_secs == 300);
	assert(scoreboard_get_default_penalty_duration() == 120);
	assert(scoreboard_get_default_major_penalty_duration() == 300);
}

static void test_set_sport_basketball(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_BASKETBALL);
	assert(strcmp(scoreboard_get_segment_name(), "Quarter") == 0);
	assert(scoreboard_get_has_shots() == false);
	assert(scoreboard_get_has_penalties() == false);
	assert(scoreboard_get_period_length() == 480);
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_DOWN);
}

static void test_set_sport_soccer(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_SOCCER);
	assert(strcmp(scoreboard_get_segment_name(), "Half") == 0);
	assert(scoreboard_get_has_shots() == false);
	assert(scoreboard_get_has_penalties() == false);
	assert(scoreboard_get_period_length() == 2700);
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_UP);
}

static void test_set_sport_football(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_FOOTBALL);
	assert(strcmp(scoreboard_get_segment_name(), "Half") == 0);
	assert(scoreboard_get_has_shots() == false);
	assert(scoreboard_get_has_penalties() == false);
	assert(scoreboard_get_period_length() == 1800);
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_DOWN);
}

static void test_set_sport_lacrosse(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_LACROSSE);
	assert(strcmp(scoreboard_get_segment_name(), "Quarter") == 0);
	assert(scoreboard_get_has_shots() == true);
	assert(scoreboard_get_has_penalties() == true);
	assert(scoreboard_get_period_length() == 720);
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_DOWN);
	assert(scoreboard_get_default_penalty_duration() == 60);
	assert(scoreboard_get_default_major_penalty_duration() == 180);
}

static void test_set_sport_generic(void)
{
	scoreboard_reset_state_for_tests();
	int old_period_length = scoreboard_get_period_length();
	scoreboard_set_sport(SCOREBOARD_SPORT_GENERIC);
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_GENERIC);
	assert(strcmp(scoreboard_get_segment_name(), "Segment") == 0);
	assert(scoreboard_get_has_shots() == false);
	assert(scoreboard_get_has_penalties() == false);
	/* Generic has duration_seconds=0 so period_length should be unchanged */
	assert(scoreboard_get_period_length() == old_period_length);
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_UP);
}

static void test_set_sport_invalid(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport((enum scoreboard_sport)99);
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_HOCKEY);
}

static void test_sport_name_round_trip(void)
{
	for (int i = 0; i < SCOREBOARD_SPORT_COUNT; i++) {
		const char *name =
			scoreboard_sport_name((enum scoreboard_sport)i);
		assert(name != NULL);
		assert(strlen(name) > 0);
		enum scoreboard_sport parsed = scoreboard_sport_from_name(name);
		assert(parsed == (enum scoreboard_sport)i);
	}
}

static void test_sport_from_name_null(void)
{
	assert(scoreboard_sport_from_name(NULL) == SCOREBOARD_SPORT_HOCKEY);
}

static void test_sport_from_name_unknown(void)
{
	assert(scoreboard_sport_from_name("curling") ==
	       SCOREBOARD_SPORT_HOCKEY);
	assert(scoreboard_sport_from_name("") == SCOREBOARD_SPORT_HOCKEY);
}

static void test_sport_name_invalid_enum(void)
{
	const char *name = scoreboard_sport_name((enum scoreboard_sport)-1);
	assert(strcmp(name, "hockey") == 0);
	name = scoreboard_sport_name((enum scoreboard_sport)99);
	assert(strcmp(name, "hockey") == 0);
}

static void test_format_period_basketball(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	char buf[16];

	scoreboard_set_period(4);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "4") == 0);

	scoreboard_set_period(5);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "OT") == 0);
}

static void test_format_period_soccer(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	char buf[16];

	scoreboard_set_period(2);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "2") == 0);

	scoreboard_set_period(3);
	scoreboard_format_period(buf, sizeof(buf));
	assert(strcmp(buf, "OT") == 0);
}

static void test_period_clamp_basketball(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	/* max = 4 quarters + 1 OT = 5 */
	scoreboard_set_period(5);
	assert(scoreboard_get_period() == 5);
	scoreboard_set_period(6);
	assert(scoreboard_get_period() == 5);

	/* Without OT: max = 4 */
	scoreboard_set_overtime_enabled(false);
	scoreboard_set_period(5);
	assert(scoreboard_get_period() == 4);
}

static void test_period_advance_soccer(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	/* soccer: 2 halves + 1 OT max */
	assert(scoreboard_get_period() == 1);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 2);
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 3); /* OT */
	scoreboard_period_advance();
	assert(scoreboard_get_period() == 3); /* capped */
}

static void test_parse_period_basketball(void)
{
	/* Verify read_all_files can parse period texts for basketball */
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);

	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	/* Write basketball state: period 5 = OT */
	scoreboard_set_period(5);
	scoreboard_mark_dirty();
	assert(scoreboard_write_all_files());

	/* Read back */
	scoreboard_set_period(1);
	assert(scoreboard_read_all_files());
	assert(scoreboard_get_period() == 5);

	/* Cleanup */
	cleanup_dir(tmpdir);
}

static void test_sport_write_read_files(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);

	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	scoreboard_mark_dirty();
	assert(scoreboard_write_all_files());

	/* Reset to hockey, then read back */
	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_HOCKEY);

	assert(scoreboard_read_all_files());
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_SOCCER);

	/* Cleanup */
	cleanup_dir(tmpdir);
}

static void test_sport_save_load_state(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);

	char tmpfile[256];
	make_tmp_file(tmpfile, sizeof(tmpfile));

	assert(scoreboard_save_state(tmpfile));

	/* Reset to hockey, then load */
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_HOCKEY);

	assert(scoreboard_load_state(tmpfile));
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_LACROSSE);

	remove(tmpfile);
}

static void test_set_sport_marks_dirty(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_is_dirty());
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_is_dirty());
}

static void test_sport_change_preserves_scores(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_score(5);
	scoreboard_set_away_score(3);
	scoreboard_set_home_shots(20);
	scoreboard_clock_set_tenths(1234);
	scoreboard_set_period(2);

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);

	assert(scoreboard_get_home_score() == 5);
	assert(scoreboard_get_away_score() == 3);
	assert(scoreboard_get_home_shots() == 20);
	assert(scoreboard_clock_get_tenths() == 1234);
	assert(scoreboard_get_period() == 2);
}

static void test_get_sport_preset(void)
{
	scoreboard_reset_state_for_tests();
	const struct scoreboard_sport_preset *p = scoreboard_get_sport_preset();
	assert(p != NULL);
	assert(p->sport == SCOREBOARD_SPORT_HOCKEY);
	assert(p->segment_count == 3);
}

static void test_set_sport_rugby(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_RUGBY);
	assert(scoreboard_get_sport() == SCOREBOARD_SPORT_RUGBY);
	assert(strcmp(scoreboard_get_segment_name(), "Half") == 0);
	assert(scoreboard_get_has_shots() == false);
	assert(scoreboard_get_has_penalties() == true);
	assert(scoreboard_get_has_fouls() == false);
	assert(scoreboard_get_period_length() == 2400);
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_UP);
	const struct scoreboard_sport_preset *p = scoreboard_get_sport_preset();
	assert(p->segment_count == 2);
	assert(p->ot_max == 1);
}

static void test_foul_get_set(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_fouls() == 0);
	assert(scoreboard_get_away_fouls() == 0);
	scoreboard_set_home_fouls(5);
	scoreboard_set_away_fouls(3);
	assert(scoreboard_get_home_fouls() == 5);
	assert(scoreboard_get_away_fouls() == 3);
	/* Negative clamps to 0 */
	scoreboard_set_home_fouls(-1);
	assert(scoreboard_get_home_fouls() == 0);
	scoreboard_set_away_fouls(-5);
	assert(scoreboard_get_away_fouls() == 0);
}

static void test_foul_increment(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_increment_home_fouls();
	scoreboard_increment_home_fouls();
	assert(scoreboard_get_home_fouls() == 2);
	scoreboard_increment_away_fouls();
	assert(scoreboard_get_away_fouls() == 1);
}

static void test_foul_decrement(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_fouls(3);
	scoreboard_decrement_home_fouls();
	assert(scoreboard_get_home_fouls() == 2);
	/* Normal decrement for away */
	scoreboard_set_away_fouls(2);
	scoreboard_decrement_away_fouls();
	assert(scoreboard_get_away_fouls() == 1);
	/* Floors at 0 */
	scoreboard_set_away_fouls(0);
	scoreboard_decrement_away_fouls();
	assert(scoreboard_get_away_fouls() == 0);
}

static void test_has_fouls_per_sport(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(scoreboard_get_has_fouls() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_get_has_fouls() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(scoreboard_get_has_fouls() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(scoreboard_get_has_fouls() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);
	assert(scoreboard_get_has_fouls() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_RUGBY);
	assert(scoreboard_get_has_fouls() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_GENERIC);
	assert(scoreboard_get_has_fouls() == false);
}

static void test_foul_label_per_sport(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(strcmp(scoreboard_get_foul_label(), "Fouls") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(strcmp(scoreboard_get_foul_label(), "YC") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(strcmp(scoreboard_get_foul_label(), "Flags") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(strcmp(scoreboard_get_foul_label(), "") == 0);
}

static void test_fouls_reset_by_new_game(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_fouls(7);
	scoreboard_set_away_fouls(4);
	scoreboard_new_game();
	assert(scoreboard_get_home_fouls() == 0);
	assert(scoreboard_get_away_fouls() == 0);
}

static void test_fouls_write_read_files(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	scoreboard_set_home_fouls(5);
	scoreboard_set_away_fouls(3);
	scoreboard_set_home_fouls2(2);
	scoreboard_set_away_fouls2(1);

	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	scoreboard_mark_dirty();
	assert(scoreboard_write_all_files());

	/* Reset fouls then read back */
	scoreboard_set_home_fouls(0);
	scoreboard_set_away_fouls(0);
	scoreboard_set_home_fouls2(0);
	scoreboard_set_away_fouls2(0);
	assert(scoreboard_read_all_files());
	assert(scoreboard_get_home_fouls() == 5);
	assert(scoreboard_get_away_fouls() == 3);
	assert(scoreboard_get_home_fouls2() == 2);
	assert(scoreboard_get_away_fouls2() == 1);

	/* Cleanup */
	cleanup_dir(tmpdir);
}

static void test_fouls_save_load_state(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_fouls(12);
	scoreboard_set_away_fouls(8);
	scoreboard_set_home_fouls2(4);
	scoreboard_set_away_fouls2(2);

	char tmpfile[256];
	make_tmp_file(tmpfile, sizeof(tmpfile));

	assert(scoreboard_save_state(tmpfile));

	/* Reset then load */
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_fouls() == 0);
	assert(scoreboard_get_away_fouls() == 0);
	assert(scoreboard_get_home_fouls2() == 0);
	assert(scoreboard_get_away_fouls2() == 0);

	assert(scoreboard_load_state(tmpfile));
	assert(scoreboard_get_home_fouls() == 12);
	assert(scoreboard_get_away_fouls() == 8);
	assert(scoreboard_get_home_fouls2() == 4);
	assert(scoreboard_get_away_fouls2() == 2);

	remove(tmpfile);
}

static void test_foul2_get_set(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_fouls2() == 0);
	assert(scoreboard_get_away_fouls2() == 0);
	scoreboard_set_home_fouls2(3);
	scoreboard_set_away_fouls2(7);
	assert(scoreboard_get_home_fouls2() == 3);
	assert(scoreboard_get_away_fouls2() == 7);
	/* Negative clamps to 0 */
	scoreboard_set_home_fouls2(-1);
	assert(scoreboard_get_home_fouls2() == 0);
	scoreboard_set_away_fouls2(-5);
	assert(scoreboard_get_away_fouls2() == 0);
}

static void test_foul2_increment_decrement(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_increment_home_fouls2();
	scoreboard_increment_home_fouls2();
	assert(scoreboard_get_home_fouls2() == 2);
	scoreboard_increment_away_fouls2();
	assert(scoreboard_get_away_fouls2() == 1);
	/* Decrement */
	scoreboard_decrement_home_fouls2();
	assert(scoreboard_get_home_fouls2() == 1);
	/* Normal away decrement */
	scoreboard_decrement_away_fouls2();
	assert(scoreboard_get_away_fouls2() == 0);
	/* Floors at 0 */
	scoreboard_decrement_away_fouls2();
	assert(scoreboard_get_away_fouls2() == 0);
}

static void test_has_fouls2_per_sport(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(scoreboard_get_has_fouls2() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_get_has_fouls2() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(scoreboard_get_has_fouls2() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(scoreboard_get_has_fouls2() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);
	assert(scoreboard_get_has_fouls2() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_RUGBY);
	assert(scoreboard_get_has_fouls2() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_GENERIC);
	assert(scoreboard_get_has_fouls2() == false);
}

static void test_foul2_label_per_sport(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(strcmp(scoreboard_get_foul_label2(), "RC") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(strcmp(scoreboard_get_foul_label2(), "") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(strcmp(scoreboard_get_foul_label2(), "") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(strcmp(scoreboard_get_foul_label2(), "") == 0);
}

static void test_fouls2_reset_by_new_game(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_fouls2(5);
	scoreboard_set_away_fouls2(3);
	scoreboard_new_game();
	assert(scoreboard_get_home_fouls2() == 0);
	assert(scoreboard_get_away_fouls2() == 0);
}

static void test_log_scores_per_sport(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(scoreboard_get_log_scores() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_get_log_scores() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(scoreboard_get_log_scores() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(scoreboard_get_log_scores() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);
	assert(scoreboard_get_log_scores() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_RUGBY);
	assert(scoreboard_get_log_scores() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_GENERIC);
	assert(scoreboard_get_log_scores() == true);
}

static void test_score_label_per_sport(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(strcmp(scoreboard_get_score_label(), "Goal") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(strcmp(scoreboard_get_score_label(), "Score") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(strcmp(scoreboard_get_score_label(), "Goal") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(strcmp(scoreboard_get_score_label(), "Score") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);
	assert(strcmp(scoreboard_get_score_label(), "Goal") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_RUGBY);
	assert(strcmp(scoreboard_get_score_label(), "Try") == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_GENERIC);
	assert(strcmp(scoreboard_get_score_label(), "Score") == 0);
}

static void test_has_faceoffs_per_sport(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(scoreboard_get_has_faceoffs() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_get_has_faceoffs() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(scoreboard_get_has_faceoffs() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(scoreboard_get_has_faceoffs() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);
	assert(scoreboard_get_has_faceoffs() == true);

	scoreboard_set_sport(SCOREBOARD_SPORT_RUGBY);
	assert(scoreboard_get_has_faceoffs() == false);

	scoreboard_set_sport(SCOREBOARD_SPORT_GENERIC);
	assert(scoreboard_get_has_faceoffs() == false);
}

static void test_faceoffs_reset_by_new_game(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_home_faceoffs(15);
	scoreboard_set_away_faceoffs(12);
	scoreboard_new_game();
	assert(scoreboard_get_home_faceoffs() == 0);
	assert(scoreboard_get_away_faceoffs() == 0);
}

static void test_default_labels_per_sport(void)
{
	/* Hockey: 1,2,3,OT,OT2,OT3,OT4 */
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(scoreboard_get_period_label_count() == 7);
	assert(strcmp(scoreboard_get_period_label(3), "OT") == 0);

	/* Basketball: 1,2,3,4,OT */
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_get_period_label_count() == 5);
	assert(strcmp(scoreboard_get_period_label(3), "4") == 0);
	assert(strcmp(scoreboard_get_period_label(4), "OT") == 0);

	/* Soccer: 1,2,OT */
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(scoreboard_get_period_label_count() == 3);
	assert(strcmp(scoreboard_get_period_label(0), "1") == 0);
	assert(strcmp(scoreboard_get_period_label(2), "OT") == 0);

	/* Generic: 1 (1 segment, no OT) */
	scoreboard_set_sport(SCOREBOARD_SPORT_GENERIC);
	assert(scoreboard_get_period_label_count() == 1);
	assert(strcmp(scoreboard_get_period_label(0), "1") == 0);
}

static void test_period_labels_write_read_files(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period_labels("1\n2\n3\n4\n5\nOT\n2OT\n");

	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);
	scoreboard_mark_dirty();
	assert(scoreboard_write_all_files());

	/* Reset labels, then read back */
	scoreboard_set_period_labels("A\nB\n");
	assert(scoreboard_get_period_label_count() == 2);

	assert(scoreboard_read_all_files());
	assert(scoreboard_get_period_label_count() == 7);
	assert(strcmp(scoreboard_get_period_label(5), "OT") == 0);
	assert(strcmp(scoreboard_get_period_label(6), "2OT") == 0);

	cleanup_dir(tmpdir);
}

static void test_period_labels_save_load_state(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_period_labels("Q1\nQ2\nQ3\nQ4\nOT\n");

	char tmpfile[256];
	make_tmp_file(tmpfile, sizeof(tmpfile));
	assert(scoreboard_save_state(tmpfile));

	/* Reset, then load */
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_period_label_count() == 7);

	assert(scoreboard_load_state(tmpfile));
	assert(scoreboard_get_period_label_count() == 5);
	assert(strcmp(scoreboard_get_period_label(0), "Q1") == 0);
	assert(strcmp(scoreboard_get_period_label(4), "OT") == 0);

	remove(tmpfile);
}

int main(void)
{
	test_default_sport_is_hockey();
	test_set_sport_hockey();
	test_set_sport_basketball();
	test_set_sport_soccer();
	test_set_sport_football();
	test_set_sport_lacrosse();
	test_set_sport_generic();
	test_set_sport_invalid();
	test_sport_name_round_trip();
	test_sport_from_name_null();
	test_sport_from_name_unknown();
	test_sport_name_invalid_enum();
	test_format_period_basketball();
	test_format_period_soccer();
	test_period_clamp_basketball();
	test_period_advance_soccer();
	test_parse_period_basketball();
	test_sport_write_read_files();
	test_sport_save_load_state();
	test_set_sport_marks_dirty();
	test_sport_change_preserves_scores();
	test_get_sport_preset();
	test_set_sport_rugby();
	test_foul_get_set();
	test_foul_increment();
	test_foul_decrement();
	test_has_fouls_per_sport();
	test_foul_label_per_sport();
	test_fouls_reset_by_new_game();
	test_fouls_write_read_files();
	test_fouls_save_load_state();
	test_foul2_get_set();
	test_foul2_increment_decrement();
	test_has_fouls2_per_sport();
	test_foul2_label_per_sport();
	test_fouls2_reset_by_new_game();
	test_log_scores_per_sport();
	test_score_label_per_sport();
	test_has_faceoffs_per_sport();
	test_faceoffs_reset_by_new_game();
	test_default_labels_per_sport();
	test_period_labels_write_read_files();
	test_period_labels_save_load_state();

	printf("All scoreboard-core sport tests passed.\n");
	return 0;
}
