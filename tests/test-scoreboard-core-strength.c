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
#define getpid() _getpid()
#else
#include <unistd.h>
#endif

static int g_tmp_counter;

static void make_tmp_dir(char *buf, size_t size)
{
#ifdef _WIN32
	snprintf(buf, size, "%s\\sb_strength_%d_%d",
		 getenv("TEMP") ? getenv("TEMP") : ".", (int)getpid(),
		 g_tmp_counter++);
#else
	snprintf(buf, size, "/tmp/sb_strength_%d_%d", (int)getpid(),
		 g_tmp_counter++);
#endif
	mkdir(buf, 0755);
}

static void cleanup_dir(const char *dir)
{
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	system(cmd);
}

static char *read_file_content(const char *dir, const char *filename)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, filename);
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = (char *)malloc((size_t)sz + 1);
	size_t n = fread(buf, 1, (size_t)sz, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

/* ---- sport preset defaults ---- */

static void test_base_strength_hockey_default(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_base_strength() == 5);
	assert(scoreboard_get_min_strength() == 3);
}

static void test_base_strength_per_sport(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_sport(SCOREBOARD_SPORT_HOCKEY);
	assert(scoreboard_get_base_strength() == 5);
	assert(scoreboard_get_min_strength() == 3);

	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_get_base_strength() == 0);
	assert(scoreboard_get_min_strength() == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(scoreboard_get_base_strength() == 11);
	assert(scoreboard_get_min_strength() == 7);

	scoreboard_set_sport(SCOREBOARD_SPORT_FOOTBALL);
	assert(scoreboard_get_base_strength() == 0);
	assert(scoreboard_get_min_strength() == 0);

	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);
	assert(scoreboard_get_base_strength() == 5);
	assert(scoreboard_get_min_strength() == 3);

	scoreboard_set_sport(SCOREBOARD_SPORT_RUGBY);
	assert(scoreboard_get_base_strength() == 15);
	assert(scoreboard_get_min_strength() == 13);

	scoreboard_set_sport(SCOREBOARD_SPORT_GENERIC);
	assert(scoreboard_get_base_strength() == 0);
	assert(scoreboard_get_min_strength() == 0);
}

static void test_set_base_strength(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_base_strength(4);
	assert(scoreboard_get_base_strength() == 4);

	/* Negative clamps to 0 */
	scoreboard_set_base_strength(-1);
	assert(scoreboard_get_base_strength() == 0);
}

/* ---- even strength (no penalties) ---- */

static void test_even_strength(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_strength() == 5);
	assert(scoreboard_get_away_strength() == 5);
}

static void test_even_strength_no_tracking(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	assert(scoreboard_get_home_strength() == 0);
	assert(scoreboard_get_away_strength() == 0);
}

/* ---- penalty-based strength (hockey) ---- */

static void test_single_penalty_power_play(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	assert(scoreboard_get_home_strength() == 4);
	assert(scoreboard_get_away_strength() == 5);
}

static void test_double_penalty(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add(20, 120);
	assert(scoreboard_get_home_strength() == 3);
	assert(scoreboard_get_away_strength() == 5);
}

static void test_three_penalties_capped(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_home_penalty_add(20, 120);
	scoreboard_home_penalty_add(30, 120);
	/* Only 2 running — strength stays at 3 */
	assert(scoreboard_get_home_strength() == 3);
	assert(scoreboard_get_away_strength() == 5);
}

static void test_offsetting_penalties(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	scoreboard_away_penalty_add(20, 120);
	assert(scoreboard_get_home_strength() == 4);
	assert(scoreboard_get_away_strength() == 4);
}

static void test_strength_after_penalty_clear(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 120);
	assert(scoreboard_get_home_strength() == 4);
	scoreboard_home_penalty_clear(0);
	assert(scoreboard_get_home_strength() == 5);
}

static void test_strength_after_penalty_expires(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_home_penalty_add(10, 1); /* 1 second = 10 tenths */
	assert(scoreboard_get_home_strength() == 4);
	scoreboard_penalty_tick(10); /* expire it */
	assert(scoreboard_get_home_strength() == 5);
}

/* ---- custom base strength (4v4, 3v3) ---- */

