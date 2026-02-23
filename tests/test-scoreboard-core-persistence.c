#include "scoreboard-core.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_tmp_dir[256];

static void setup_tmp_dir(void)
{
	snprintf(g_tmp_dir, sizeof(g_tmp_dir), "/tmp/scoreboard_test_%d",
		 (int)getpid());
	mkdir(g_tmp_dir, 0755);
}

static void cleanup_tmp_dir(void)
{
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmp_dir);
	system(cmd);
}

static char *read_file_content(const char *path)
{
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = (char *)malloc((size_t)size + 1);
	size_t n = fread(buf, 1, (size_t)size, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static void test_write_all_files(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	scoreboard_set_output_directory(g_tmp_dir);
	assert(strcmp(scoreboard_get_output_directory(), g_tmp_dir) == 0);

	scoreboard_set_home_name("Eagles");
	scoreboard_set_away_name("Hawks");
	scoreboard_set_home_score(3);
	scoreboard_set_away_score(1);
	scoreboard_set_home_shots(25);
	scoreboard_set_away_shots(18);
	scoreboard_home_penalty_add(12, 120);

	bool ok = scoreboard_write_all_files();
	assert(ok);

	char path[512];

	snprintf(path, sizeof(path), "%s/clock.txt", g_tmp_dir);
	char *content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "15:00") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/period.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "1") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/home_name.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "Eagles") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/away_name.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "Hawks") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/home_score.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "3") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/away_score.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "1") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/home_shots.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "25") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/away_shots.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "18") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/home_penalty_numbers.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "#12") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/home_penalty_times.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "2:00") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/away_penalty_numbers.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/away_penalty_times.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "") == 0);
	free(content);

	cleanup_tmp_dir();
}

static void test_write_all_files_no_directory(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_output_directory("");
	assert(!scoreboard_write_all_files());
}

static void test_write_all_files_null_directory(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_output_directory(NULL);
	assert(!scoreboard_write_all_files());
}

static void test_save_load_state(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	scoreboard_set_home_name("Eagles");
	scoreboard_set_away_name("Hawks");
	scoreboard_set_home_score(3);
	scoreboard_set_away_score(2);
	scoreboard_set_home_shots(25);
	scoreboard_set_away_shots(18);
	scoreboard_set_period(2);
	scoreboard_clock_set_tenths(5000);
	scoreboard_set_clock_direction(SCOREBOARD_CLOCK_COUNT_UP);
	scoreboard_set_period_length(1200);
	scoreboard_set_overtime_enabled(false);
	scoreboard_clock_start();
	scoreboard_home_penalty_add(12, 120);
	scoreboard_away_penalty_add(22, 60);

	char save_path[512];
	snprintf(save_path, sizeof(save_path), "%s/state.json", g_tmp_dir);

	bool saved = scoreboard_save_state(save_path);
	assert(saved);

	/* reset and reload */
	scoreboard_reset_state_for_tests();
	assert(scoreboard_get_home_score() == 0);
	assert(strcmp(scoreboard_get_home_name(), "Home") == 0);

	bool loaded = scoreboard_load_state(save_path);
	assert(loaded);

	assert(strcmp(scoreboard_get_home_name(), "Eagles") == 0);
	assert(strcmp(scoreboard_get_away_name(), "Hawks") == 0);
	assert(scoreboard_get_home_score() == 3);
	assert(scoreboard_get_away_score() == 2);
	assert(scoreboard_get_home_shots() == 25);
	assert(scoreboard_get_away_shots() == 18);
	assert(scoreboard_get_period() == 2);
	assert(scoreboard_clock_get_tenths() == 5000);
	assert(scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_UP);
	assert(scoreboard_get_period_length() == 1200);
	assert(!scoreboard_get_overtime_enabled());
	assert(scoreboard_clock_is_running());

	const struct scoreboard_penalty *hp = scoreboard_get_home_penalty(0);
	assert(hp->active);
	assert(hp->player_number == 12);
	assert(hp->remaining_tenths == 1200);

	const struct scoreboard_penalty *ap = scoreboard_get_away_penalty(0);
	assert(ap->active);
	assert(ap->player_number == 22);
	assert(ap->remaining_tenths == 600);

	cleanup_tmp_dir();
}

