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

static char g_tmp_dir[256];

static void setup_tmp_dir(void)
{
#ifdef _WIN32
	snprintf(g_tmp_dir, sizeof(g_tmp_dir), "%s\\scoreboard_evt_test_%d",
		 getenv("TEMP") ? getenv("TEMP") : ".", (int)getpid());
#else
	snprintf(g_tmp_dir, sizeof(g_tmp_dir), "/tmp/scoreboard_evt_test_%d",
		 (int)getpid());
#endif
	mkdir(g_tmp_dir, 0755);
}

static void cleanup_tmp_dir(void)
{
	char cmd[512];
#ifdef _WIN32
	snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", g_tmp_dir);
#else
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_tmp_dir);
#endif
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

/* ---- Tests ---- */

static void test_event_log_empty(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_event_log_count() == 0);
	assert(scoreboard_event_log_get(0) == NULL);
	assert(scoreboard_event_log_get(-1) == NULL);
}

static void test_event_log_add_and_get(void)
{
	scoreboard_reset_state_for_tests();

	int idx = scoreboard_event_log_add(0, "Stream Start");
	assert(idx == 0);
	assert(scoreboard_event_log_count() == 1);

	const struct scoreboard_game_event *ev = scoreboard_event_log_get(0);
	assert(ev != NULL);
	assert(ev->offset_seconds == 0);
	assert(strcmp(ev->label, "Stream Start") == 0);
}

static void test_event_log_multiple(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "Stream Start");
	scoreboard_event_log_add(754, "Period 1 Start");
	scoreboard_event_log_add(1322, "Goal: Eagles (1-0)");
	scoreboard_event_log_add(2145, "Penalty: Hawks #12");
	scoreboard_event_log_add(3610, "Period 1 End");

	assert(scoreboard_event_log_count() == 5);

	const struct scoreboard_game_event *ev = scoreboard_event_log_get(2);
	assert(ev != NULL);
	assert(ev->offset_seconds == 1322);
	assert(strcmp(ev->label, "Goal: Eagles (1-0)") == 0);

	ev = scoreboard_event_log_get(4);
	assert(ev != NULL);
	assert(ev->offset_seconds == 3610);
	assert(strcmp(ev->label, "Period 1 End") == 0);
}

static void test_event_log_clear(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "Stream Start");
	scoreboard_event_log_add(100, "Goal: Home (1-0)");
	assert(scoreboard_event_log_count() == 2);

	scoreboard_event_log_clear();
	assert(scoreboard_event_log_count() == 0);
	assert(scoreboard_event_log_get(0) == NULL);

	/* Can add again after clear */
	int idx = scoreboard_event_log_add(200, "New Event");
	assert(idx == 0);
	assert(scoreboard_event_log_count() == 1);
}

static void test_event_log_null_label(void)
{
	scoreboard_reset_state_for_tests();
	int idx = scoreboard_event_log_add(0, NULL);
	assert(idx == -1);
	assert(scoreboard_event_log_count() == 0);
}

static void test_event_log_negative_offset(void)
{
	scoreboard_reset_state_for_tests();
	int idx = scoreboard_event_log_add(-5, "Event");
	assert(idx == 0);
	const struct scoreboard_game_event *ev = scoreboard_event_log_get(0);
	assert(ev != NULL);
	assert(ev->offset_seconds == 0);
}

static void test_event_log_capacity(void)
{
	scoreboard_reset_state_for_tests();

	for (int i = 0; i < SCOREBOARD_MAX_EVENTS; i++) {
		char label[32];
		snprintf(label, sizeof(label), "Event %d", i);
		int idx = scoreboard_event_log_add(i * 10, label);
		assert(idx == i);
	}

	assert(scoreboard_event_log_count() == SCOREBOARD_MAX_EVENTS);

	/* Adding beyond capacity returns -1 */
	int idx = scoreboard_event_log_add(99999, "Overflow");
	assert(idx == -1);
	assert(scoreboard_event_log_count() == SCOREBOARD_MAX_EVENTS);
}