static void test_custom_base_4v4(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_base_strength(4);
	assert(scoreboard_get_home_strength() == 4);
	assert(scoreboard_get_away_strength() == 4);

	scoreboard_home_penalty_add(10, 120);
	assert(scoreboard_get_home_strength() == 3);

	scoreboard_home_penalty_add(20, 120);
	/* 4 - 2 = 2, but clamped to min_strength (3) */
	assert(scoreboard_get_home_strength() == 3);
}

static void test_custom_base_3v3(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_base_strength(3);
	assert(scoreboard_get_home_strength() == 3);

	scoreboard_home_penalty_add(10, 120);
	/* 3 - 1 = 2, but clamped to min_strength (3) */
	assert(scoreboard_get_home_strength() == 3);
}

/* ---- soccer red card strength ---- */

static void test_soccer_single_red_card(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	assert(scoreboard_get_home_strength() == 11);

	scoreboard_increment_home_fouls2();
	assert(scoreboard_get_home_strength() == 10);
	assert(scoreboard_get_away_strength() == 11);
}

static void test_soccer_multiple_red_cards(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);

	scoreboard_increment_home_fouls2();
	scoreboard_increment_home_fouls2();
	assert(scoreboard_get_home_strength() == 9);

	scoreboard_increment_away_fouls2();
	assert(scoreboard_get_away_strength() == 10);
}

static void test_soccer_clamped_to_min(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);

	/* Add 5 red cards: 11 - 5 = 6, but clamped to 7 */
	for (int i = 0; i < 5; i++)
		scoreboard_increment_home_fouls2();
	assert(scoreboard_get_home_strength() == 7);
}

static void test_soccer_custom_base_7v7(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);
	scoreboard_set_base_strength(7);
	assert(scoreboard_get_home_strength() == 7);

	scoreboard_increment_home_fouls2();
	/* 7 - 1 = 6, but clamped to min_strength (7) */
	assert(scoreboard_get_home_strength() == 7);
}

static void test_soccer_red_card_cleared(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_SOCCER);

	scoreboard_increment_home_fouls2();
	assert(scoreboard_get_home_strength() == 10);

	scoreboard_decrement_home_fouls2();
	assert(scoreboard_get_home_strength() == 11);
}

/* ---- lacrosse and rugby ---- */

static void test_lacrosse_strength(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_LACROSSE);
	assert(scoreboard_get_home_strength() == 5);

	scoreboard_home_penalty_add(10, 60);
	assert(scoreboard_get_home_strength() == 4);
}

static void test_rugby_strength(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_RUGBY);
	assert(scoreboard_get_home_strength() == 15);

	scoreboard_home_penalty_add(10, 120);
	assert(scoreboard_get_home_strength() == 14);

	scoreboard_home_penalty_add(20, 120);
	assert(scoreboard_get_home_strength() == 13);
}

/* ---- file output ---- */

static void test_strength_file_output(void)
{
	scoreboard_reset_state_for_tests();
	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	scoreboard_home_penalty_add(10, 120);
	assert(scoreboard_write_all_files());

	char *content = read_file_content(tmpdir, "strength.txt");
	assert(content != NULL);
	assert(strcmp(content, "4-5") == 0);
	free(content);

	cleanup_dir(tmpdir);
}

static void test_strength_file_even(void)
{
	scoreboard_reset_state_for_tests();
	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	scoreboard_mark_dirty();
	assert(scoreboard_write_all_files());

	char *content = read_file_content(tmpdir, "strength.txt");
	assert(content != NULL);
	assert(strcmp(content, "5-5") == 0);
	free(content);

	cleanup_dir(tmpdir);
}

static void test_strength_file_not_written_when_zero(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	scoreboard_mark_dirty();
	assert(scoreboard_write_all_files());

	char *content = read_file_content(tmpdir, "strength.txt");
	assert(content == NULL); /* file should not exist */

	cleanup_dir(tmpdir);
}

/* ---- JSON save/load ---- */

static void test_strength_save_load(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_base_strength(4);

	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	char save_path[512];
	snprintf(save_path, sizeof(save_path), "%s/state.json", tmpdir);

	assert(scoreboard_save_state(save_path));

	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_base_strength() == 5); /* hockey default */

	assert(scoreboard_load_state(save_path));
	assert(scoreboard_get_base_strength() == 4); /* restored */

	cleanup_dir(tmpdir);
}