static void test_save_state_null(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_save_state(NULL));
}

static void test_save_state_bad_path(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_save_state("/nonexistent/dir/state.json"));
}

static void test_load_state_null(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_load_state(NULL));
}

static void test_load_state_bad_path(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_load_state("/nonexistent/state.json"));
}

static void test_load_state_empty_file(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	char path[512];
	snprintf(path, sizeof(path), "%s/empty.json", g_tmp_dir);
	FILE *f = fopen(path, "w");
	fclose(f);
	assert(!scoreboard_load_state(path));
	cleanup_tmp_dir();
}

static void test_action_log(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_add_action_log("first log");
	scoreboard_add_action_log("second log");

	char buf[1024];
	size_t written = scoreboard_copy_action_logs(buf, sizeof(buf));
	assert(written > 0);
	assert(strstr(buf, "first log") != NULL);
	assert(strstr(buf, "second log") != NULL);
}

static void test_action_log_null(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_add_action_log(NULL);
	char buf[128];
	size_t w = scoreboard_copy_action_logs(buf, sizeof(buf));
	assert(w == 0);
}

static void test_action_log_null_buffer(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_add_action_log("test");
	size_t w = scoreboard_copy_action_logs(NULL, 0);
	assert(w == 0);
	char buf[2];
	w = scoreboard_copy_action_logs(buf, 0);
	assert(w == 0);
}

static void test_action_log_ring_buffer(void)
{
	scoreboard_reset_state_for_tests();
	for (int i = 0; i < 70; i++) {
		char msg[32];
		snprintf(msg, sizeof(msg), "log_%d", i);
		scoreboard_add_action_log(msg);
	}

	char buf[16384];
	size_t w = scoreboard_copy_action_logs(buf, sizeof(buf));
	assert(w > 0);
	/* oldest entries (0-5) should be gone, recent ones present */
	assert(strstr(buf, "log_0") == NULL);
	assert(strstr(buf, "log_69") != NULL);
	assert(strstr(buf, "log_6") != NULL);
}

static void test_action_log_small_buffer(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_add_action_log("hello");
	scoreboard_add_action_log("world");
	char buf[8];
	size_t w = scoreboard_copy_action_logs(buf, sizeof(buf));
	assert(w > 0);
	assert(strcmp(buf, "hello") == 0);
}

static void test_cli_settings(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_set_cli_executable("/usr/local/bin/streamn");
	assert(strcmp(scoreboard_get_cli_executable(),
		     "/usr/local/bin/streamn") == 0);

	scoreboard_set_main_config_path("/home/user/config.json");
	assert(strcmp(scoreboard_get_main_config_path(),
		     "/home/user/config.json") == 0);

	scoreboard_set_override_config_path("/home/user/override.json");
	assert(strcmp(scoreboard_get_override_config_path(),
		     "/home/user/override.json") == 0);
}

static void test_cli_settings_null(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_cli_executable(NULL);
	assert(strcmp(scoreboard_get_cli_executable(), "") == 0);
	scoreboard_set_main_config_path(NULL);
	assert(strcmp(scoreboard_get_main_config_path(), "") == 0);
	scoreboard_set_override_config_path(NULL);
	assert(strcmp(scoreboard_get_override_config_path(), "") == 0);
}

static void test_write_all_files_bad_directory(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_output_directory("/nonexistent/path/that/does/not/exist");
	assert(!scoreboard_write_all_files());
}

static void test_load_state_partial_json(void)
{
	/* JSON with only a few keys exercises find_json_value returning NULL
	   for missing keys, plus parse_json_int/bool/string default paths. */
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	char path[512];
	snprintf(path, sizeof(path), "%s/partial.json", g_tmp_dir);
	FILE *f = fopen(path, "w");
	fprintf(f, "{\n  \"home_score\": 5\n}\n");
	fclose(f);

	scoreboard_set_home_score(0);
	bool loaded = scoreboard_load_state(path);
	assert(loaded);
	assert(scoreboard_get_home_score() == 5);
	/* fields not in JSON keep their defaults */
	assert(scoreboard_get_away_score() == 0);
	cleanup_tmp_dir();
}