static void test_event_log_get_out_of_bounds(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_event_log_add(0, "Only Event");

	assert(scoreboard_event_log_get(-1) == NULL);
	assert(scoreboard_event_log_get(1) == NULL);
	assert(scoreboard_event_log_get(100) == NULL);
}

static void test_event_log_write(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	scoreboard_event_log_add(0, "Stream Start");
	scoreboard_event_log_add(754, "Period 1 Start");
	scoreboard_event_log_add(3661, "Goal: Eagles (1-0)");
	scoreboard_event_log_add(6300, "Game End");

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps.txt", g_tmp_dir);

	bool ok = scoreboard_event_log_write(path);
	assert(ok);

	char *content = read_file_content(path);
	assert(content != NULL);

	/* Verify format: H:MM:SS label */
	assert(strstr(content, "0:00:00 Stream Start\n") != NULL);
	assert(strstr(content, "0:12:34 Period 1 Start\n") != NULL);
	assert(strstr(content, "1:01:01 Goal: Eagles (1-0)\n") != NULL);
	assert(strstr(content, "1:45:00 Game End\n") != NULL);

	free(content);
	cleanup_tmp_dir();
}

static void test_event_log_write_empty(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_empty.txt", g_tmp_dir);

	bool ok = scoreboard_event_log_write(path);
	assert(ok);

	char *content = read_file_content(path);
	assert(content != NULL);
	assert(strlen(content) == 0);

	free(content);
	cleanup_tmp_dir();
}

static void test_event_log_write_null_path(void)
{
	scoreboard_reset_state_for_tests();
	bool ok = scoreboard_event_log_write(NULL);
	assert(!ok);
}

static void test_event_log_write_bad_path(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_event_log_add(0, "Event");
	bool ok = scoreboard_event_log_write("/nonexistent/dir/file.txt");
	assert(!ok);
}

static void test_event_log_reset_clears(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_event_log_add(0, "Event before reset");
	assert(scoreboard_event_log_count() == 1);

	scoreboard_reset_state_for_tests();
	assert(scoreboard_event_log_count() == 0);
}

static void test_event_log_large_offset(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	/* 10 hours, 5 minutes, 30 seconds */
	scoreboard_event_log_add(36330, "Late Event");

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_large.txt", g_tmp_dir);

	bool ok = scoreboard_event_log_write(path);
	assert(ok);

	char *content = read_file_content(path);
	assert(content != NULL);
	assert(strstr(content, "10:05:30 Late Event\n") != NULL);

	free(content);
	cleanup_tmp_dir();
}

static void test_event_log_long_label(void)
{
	scoreboard_reset_state_for_tests();

	/* Create a label that exceeds SCOREBOARD_EVENT_LABEL_SIZE */
	char long_label[256];
	memset(long_label, 'A', sizeof(long_label) - 1);
	long_label[sizeof(long_label) - 1] = '\0';

	int idx = scoreboard_event_log_add(0, long_label);
	assert(idx == 0);

	const struct scoreboard_game_event *ev = scoreboard_event_log_get(0);
	assert(ev != NULL);
	/* Label should be truncated to fit */
	assert(strlen(ev->label) == SCOREBOARD_EVENT_LABEL_SIZE - 1);
}

static void test_event_log_remove_middle(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "Stream Start");
	scoreboard_event_log_add(100, "Goal: Eagles (1-0)");
	scoreboard_event_log_add(200, "Goal: Hawks (1-1)");
	scoreboard_event_log_add(300, "Period 1 End");
	assert(scoreboard_event_log_count() == 4);

	/* Remove the Eagles goal (index 1) */
	bool removed = scoreboard_event_log_remove(1);
	assert(removed);
	assert(scoreboard_event_log_count() == 3);

	/* Remaining events should shift down */
	const struct scoreboard_game_event *ev = scoreboard_event_log_get(0);
	assert(strcmp(ev->label, "Stream Start") == 0);
	ev = scoreboard_event_log_get(1);
	assert(strcmp(ev->label, "Goal: Hawks (1-1)") == 0);
	ev = scoreboard_event_log_get(2);
	assert(strcmp(ev->label, "Period 1 End") == 0);
}