static void test_strength_save_load_default(void)
{
	scoreboard_reset_state_for_tests();
	/* Don't override — should persist hockey default */

	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	char save_path[512];
	snprintf(save_path, sizeof(save_path), "%s/state.json", tmpdir);

	assert(scoreboard_save_state(save_path));

	scoreboard_reset_state_for_tests();
	scoreboard_set_base_strength(0);

	assert(scoreboard_load_state(save_path));
	assert(scoreboard_get_base_strength() == 5);

	cleanup_dir(tmpdir);
}

/* ---- dirty flag ---- */

static void test_set_base_strength_marks_dirty(void)
{
	scoreboard_reset_state_for_tests();
	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	/* Drain dirty flag */
	scoreboard_write_all_files();

	scoreboard_set_base_strength(4);
	/* Should be dirty now — write should produce output */
	assert(scoreboard_write_all_files());

	char *content = read_file_content(tmpdir, "strength.txt");
	assert(content != NULL);
	assert(strcmp(content, "4-4") == 0);
	free(content);

	cleanup_dir(tmpdir);
}

/* ---- strength label format ---- */

static void test_default_strength_format(void)
{
	scoreboard_reset_state_for_tests();
	assert(strcmp(scoreboard_get_strength_label_format(),
		     "{{ home }}-{{ away }}") == 0);
}

static void test_set_strength_format(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format("{{ home }}v{{ away }}");
	assert(strcmp(scoreboard_get_strength_label_format(),
		     "{{ home }}v{{ away }}") == 0);
}

static void test_format_strength_default(void)
{
	scoreboard_reset_state_for_tests();
	char buf[128];
	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "5-5") == 0);
}

static void test_format_strength_custom(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format("{{ home }}v{{ away }}");
	scoreboard_home_penalty_add(10, 120);
	char buf[128];
	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "4v5") == 0);
}

static void test_format_strength_with_text(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format("{{ home }} on {{ away }}");
	scoreboard_home_penalty_add(10, 120);
	char buf[128];
	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "4 on 5") == 0);
}

static void test_format_strength_if_pp(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format(
		"{{ if_pp }}PP {{ home }}v{{ away }}{{ end_if }}");

	char buf[128];

	/* Even strength — should produce empty string */
	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);

	/* Power play — should produce formatted string */
	scoreboard_home_penalty_add(10, 120);
	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "PP 4v5") == 0);
}

static void test_format_strength_mixed_conditional(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format(
		"{{ home }}-{{ away }}{{ if_pp }} PP{{ end_if }}");

	char buf[128];

	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "5-5") == 0);

	scoreboard_home_penalty_add(10, 120);
	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "4-5 PP") == 0);
}

static void test_format_strength_zero_base(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_sport(SCOREBOARD_SPORT_BASKETBALL);
	char buf[128];
	buf[0] = 'X';
	scoreboard_format_strength(buf, sizeof(buf));
	assert(buf[0] == '\0');
}

static void test_preview_strength_label(void)
{
	char buf[512];
	scoreboard_preview_strength_label("{{ home }}-{{ away }}", buf,
					  sizeof(buf));
	assert(strcmp(buf, "Even: 5-5\nPP:   5-4") == 0);
}

static void test_preview_strength_with_pp(void)
{
	char buf[512];
	scoreboard_preview_strength_label(
		"{{ if_pp }}{{ home }}v{{ away }}{{ end_if }}", buf,
		sizeof(buf));
	assert(strcmp(buf, "Even: \nPP:   5v4") == 0);
}

static void test_preview_strength_null(void)
{
	char buf[512];
	scoreboard_preview_strength_label(NULL, buf, sizeof(buf));
	assert(strcmp(buf, "Even: 5-5\nPP:   5-4") == 0);
}

static void test_preview_strength_empty(void)
{
	char buf[512];
	scoreboard_preview_strength_label("", buf, sizeof(buf));
	assert(strcmp(buf, "Even: 5-5\nPP:   5-4") == 0);
}

static void test_strength_file_custom_format(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format("{{ home }}v{{ away }}");
	scoreboard_home_penalty_add(10, 120);

	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	assert(scoreboard_write_all_files());

	char *content = read_file_content(tmpdir, "strength.txt");
	assert(content != NULL);
	assert(strcmp(content, "4v5") == 0);
	free(content);

	cleanup_dir(tmpdir);
}