static void test_load_state_invalid_bool(void)
{
	/* A bool field with a value that is neither "true" nor "false"
	   exercises the parse_json_bool default_val fallback (line 113). */
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	char path[512];
	snprintf(path, sizeof(path), "%s/badbool.json", g_tmp_dir);
	FILE *f = fopen(path, "w");
	fprintf(f, "{\n  \"clock_running\": maybe,\n  \"overtime_enabled\": 42\n}\n");
	fclose(f);

	bool loaded = scoreboard_load_state(path);
	assert(loaded);
	/* both should retain defaults since values are invalid */
	assert(!scoreboard_clock_is_running());
	assert(scoreboard_get_overtime_enabled());
	cleanup_tmp_dir();
}

static void test_save_load_special_chars(void)
{
	/* Team names with quotes and backslashes exercise write_json_string
	   escape path (line 140) and parse_json_string escape path (line 128). */
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	scoreboard_set_home_name("Team \"A\"");
	scoreboard_set_away_name("Team\\B");

	char path[512];
	snprintf(path, sizeof(path), "%s/special.json", g_tmp_dir);
	bool saved = scoreboard_save_state(path);
	assert(saved);

	scoreboard_reset_state_for_tests();
	bool loaded = scoreboard_load_state(path);
	assert(loaded);
	assert(strcmp(scoreboard_get_home_name(), "Team \"A\"") == 0);
	assert(strcmp(scoreboard_get_away_name(), "Team\\B") == 0);

	cleanup_tmp_dir();
}

static void test_load_state_string_not_quoted(void)
{
	/* A string field with a non-quoted value exercises parse_json_string
	   null/non-quote path (lines 121-122). */
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	char path[512];
	snprintf(path, sizeof(path), "%s/noquote.json", g_tmp_dir);
	FILE *f = fopen(path, "w");
	fprintf(f, "{\n  \"home_name\": unquoted_value\n}\n");
	fclose(f);

	scoreboard_set_home_name("Eagles");
	bool loaded = scoreboard_load_state(path);
	assert(loaded);
	/* parse_json_string should set empty string for non-quoted value */
	assert(strcmp(scoreboard_get_home_name(), "") == 0);
	cleanup_tmp_dir();
}

static void test_output_directory_null(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_output_directory(NULL);
	assert(strcmp(scoreboard_get_output_directory(), "") == 0);
}