static void test_event_log_remove_first(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "A");
	scoreboard_event_log_add(10, "B");
	scoreboard_event_log_add(20, "C");

	bool removed = scoreboard_event_log_remove(0);
	assert(removed);
	assert(scoreboard_event_log_count() == 2);
	assert(strcmp(scoreboard_event_log_get(0)->label, "B") == 0);
	assert(strcmp(scoreboard_event_log_get(1)->label, "C") == 0);
}

static void test_event_log_remove_last(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "A");
	scoreboard_event_log_add(10, "B");

	bool removed = scoreboard_event_log_remove(1);
	assert(removed);
	assert(scoreboard_event_log_count() == 1);
	assert(strcmp(scoreboard_event_log_get(0)->label, "A") == 0);
}

static void test_event_log_remove_only(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "Only");

	bool removed = scoreboard_event_log_remove(0);
	assert(removed);
	assert(scoreboard_event_log_count() == 0);
}

static void test_event_log_remove_out_of_bounds(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "Event");
	assert(!scoreboard_event_log_remove(-1));
	assert(!scoreboard_event_log_remove(1));
	assert(!scoreboard_event_log_remove(100));
	assert(scoreboard_event_log_count() == 1);
}

static void test_event_log_remove_empty(void)
{
	scoreboard_reset_state_for_tests();
	assert(!scoreboard_event_log_remove(0));
}

static void test_event_log_find_last_basic(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "Stream Start");
	scoreboard_event_log_add(100, "Goal: Eagles (1-0)");
	scoreboard_event_log_add(200, "Goal: Hawks (1-1)");
	scoreboard_event_log_add(300, "Goal: Eagles (2-1)");

	/* Should find the last Eagles goal (index 3) */
	int idx = scoreboard_event_log_find_last("Goal: Eagles");
	assert(idx == 3);

	/* Should find the Hawks goal (index 2) */
	idx = scoreboard_event_log_find_last("Goal: Hawks");
	assert(idx == 2);
}

static void test_event_log_find_last_not_found(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "Stream Start");
	scoreboard_event_log_add(100, "Goal: Eagles (1-0)");

	int idx = scoreboard_event_log_find_last("Goal: Hawks");
	assert(idx == -1);
}

static void test_event_log_find_last_null(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_event_log_add(0, "Event");
	assert(scoreboard_event_log_find_last(NULL) == -1);
}

static void test_event_log_find_last_empty_log(void)
{
	scoreboard_reset_state_for_tests();
	assert(scoreboard_event_log_find_last("anything") == -1);
}

static void test_event_log_find_last_empty_prefix(void)
{
	scoreboard_reset_state_for_tests();
	scoreboard_event_log_add(0, "Event");
	/* Empty prefix should not match anything */
	assert(scoreboard_event_log_find_last("") == -1);
}

static void test_event_log_file_has_content(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_hc.txt", g_tmp_dir);

	/* Write a file with content */
	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fprintf(f, "0:00:00 Stream Start\n");
	fclose(f);

	assert(scoreboard_event_log_file_has_content(path));

	cleanup_tmp_dir();
}

static void test_event_log_file_has_content_empty(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_empty_hc.txt", g_tmp_dir);

	/* Write an empty file */
	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fclose(f);

	assert(!scoreboard_event_log_file_has_content(path));

	cleanup_tmp_dir();
}

static void test_event_log_file_has_content_missing(void)
{
	assert(!scoreboard_event_log_file_has_content(
		"/nonexistent/path/timestamps.txt"));
}

static void test_event_log_file_has_content_null(void)
{
	assert(!scoreboard_event_log_file_has_content(NULL));
}