static void test_strength_format_save_load(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format("{{ home }}v{{ away }}");

	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	char save_path[512];
	snprintf(save_path, sizeof(save_path), "%s/state.json", tmpdir);

	assert(scoreboard_save_state(save_path));

	scoreboard_reset_state_for_tests();
	assert(strcmp(scoreboard_get_strength_label_format(),
		     "{{ home }}-{{ away }}") == 0);

	assert(scoreboard_load_state(save_path));
	assert(strcmp(scoreboard_get_strength_label_format(),
		     "{{ home }}v{{ away }}") == 0);

	cleanup_dir(tmpdir);
}

static void test_format_strength_null_buf(void)
{
	/* Should not crash */
	scoreboard_format_strength(NULL, 0);
}

static void test_preview_strength_null_buf(void)
{
	/* Should not crash */
	scoreboard_preview_strength_label("{{ home }}", NULL, 0);
}

static void test_format_strength_empty_format_fallback(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format("");
	/* Empty format should fall back to default */
	assert(strcmp(scoreboard_get_strength_label_format(),
		     "{{ home }}-{{ away }}") == 0);
}

static void test_format_strength_unterminated(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format("{{ home }}–{{ away");
	char buf[128];
	scoreboard_format_strength(buf, sizeof(buf));
	/* Copies literal text after unterminated {{ */
	assert(strcmp(buf, "5\xe2\x80\x93{{ away") == 0);
}

static void test_format_strength_if_pp_no_end_if(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_strength_label_format("{{ if_pp }}PP");
	char buf[128];

	/* Even strength — skips to end, no end_if found */
	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "") == 0);

	/* Power play — renders the content */
	scoreboard_home_penalty_add(10, 120);
	scoreboard_format_strength(buf, sizeof(buf));
	assert(strcmp(buf, "PP") == 0);
}

static void test_set_strength_format_marks_dirty(void)
{
	scoreboard_reset_state_for_tests();
	char tmpdir[256];
	make_tmp_dir(tmpdir, sizeof(tmpdir));
	scoreboard_set_output_directory(tmpdir);

	scoreboard_write_all_files();

	scoreboard_set_strength_label_format("{{ home }}v{{ away }}");
	assert(scoreboard_write_all_files());

	char *content = read_file_content(tmpdir, "strength.txt");
	assert(content != NULL);
	assert(strcmp(content, "5v5") == 0);
	free(content);

	cleanup_dir(tmpdir);
}

int main(void)
{
	/* Sport preset defaults */
	test_base_strength_hockey_default();
	test_base_strength_per_sport();
	test_set_base_strength();

	/* Even strength */
	test_even_strength();
	test_even_strength_no_tracking();

	/* Penalty-based strength */
	test_single_penalty_power_play();
	test_double_penalty();
	test_three_penalties_capped();
	test_offsetting_penalties();
	test_strength_after_penalty_clear();
	test_strength_after_penalty_expires();

	/* Custom base strength */
	test_custom_base_4v4();
	test_custom_base_3v3();

	/* Soccer red cards */
	test_soccer_single_red_card();
	test_soccer_multiple_red_cards();
	test_soccer_clamped_to_min();
	test_soccer_custom_base_7v7();
	test_soccer_red_card_cleared();

	/* Lacrosse and rugby */
	test_lacrosse_strength();
	test_rugby_strength();

	/* File output */
	test_strength_file_output();
	test_strength_file_even();
	test_strength_file_not_written_when_zero();

	/* JSON persistence */
	test_strength_save_load();
	test_strength_save_load_default();

	/* Dirty flag */
	test_set_base_strength_marks_dirty();

	/* Strength label format */
	test_default_strength_format();
	test_set_strength_format();
	test_format_strength_default();
	test_format_strength_custom();
	test_format_strength_with_text();
	test_format_strength_if_pp();
	test_format_strength_mixed_conditional();
	test_format_strength_zero_base();
	test_preview_strength_label();
	test_preview_strength_with_pp();
	test_preview_strength_null();
	test_preview_strength_empty();
	test_strength_file_custom_format();
	test_strength_format_save_load();
	test_format_strength_null_buf();
	test_preview_strength_null_buf();
	test_format_strength_empty_format_fallback();
	test_format_strength_unterminated();
	test_format_strength_if_pp_no_end_if();
	test_set_strength_format_marks_dirty();

	printf("All scoreboard-core strength tests passed.\n");
	return 0;
}