static void test_write_penalty_files_consolidated(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	/* Add two home penalties, one away */
	scoreboard_home_penalty_add(12, 120);
	scoreboard_home_penalty_add(7, 60);
	scoreboard_away_penalty_add(22, 300);

	bool ok = scoreboard_write_all_files();
	assert(ok);

	char path[512];
	char *content;

	snprintf(path, sizeof(path), "%s/home_penalty_numbers.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "#12\n#7") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/home_penalty_times.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "2:00\n1:00") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/away_penalty_numbers.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "#22") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/away_penalty_times.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "5:00") == 0);
	free(content);

	/* Clear first home penalty — files update correctly */
	scoreboard_home_penalty_clear(0);
	ok = scoreboard_write_all_files();
	assert(ok);

	snprintf(path, sizeof(path), "%s/home_penalty_numbers.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "#7") == 0);
	free(content);

	snprintf(path, sizeof(path), "%s/home_penalty_times.txt", g_tmp_dir);
	content = read_file_content(path);
	assert(content != NULL);
	assert(strcmp(content, "1:00") == 0);
	free(content);

	cleanup_tmp_dir();
}

static void write_file(const char *dir, const char *name, const char *content)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *f = fopen(path, "w");
	fprintf(f, "%s", content);
	fclose(f);
}

static void test_read_all_files(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	/* Write text files manually */
	write_file(g_tmp_dir, "clock.txt", "8:30");
	write_file(g_tmp_dir, "period.txt", "2");
	write_file(g_tmp_dir, "home_name.txt", "Eagles");
	write_file(g_tmp_dir, "away_name.txt", "Hawks");
	write_file(g_tmp_dir, "home_score.txt", "3");
	write_file(g_tmp_dir, "away_score.txt", "1");
	write_file(g_tmp_dir, "home_shots.txt", "25");
	write_file(g_tmp_dir, "away_shots.txt", "18");
	write_file(g_tmp_dir, "home_penalty_numbers.txt", "#12\n#7");
	write_file(g_tmp_dir, "home_penalty_times.txt", "1:30\n0:45");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "#22");
	write_file(g_tmp_dir, "away_penalty_times.txt", "2:00");

	bool ok = scoreboard_read_all_files();
	assert(ok);

	assert(scoreboard_clock_get_tenths() == 5100);
	assert(scoreboard_get_period() == 2);
	assert(strcmp(scoreboard_get_home_name(), "Eagles") == 0);
	assert(strcmp(scoreboard_get_away_name(), "Hawks") == 0);
	assert(scoreboard_get_home_score() == 3);
	assert(scoreboard_get_away_score() == 1);
	assert(scoreboard_get_home_shots() == 25);
	assert(scoreboard_get_away_shots() == 18);

	assert(scoreboard_get_home_penalty_count() == 2);
	const struct scoreboard_penalty *hp0 = scoreboard_get_home_penalty(0);
	assert(hp0->active && hp0->player_number == 12);
	assert(hp0->remaining_tenths == 900);
	const struct scoreboard_penalty *hp1 = scoreboard_get_home_penalty(1);
	assert(hp1->active && hp1->player_number == 7);
	assert(hp1->remaining_tenths == 450);

	assert(scoreboard_get_away_penalty_count() == 1);
	const struct scoreboard_penalty *ap0 = scoreboard_get_away_penalty(0);
	assert(ap0->active && ap0->player_number == 22);
	assert(ap0->remaining_tenths == 1200);

	cleanup_tmp_dir();
}

static void test_read_all_files_no_directory(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_set_output_directory("");
	assert(!scoreboard_read_all_files());
}

static void test_read_all_files_missing_files(void)
{
	/* Should return false but not crash when files are missing */
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);
	assert(!scoreboard_read_all_files());
	cleanup_tmp_dir();
}