static void test_event_log_read_basic(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	/* Add events and write them out */
	scoreboard_event_log_add(0, "Stream Start");
	scoreboard_event_log_add(754, "Period 1 Start");
	scoreboard_event_log_add(3661, "Goal: Eagles (1-0)");

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_read.txt", g_tmp_dir);
	assert(scoreboard_event_log_write(path));

	/* Clear and read back */
	scoreboard_event_log_clear();
	assert(scoreboard_event_log_count() == 0);

	int loaded = scoreboard_event_log_read(path);
	assert(loaded == 3);
	assert(scoreboard_event_log_count() == 3);

	const struct scoreboard_game_event *ev = scoreboard_event_log_get(0);
	assert(ev->offset_seconds == 0);
	assert(strcmp(ev->label, "Stream Start") == 0);

	ev = scoreboard_event_log_get(1);
	assert(ev->offset_seconds == 754);
	assert(strcmp(ev->label, "Period 1 Start") == 0);

	ev = scoreboard_event_log_get(2);
	assert(ev->offset_seconds == 3661);
	assert(strcmp(ev->label, "Goal: Eagles (1-0)") == 0);

	cleanup_tmp_dir();
}

static void test_event_log_read_empty_file(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_read_empty.txt", g_tmp_dir);

	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fclose(f);

	int loaded = scoreboard_event_log_read(path);
	assert(loaded == 0);
	assert(scoreboard_event_log_count() == 0);

	cleanup_tmp_dir();
}

static void test_event_log_read_bad_path(void)
{
	scoreboard_reset_state_for_tests();
	int loaded = scoreboard_event_log_read("/nonexistent/dir/file.txt");
	assert(loaded == -1);
}

static void test_event_log_read_null_path(void)
{
	scoreboard_reset_state_for_tests();
	int loaded = scoreboard_event_log_read(NULL);
	assert(loaded == -1);
}

static void test_event_log_read_malformed_lines(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_malformed.txt", g_tmp_dir);

	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fprintf(f, "0:00:00 Stream Start\n");
	fprintf(f, "this is not a valid line\n");
	fprintf(f, "\n");
	fprintf(f, "0:12:34 Period 1 Start\n");
	fprintf(f, "abc:de:fg Bad Time\n");
	fclose(f);

	int loaded = scoreboard_event_log_read(path);
	assert(loaded == 2);
	assert(scoreboard_event_log_count() == 2);

	const struct scoreboard_game_event *ev = scoreboard_event_log_get(0);
	assert(strcmp(ev->label, "Stream Start") == 0);
	ev = scoreboard_event_log_get(1);
	assert(strcmp(ev->label, "Period 1 Start") == 0);

	cleanup_tmp_dir();
}

static void test_event_log_read_preserves_existing(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	/* Add an event in memory first */
	scoreboard_event_log_add(0, "Existing Event");
	assert(scoreboard_event_log_count() == 1);

	/* Write a file with different events */
	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_preserve.txt", g_tmp_dir);

	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fprintf(f, "0:05:00 File Event 1\n");
	fprintf(f, "0:10:00 File Event 2\n");
	fclose(f);

	int loaded = scoreboard_event_log_read(path);
	assert(loaded == 2);
	assert(scoreboard_event_log_count() == 3);

	/* Original event still first */
	assert(strcmp(scoreboard_event_log_get(0)->label,
		     "Existing Event") == 0);
	assert(strcmp(scoreboard_event_log_get(1)->label,
		     "File Event 1") == 0);
	assert(strcmp(scoreboard_event_log_get(2)->label,
		     "File Event 2") == 0);

	cleanup_tmp_dir();
}

static void test_event_log_read_empty_label(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_emptylabel.txt", g_tmp_dir);

	FILE *f = fopen(path, "w");
	assert(f != NULL);
	/* Line with timestamp but no label after the space */
	fprintf(f, "0:00:00 \n");
	fprintf(f, "0:01:00 Valid Event\n");
	fclose(f);

	int loaded = scoreboard_event_log_read(path);
	assert(loaded == 1);
	assert(scoreboard_event_log_count() == 1);
	assert(strcmp(scoreboard_event_log_get(0)->label, "Valid Event") == 0);

	cleanup_tmp_dir();
}