static void test_read_all_files_ot_period(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	write_file(g_tmp_dir, "clock.txt", "5:00");
	write_file(g_tmp_dir, "period.txt", "OT");
	write_file(g_tmp_dir, "home_name.txt", "A");
	write_file(g_tmp_dir, "away_name.txt", "B");
	write_file(g_tmp_dir, "home_score.txt", "0");
	write_file(g_tmp_dir, "away_score.txt", "0");
	write_file(g_tmp_dir, "home_shots.txt", "0");
	write_file(g_tmp_dir, "away_shots.txt", "0");
	write_file(g_tmp_dir, "home_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "home_penalty_times.txt", "");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "away_penalty_times.txt", "");

	bool ok = scoreboard_read_all_files();
	assert(ok);
	assert(scoreboard_get_period() == 4);

	/* OT3 */
	write_file(g_tmp_dir, "period.txt", "OT3");
	ok = scoreboard_read_all_files();
	assert(ok);
	assert(scoreboard_get_period() == 6);

	cleanup_tmp_dir();
}

static void test_read_all_files_empty_penalties(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	/* Pre-add a penalty then refresh with empty files — should clear it */
	scoreboard_home_penalty_add(12, 120);
	assert(scoreboard_get_home_penalty_count() == 1);

	write_file(g_tmp_dir, "clock.txt", "15:00");
	write_file(g_tmp_dir, "period.txt", "1");
	write_file(g_tmp_dir, "home_name.txt", "Home");
	write_file(g_tmp_dir, "away_name.txt", "Away");
	write_file(g_tmp_dir, "home_score.txt", "0");
	write_file(g_tmp_dir, "away_score.txt", "0");
	write_file(g_tmp_dir, "home_shots.txt", "0");
	write_file(g_tmp_dir, "away_shots.txt", "0");
	write_file(g_tmp_dir, "home_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "home_penalty_times.txt", "");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "away_penalty_times.txt", "");

	bool ok = scoreboard_read_all_files();
	assert(ok);
	assert(scoreboard_get_home_penalty_count() == 0);

	cleanup_tmp_dir();
}

static void test_read_all_files_no_player_number(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	write_file(g_tmp_dir, "clock.txt", "10:00");
	write_file(g_tmp_dir, "period.txt", "1");
	write_file(g_tmp_dir, "home_name.txt", "Home");
	write_file(g_tmp_dir, "away_name.txt", "Away");
	write_file(g_tmp_dir, "home_score.txt", "0");
	write_file(g_tmp_dir, "away_score.txt", "0");
	write_file(g_tmp_dir, "home_shots.txt", "0");
	write_file(g_tmp_dir, "away_shots.txt", "0");
	write_file(g_tmp_dir, "home_penalty_numbers.txt", " ");
	write_file(g_tmp_dir, "home_penalty_times.txt", "2:00");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "away_penalty_times.txt", "");

	bool ok = scoreboard_read_all_files();
	assert(ok);
	assert(scoreboard_get_home_penalty_count() == 1);
	const struct scoreboard_penalty *hp = scoreboard_get_home_penalty(0);
	assert(hp->active && hp->player_number == 0);
	assert(hp->remaining_tenths == 1200);

	cleanup_tmp_dir();
}

static void test_read_all_files_invalid_clock(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	write_file(g_tmp_dir, "clock.txt", "garbage");
	write_file(g_tmp_dir, "period.txt", "1");
	write_file(g_tmp_dir, "home_name.txt", "H");
	write_file(g_tmp_dir, "away_name.txt", "A");
	write_file(g_tmp_dir, "home_score.txt", "0");
	write_file(g_tmp_dir, "away_score.txt", "0");
	write_file(g_tmp_dir, "home_shots.txt", "0");
	write_file(g_tmp_dir, "away_shots.txt", "0");
	write_file(g_tmp_dir, "home_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "home_penalty_times.txt", "");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "away_penalty_times.txt", "");

	/* Clock should remain unchanged since format is invalid */
	int before = scoreboard_clock_get_tenths();
	scoreboard_read_all_files();
	assert(scoreboard_clock_get_tenths() == before);

	cleanup_tmp_dir();
}

static void test_read_all_files_invalid_period(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	write_file(g_tmp_dir, "clock.txt", "10:00");
	write_file(g_tmp_dir, "period.txt", "bogus");
	write_file(g_tmp_dir, "home_name.txt", "H");
	write_file(g_tmp_dir, "away_name.txt", "A");
	write_file(g_tmp_dir, "home_score.txt", "0");
	write_file(g_tmp_dir, "away_score.txt", "0");
	write_file(g_tmp_dir, "home_shots.txt", "0");
	write_file(g_tmp_dir, "away_shots.txt", "0");
	write_file(g_tmp_dir, "home_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "home_penalty_times.txt", "");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "away_penalty_times.txt", "");

	/* Period should remain 1 since text is invalid */
	scoreboard_read_all_files();
	assert(scoreboard_get_period() == 1);

	cleanup_tmp_dir();
}

static void test_read_all_files_ot2_ot4(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	write_file(g_tmp_dir, "clock.txt", "5:00");
	write_file(g_tmp_dir, "home_name.txt", "H");
	write_file(g_tmp_dir, "away_name.txt", "A");
	write_file(g_tmp_dir, "home_score.txt", "0");
	write_file(g_tmp_dir, "away_score.txt", "0");
	write_file(g_tmp_dir, "home_shots.txt", "0");
	write_file(g_tmp_dir, "away_shots.txt", "0");
	write_file(g_tmp_dir, "home_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "home_penalty_times.txt", "");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "away_penalty_times.txt", "");

	write_file(g_tmp_dir, "period.txt", "OT2");
	scoreboard_read_all_files();
	assert(scoreboard_get_period() == 5);

	write_file(g_tmp_dir, "period.txt", "OT4");
	scoreboard_read_all_files();
	assert(scoreboard_get_period() == 7);

	cleanup_tmp_dir();
}

static void test_read_all_files_long_penalty_lines(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	write_file(g_tmp_dir, "clock.txt", "10:00");
	write_file(g_tmp_dir, "period.txt", "1");
	write_file(g_tmp_dir, "home_name.txt", "H");
	write_file(g_tmp_dir, "away_name.txt", "A");
	write_file(g_tmp_dir, "home_score.txt", "0");
	write_file(g_tmp_dir, "away_score.txt", "0");
	write_file(g_tmp_dir, "home_shots.txt", "0");
	write_file(g_tmp_dir, "away_shots.txt", "0");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "away_penalty_times.txt", "");

	/* Write a very long number line to exercise truncation */
	char long_num[128];
	memset(long_num, '1', 50);
	long_num[0] = '#';
	long_num[50] = '\0';
	char long_time[128];
	memset(long_time, '1', 50);
	long_time[50] = '\0';
	write_file(g_tmp_dir, "home_penalty_numbers.txt", long_num);
	write_file(g_tmp_dir, "home_penalty_times.txt", long_time);

	/* Should not crash; penalty won't parse properly but that's OK */
	scoreboard_read_all_files();

	cleanup_tmp_dir();
}

static void test_read_all_files_large_file(void)
{
	/* Exercise the read_text_file size clamping */
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	/* Create a file bigger than 512 bytes (the buf size in read_all_files) */
	char big[600];
	memset(big, 'A', 599);
	big[599] = '\0';
	write_file(g_tmp_dir, "home_name.txt", big);
	write_file(g_tmp_dir, "clock.txt", "10:00");
	write_file(g_tmp_dir, "period.txt", "1");
	write_file(g_tmp_dir, "away_name.txt", "A");
	write_file(g_tmp_dir, "home_score.txt", "0");
	write_file(g_tmp_dir, "away_score.txt", "0");
	write_file(g_tmp_dir, "home_shots.txt", "0");
	write_file(g_tmp_dir, "away_shots.txt", "0");
	write_file(g_tmp_dir, "home_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "home_penalty_times.txt", "");
	write_file(g_tmp_dir, "away_penalty_numbers.txt", "");
	write_file(g_tmp_dir, "away_penalty_times.txt", "");

	scoreboard_read_all_files();
	/* Name will be truncated to fit buffer — just verify no crash */
	assert(strlen(scoreboard_get_home_name()) > 0);

	cleanup_tmp_dir();
}

static void test_write_read_round_trip(void)
{
	/* Write game state via write_all_files, then read it back via
	   read_all_files and verify everything survives the round trip. */
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);

	scoreboard_set_home_name("Eagles");
	scoreboard_set_away_name("Hawks");
	scoreboard_set_home_score(4);
	scoreboard_set_away_score(2);
	scoreboard_set_home_shots(30);
	scoreboard_set_away_shots(22);
	scoreboard_set_period(2);
	scoreboard_clock_set_tenths(5100); /* 8:30 */
	scoreboard_home_penalty_add(12, 120);
	scoreboard_away_penalty_add(22, 60);

	bool ok = scoreboard_write_all_files();
	assert(ok);

	/* Wipe in-memory state but keep output directory */
	const char *dir = scoreboard_get_output_directory();
	char saved_dir[256];
	snprintf(saved_dir, sizeof(saved_dir), "%s", dir);
	scoreboard_reset_state_for_tests();
	scoreboard_set_output_directory(saved_dir);

	/* Read back from the files we just wrote */
	ok = scoreboard_read_all_files();
	assert(ok);

	assert(strcmp(scoreboard_get_home_name(), "Eagles") == 0);
	assert(strcmp(scoreboard_get_away_name(), "Hawks") == 0);
	assert(scoreboard_get_home_score() == 4);
	assert(scoreboard_get_away_score() == 2);
	assert(scoreboard_get_home_shots() == 30);
	assert(scoreboard_get_away_shots() == 22);
	assert(scoreboard_get_period() == 2);
	assert(scoreboard_clock_get_tenths() == 5100);
	assert(scoreboard_get_home_penalty_count() == 1);
	assert(scoreboard_get_home_penalty(0)->player_number == 12);
	assert(scoreboard_get_away_penalty_count() == 1);
	assert(scoreboard_get_away_penalty(0)->player_number == 22);

	cleanup_tmp_dir();
}

static void test_reset_state_wipes_output_directory(void)
{
	/* Regression: reset_state_for_tests clears the output directory.
	   The production startup must set the directory AFTER reset. */
	scoreboard_reset_state_for_tests();
	scoreboard_set_output_directory("/some/path");
	assert(strcmp(scoreboard_get_output_directory(), "/some/path") == 0);

	scoreboard_reset_state_for_tests();
	assert(strcmp(scoreboard_get_output_directory(), "") == 0);
}

static void test_startup_sequence_simulation(void)
{
	/* Simulates the real OBS startup: reset → set dir → read files.
	   Verifies that state written before "restart" survives. */
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();
	scoreboard_set_output_directory(g_tmp_dir);
	scoreboard_set_home_name("Wolves");
	scoreboard_set_away_name("Bears");
	scoreboard_set_home_score(3);
	scoreboard_set_away_score(1);
	scoreboard_set_period(2);
	scoreboard_clock_set_tenths(3000);
	scoreboard_home_penalty_add(10, 90);

	bool ok = scoreboard_write_all_files();
	assert(ok);

	/* Simulate OBS restart: reset wipes everything */
	scoreboard_reset_state_for_tests();
	assert(strcmp(scoreboard_get_home_name(), "Home") == 0);
	assert(scoreboard_get_home_score() == 0);
	assert(strcmp(scoreboard_get_output_directory(), "") == 0);

	/* Production code sets dir AFTER reset (from OBS profile) */
	scoreboard_set_output_directory(g_tmp_dir);
	ok = scoreboard_read_all_files();
	assert(ok);

	/* All state should be restored */
	assert(strcmp(scoreboard_get_home_name(), "Wolves") == 0);
	assert(strcmp(scoreboard_get_away_name(), "Bears") == 0);
	assert(scoreboard_get_home_score() == 3);
	assert(scoreboard_get_away_score() == 1);
	assert(scoreboard_get_period() == 2);
	assert(scoreboard_clock_get_tenths() == 3000);
	assert(scoreboard_get_home_penalty_count() == 1);
	assert(scoreboard_get_home_penalty(0)->player_number == 10);

	cleanup_tmp_dir();
}

int main(void)
{
	test_write_all_files();
	test_write_all_files_no_directory();
	test_write_all_files_null_directory();
	test_save_load_state();
	test_save_state_null();
	test_save_state_bad_path();
	test_load_state_null();
	test_load_state_bad_path();
	test_load_state_empty_file();
	test_action_log();
	test_action_log_null();
	test_action_log_null_buffer();
	test_action_log_ring_buffer();
	test_action_log_small_buffer();
	test_write_all_files_bad_directory();
	test_load_state_partial_json();
	test_load_state_invalid_bool();
	test_save_load_special_chars();
	test_load_state_string_not_quoted();
	test_cli_settings();
	test_cli_settings_null();
	test_output_directory_null();
	test_write_penalty_files_consolidated();
	test_read_all_files();
	test_read_all_files_no_directory();
	test_read_all_files_missing_files();
	test_read_all_files_ot_period();
	test_read_all_files_empty_penalties();
	test_read_all_files_no_player_number();
	test_read_all_files_invalid_clock();
	test_read_all_files_invalid_period();
	test_read_all_files_ot2_ot4();
	test_read_all_files_long_penalty_lines();
	test_read_all_files_large_file();
	test_write_read_round_trip();
	test_reset_state_wipes_output_directory();
	test_startup_sequence_simulation();

	printf("All scoreboard-core persistence tests passed.\n");
	return 0;
}