static void test_event_log_read_capacity_limit(void)
{
	scoreboard_reset_state_for_tests();
	setup_tmp_dir();

	/* Fill event log to capacity */
	for (int i = 0; i < SCOREBOARD_MAX_EVENTS; i++) {
		char label[32];
		snprintf(label, sizeof(label), "Event %d", i);
		scoreboard_event_log_add(i, label);
	}
	assert(scoreboard_event_log_count() == SCOREBOARD_MAX_EVENTS);

	/* Write a file with one more event */
	char path[512];
	snprintf(path, sizeof(path), "%s/timestamps_cap.txt", g_tmp_dir);

	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fprintf(f, "0:00:00 Overflow Event\n");
	fclose(f);

	/* Read should stop at capacity */
	int loaded = scoreboard_event_log_read(path);
	assert(loaded == 0);
	assert(scoreboard_event_log_count() == SCOREBOARD_MAX_EVENTS);

	cleanup_tmp_dir();
}

static void test_event_log_find_and_remove(void)
{
	scoreboard_reset_state_for_tests();

	scoreboard_event_log_add(0, "Stream Start");
	scoreboard_event_log_add(100, "Power Play: Eagles #12");
	scoreboard_event_log_add(200, "Goal: Eagles (1-0)");
	scoreboard_event_log_add(300, "Power Play: Hawks #7");
	scoreboard_event_log_add(400, "Goal: Eagles (2-0)");

	/* Remove the last Eagles goal — simulates goal called off */
	int idx = scoreboard_event_log_find_last("Goal: Eagles");
	assert(idx == 4);
	scoreboard_event_log_remove(idx);
	assert(scoreboard_event_log_count() == 4);

	/* The first Eagles goal should still be there */
	idx = scoreboard_event_log_find_last("Goal: Eagles");
	assert(idx == 2);

	/* Remove the Eagles power play — simulates penalty called off */
	idx = scoreboard_event_log_find_last("Power Play: Eagles");
	assert(idx == 1);
	scoreboard_event_log_remove(idx);
	assert(scoreboard_event_log_count() == 3);

	/* Verify remaining events */
	assert(strcmp(scoreboard_event_log_get(0)->label, "Stream Start") == 0);
	assert(strcmp(scoreboard_event_log_get(1)->label,
		     "Goal: Eagles (1-0)") == 0);
	assert(strcmp(scoreboard_event_log_get(2)->label,
		     "Power Play: Hawks #7") == 0);
}

int main(void)
{
	test_event_log_empty();
	test_event_log_add_and_get();
	test_event_log_multiple();
	test_event_log_clear();
	test_event_log_null_label();
	test_event_log_negative_offset();
	test_event_log_capacity();
	test_event_log_get_out_of_bounds();
	test_event_log_write();
	test_event_log_write_empty();
	test_event_log_write_null_path();
	test_event_log_write_bad_path();
	test_event_log_reset_clears();
	test_event_log_large_offset();
	test_event_log_long_label();
	test_event_log_remove_middle();
	test_event_log_remove_first();
	test_event_log_remove_last();
	test_event_log_remove_only();
	test_event_log_remove_out_of_bounds();
	test_event_log_remove_empty();
	test_event_log_find_last_basic();
	test_event_log_find_last_not_found();
	test_event_log_find_last_null();
	test_event_log_find_last_empty_log();
	test_event_log_find_last_empty_prefix();
	test_event_log_find_and_remove();
	test_event_log_file_has_content();
	test_event_log_file_has_content_empty();
	test_event_log_file_has_content_missing();
	test_event_log_file_has_content_null();
	test_event_log_read_basic();
	test_event_log_read_empty_file();
	test_event_log_read_bad_path();
	test_event_log_read_null_path();
	test_event_log_read_malformed_lines();
	test_event_log_read_preserves_existing();
	test_event_log_read_empty_label();
	test_event_log_read_capacity_limit();
	printf("All event log tests passed!\n");
	return 0;
}
