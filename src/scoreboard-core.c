#include "scoreboard-core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCOREBOARD_MAX_NAME 65
#define SCOREBOARD_MAX_PATH 512
#define SCOREBOARD_ACTION_LOG_CAPACITY 64
#define SCOREBOARD_ACTION_LOG_ENTRY_SIZE 160
#define SCOREBOARD_DEFAULT_PERIOD_LENGTH 900
#define SCOREBOARD_DEFAULT_PENALTY_DURATION 120
#define SCOREBOARD_PENALTY_SLOTS SCOREBOARD_MAX_PENALTIES

static struct {
	int clock_tenths;
	bool clock_running;
	enum scoreboard_clock_direction clock_direction;
	int period_length;

	int period;
	bool overtime_enabled;
	int default_penalty_duration;

	char home_name[SCOREBOARD_MAX_NAME];
	char away_name[SCOREBOARD_MAX_NAME];

	int home_score;
	int away_score;
	int home_shots;
	int away_shots;

	struct scoreboard_penalty home_penalties[SCOREBOARD_PENALTY_SLOTS];
	struct scoreboard_penalty away_penalties[SCOREBOARD_PENALTY_SLOTS];

	char output_directory[SCOREBOARD_MAX_PATH];

	char cli_executable[SCOREBOARD_MAX_PATH];
	char main_config_path[SCOREBOARD_MAX_PATH];
	char override_config_path[SCOREBOARD_MAX_PATH];

	scoreboard_log_fn log_fn;

	char action_logs[SCOREBOARD_ACTION_LOG_CAPACITY]
			[SCOREBOARD_ACTION_LOG_ENTRY_SIZE];
	int action_log_head;
	int action_log_count;
} g_state;

/* ---- helpers ---- */

static void safe_copy(char *dst, const char *src, size_t dst_size)
{
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}
	size_t len = strlen(src);
	if (len >= dst_size)
		len = dst_size - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
}

static void log_message(enum scoreboard_log_level level, const char *msg)
{
	if (g_state.log_fn != NULL)
		g_state.log_fn(level, msg);
}

static bool read_text_file(const char *dir, const char *filename, char *buf,
			   size_t buf_size)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, filename);
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < 0 || (size_t)size >= buf_size)
		size = (long)(buf_size - 1);
	size_t n = fread(buf, 1, (size_t)size, f);
	buf[n] = '\0';
	fclose(f);
	return true;
}

static int parse_clock_text(const char *text)
{
	int minutes = 0, seconds = 0;
	if (sscanf(text, "%d:%d", &minutes, &seconds) == 2)
		return (minutes * 60 + seconds) * 10;
	return -1;
}

static int parse_period_text(const char *text)
{
	if (strcmp(text, "OT") == 0)
		return 4;
	if (strcmp(text, "OT2") == 0)
		return 5;
	if (strcmp(text, "OT3") == 0)
		return 6;
	if (strcmp(text, "OT4") == 0)
		return 7;
	int p = 0;
	if (sscanf(text, "%d", &p) == 1 && p >= 1 && p <= 3)
		return p;
	return -1;
}

static void parse_penalty_files(const char *numbers_text,
				const char *times_text, bool home)
{
	/* Clear all existing penalties for this team */
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (home)
			scoreboard_home_penalty_clear(i);
		else
			scoreboard_away_penalty_clear(i);
	}

	if (numbers_text[0] == '\0' || times_text[0] == '\0')
		return;

	/* Walk both texts line by line in parallel */
	const char *np = numbers_text;
	const char *tp = times_text;
	while (*np != '\0' && *tp != '\0') {
		/* Extract player number from current line */
		int player = 0;
		const char *nl = strchr(np, '\n');
		size_t nlen = nl ? (size_t)(nl - np) : strlen(np);
		char nline[32];
		if (nlen >= sizeof(nline))
			nlen = sizeof(nline) - 1;
		memcpy(nline, np, nlen);
		nline[nlen] = '\0';
		if (nline[0] == '#')
			sscanf(nline + 1, "%d", &player);

		/* Extract time from current line */
		const char *tl = strchr(tp, '\n');
		size_t tlen = tl ? (size_t)(tl - tp) : strlen(tp);
		char tline[32];
		if (tlen >= sizeof(tline))
			tlen = sizeof(tline) - 1;
		memcpy(tline, tp, tlen);
		tline[tlen] = '\0';
		int minutes = 0, seconds = 0;
		if (sscanf(tline, "%d:%d", &minutes, &seconds) == 2) {
			int duration_secs = minutes * 60 + seconds;
			if (duration_secs > 0) {
				if (home)
					scoreboard_home_penalty_add(
						player, duration_secs);
				else
					scoreboard_away_penalty_add(
						player, duration_secs);
			}
		}

		np = nl ? nl + 1 : np + nlen;
		tp = tl ? tl + 1 : tp + tlen;
	}
}

static bool write_text_file(const char *dir, const char *filename,
			    const char *content)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, filename);
	FILE *f = fopen(path, "w");
	if (f == NULL)
		return false;
	fprintf(f, "%s", content);
	fclose(f);
	return true;
}

static const char *find_json_value(const char *json, const char *key)
{
	char pattern[256];
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	const char *pos = strstr(json, pattern);
	if (pos == NULL)
		return NULL;
	pos += strlen(pattern);
	while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r' ||
	       *pos == ':')
		pos++;
	return pos;
}

static int parse_json_int(const char *json, const char *key, int default_val)
{
	const char *val = find_json_value(json, key);
	if (val == NULL)
		return default_val;
	return atoi(val);
}

static bool parse_json_bool(const char *json, const char *key, bool default_val)
{
	const char *val = find_json_value(json, key);
	if (val == NULL)
		return default_val;
	if (strncmp(val, "true", 4) == 0)
		return true;
	if (strncmp(val, "false", 5) == 0)
		return false;
	return default_val;
}

static void parse_json_string(const char *json, const char *key, char *out,
			      size_t out_size)
{
	const char *val = find_json_value(json, key);
	if (val == NULL || *val != '"') {
		out[0] = '\0';
		return;
	}
	val++;
	size_t i = 0;
	while (*val != '\0' && *val != '"' && i < out_size - 1) {
		if (*val == '\\' && *(val + 1) != '\0')
			val++;
		out[i++] = *val++;
	}
	out[i] = '\0';
}

static void write_json_string(FILE *f, const char *key, const char *value,
			      bool last)
{
	fprintf(f, "  \"%s\": \"", key);
	for (const char *p = value; *p != '\0'; p++) {
		if (*p == '"' || *p == '\\')
			fputc('\\', f);
		fputc(*p, f);
	}
	fprintf(f, last ? "\"\n" : "\",\n");
}

/* ---- lifecycle ---- */

const char *scoreboard_description(void)
{
	return "Streamn Scoreboard";
}

bool scoreboard_on_load(scoreboard_log_fn log_fn)
{
	g_state.log_fn = log_fn;
	log_message(SCOREBOARD_LOG_INFO,
		    "[streamn-obs-scoreboard] module loaded");
	return true;
}

void scoreboard_on_unload(scoreboard_log_fn log_fn)
{
	if (log_fn != NULL)
		log_fn(SCOREBOARD_LOG_INFO,
		       "[streamn-obs-scoreboard] module unloaded");
	g_state.log_fn = NULL;
}

void scoreboard_reset_state_for_tests(void)
{
	memset(&g_state, 0, sizeof(g_state));
	g_state.period = 1;
	g_state.period_length = SCOREBOARD_DEFAULT_PERIOD_LENGTH;
	g_state.clock_direction = SCOREBOARD_CLOCK_COUNT_DOWN;
	g_state.clock_tenths = SCOREBOARD_DEFAULT_PERIOD_LENGTH * 10;
	g_state.overtime_enabled = true;
	g_state.default_penalty_duration = SCOREBOARD_DEFAULT_PENALTY_DURATION;
	safe_copy(g_state.home_name, "Home", sizeof(g_state.home_name));
	safe_copy(g_state.away_name, "Away", sizeof(g_state.away_name));
}

/* ---- clock ---- */

void scoreboard_clock_start(void)
{
	g_state.clock_running = true;
}

void scoreboard_clock_stop(void)
{
	g_state.clock_running = false;
}

bool scoreboard_clock_is_running(void)
{
	return g_state.clock_running;
}

void scoreboard_clock_reset(void)
{
	g_state.clock_running = false;
	if (g_state.clock_direction == SCOREBOARD_CLOCK_COUNT_DOWN)
		g_state.clock_tenths = g_state.period_length * 10;
	else
		g_state.clock_tenths = 0;
}

void scoreboard_clock_tick(int elapsed_tenths)
{
	if (!g_state.clock_running)
		return;

	if (g_state.clock_direction == SCOREBOARD_CLOCK_COUNT_DOWN) {
		g_state.clock_tenths -= elapsed_tenths;
		if (g_state.clock_tenths < 0)
			g_state.clock_tenths = 0;
	} else {
		g_state.clock_tenths += elapsed_tenths;
		int max_tenths = g_state.period_length * 10;
		if (g_state.clock_tenths > max_tenths)
			g_state.clock_tenths = max_tenths;
	}

	scoreboard_penalty_tick(elapsed_tenths);
}

int scoreboard_clock_get_tenths(void)
{
	return g_state.clock_tenths;
}

void scoreboard_clock_set_tenths(int tenths)
{
	if (tenths < 0)
		tenths = 0;
	g_state.clock_tenths = tenths;
}

void scoreboard_clock_adjust_seconds(int delta)
{
	g_state.clock_tenths += delta * 10;
	if (g_state.clock_tenths < 0)
		g_state.clock_tenths = 0;
	scoreboard_penalty_adjust(delta * 10);
}

void scoreboard_clock_adjust_minutes(int delta)
{
	g_state.clock_tenths += delta * 600;
	if (g_state.clock_tenths < 0)
		g_state.clock_tenths = 0;
	scoreboard_penalty_adjust(delta * 600);
}

void scoreboard_clock_format(char *buf, size_t size)
{
	if (buf == NULL || size == 0)
		return;
	int total_seconds = g_state.clock_tenths / 10;
	int minutes = total_seconds / 60;
	int seconds = total_seconds % 60;
	snprintf(buf, size, "%d:%02d", minutes, seconds);
}

void scoreboard_set_clock_direction(enum scoreboard_clock_direction dir)
{
	g_state.clock_direction = dir;
}

enum scoreboard_clock_direction scoreboard_get_clock_direction(void)
{
	return g_state.clock_direction;
}

void scoreboard_set_period_length(int seconds)
{
	if (seconds < 1)
		seconds = 1;
	g_state.period_length = seconds;
}

int scoreboard_get_period_length(void)
{
	return g_state.period_length;
}

/* ---- period ---- */

int scoreboard_get_period(void)
{
	return g_state.period;
}

void scoreboard_set_period(int period)
{
	if (period < 1)
		period = 1;
	int max_period = g_state.overtime_enabled ? 7 : 3;
	if (period > max_period)
		period = max_period;
	g_state.period = period;
}

void scoreboard_period_advance(void)
{
	int max_period = g_state.overtime_enabled ? 7 : 3;
	if (g_state.period < max_period) {
		g_state.period++;
		scoreboard_clock_reset();
	}
}

void scoreboard_period_rewind(void)
{
	if (g_state.period > 1) {
		g_state.period--;
		scoreboard_clock_reset();
	}
}

void scoreboard_format_period(char *buf, size_t size)
{
	if (buf == NULL || size == 0)
		return;
	if (g_state.period == 4)
		snprintf(buf, size, "OT");
	else if (g_state.period >= 5)
		snprintf(buf, size, "OT%d", g_state.period - 3);
	else
		snprintf(buf, size, "%d", g_state.period);
}

void scoreboard_set_overtime_enabled(bool enabled)
{
	g_state.overtime_enabled = enabled;
}

bool scoreboard_get_overtime_enabled(void)
{
	return g_state.overtime_enabled;
}

void scoreboard_set_default_penalty_duration(int seconds)
{
	if (seconds < 1)
		seconds = 1;
	g_state.default_penalty_duration = seconds;
}

int scoreboard_get_default_penalty_duration(void)
{
	return g_state.default_penalty_duration;
}

/* ---- team names ---- */

void scoreboard_set_home_name(const char *name)
{
	safe_copy(g_state.home_name, name, sizeof(g_state.home_name));
}

const char *scoreboard_get_home_name(void)
{
	return g_state.home_name;
}

void scoreboard_set_away_name(const char *name)
{
	safe_copy(g_state.away_name, name, sizeof(g_state.away_name));
}

const char *scoreboard_get_away_name(void)
{
	return g_state.away_name;
}

/* ---- score ---- */

int scoreboard_get_home_score(void)
{
	return g_state.home_score;
}

void scoreboard_set_home_score(int score)
{
	g_state.home_score = score < 0 ? 0 : score;
}

void scoreboard_increment_home_score(void)
{
	g_state.home_score++;
}

void scoreboard_decrement_home_score(void)
{
	if (g_state.home_score > 0)
		g_state.home_score--;
}

int scoreboard_get_away_score(void)
{
	return g_state.away_score;
}

void scoreboard_set_away_score(int score)
{
	g_state.away_score = score < 0 ? 0 : score;
}

void scoreboard_increment_away_score(void)
{
	g_state.away_score++;
}

void scoreboard_decrement_away_score(void)
{
	if (g_state.away_score > 0)
		g_state.away_score--;
}

/* ---- shots ---- */

int scoreboard_get_home_shots(void)
{
	return g_state.home_shots;
}

void scoreboard_set_home_shots(int shots)
{
	g_state.home_shots = shots < 0 ? 0 : shots;
}

void scoreboard_increment_home_shots(void)
{
	g_state.home_shots++;
}

void scoreboard_decrement_home_shots(void)
{
	if (g_state.home_shots > 0)
		g_state.home_shots--;
}

int scoreboard_get_away_shots(void)
{
	return g_state.away_shots;
}

void scoreboard_set_away_shots(int shots)
{
	g_state.away_shots = shots < 0 ? 0 : shots;
}

void scoreboard_increment_away_shots(void)
{
	g_state.away_shots++;
}

void scoreboard_decrement_away_shots(void)
{
	if (g_state.away_shots > 0)
		g_state.away_shots--;
}

/* ---- penalties ---- */

int scoreboard_home_penalty_add(int player_number, int duration_secs)
{
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (!g_state.home_penalties[i].active) {
			g_state.home_penalties[i].player_number = player_number;
			g_state.home_penalties[i].remaining_tenths =
				duration_secs * 10;
			g_state.home_penalties[i].active = true;
			return i;
		}
	}
	return -1;
}

void scoreboard_home_penalty_clear(int slot)
{
	if (slot >= 0 && slot < SCOREBOARD_PENALTY_SLOTS) {
		g_state.home_penalties[slot].active = false;
		g_state.home_penalties[slot].player_number = 0;
		g_state.home_penalties[slot].remaining_tenths = 0;
	}
}

const struct scoreboard_penalty *scoreboard_get_home_penalty(int slot)
{
	if (slot < 0 || slot >= SCOREBOARD_PENALTY_SLOTS)
		return NULL;
	return &g_state.home_penalties[slot];
}

int scoreboard_away_penalty_add(int player_number, int duration_secs)
{
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (!g_state.away_penalties[i].active) {
			g_state.away_penalties[i].player_number = player_number;
			g_state.away_penalties[i].remaining_tenths =
				duration_secs * 10;
			g_state.away_penalties[i].active = true;
			return i;
		}
	}
	return -1;
}

void scoreboard_away_penalty_clear(int slot)
{
	if (slot >= 0 && slot < SCOREBOARD_PENALTY_SLOTS) {
		g_state.away_penalties[slot].active = false;
		g_state.away_penalties[slot].player_number = 0;
		g_state.away_penalties[slot].remaining_tenths = 0;
	}
}

const struct scoreboard_penalty *scoreboard_get_away_penalty(int slot)
{
	if (slot < 0 || slot >= SCOREBOARD_PENALTY_SLOTS)
		return NULL;
	return &g_state.away_penalties[slot];
}

int scoreboard_get_home_penalty_count(void)
{
	int count = 0;
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (g_state.home_penalties[i].active)
			count++;
	}
	return count;
}

int scoreboard_get_away_penalty_count(void)
{
	int count = 0;
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (g_state.away_penalties[i].active)
			count++;
	}
	return count;
}

void scoreboard_penalty_tick(int elapsed_tenths)
{
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (g_state.home_penalties[i].active) {
			g_state.home_penalties[i].remaining_tenths -=
				elapsed_tenths;
			if (g_state.home_penalties[i].remaining_tenths <= 0)
				scoreboard_home_penalty_clear(i);
		}
		if (g_state.away_penalties[i].active) {
			g_state.away_penalties[i].remaining_tenths -=
				elapsed_tenths;
			if (g_state.away_penalties[i].remaining_tenths <= 0)
				scoreboard_away_penalty_clear(i);
		}
	}
}

void scoreboard_penalty_adjust(int delta_tenths)
{
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (g_state.home_penalties[i].active) {
			g_state.home_penalties[i].remaining_tenths +=
				delta_tenths;
			if (g_state.home_penalties[i].remaining_tenths <= 0)
				scoreboard_home_penalty_clear(i);
		}
		if (g_state.away_penalties[i].active) {
			g_state.away_penalties[i].remaining_tenths +=
				delta_tenths;
			if (g_state.away_penalties[i].remaining_tenths <= 0)
				scoreboard_away_penalty_clear(i);
		}
	}
}

void scoreboard_format_penalty_number(int slot, bool home, char *buf,
				      size_t size)
{
	if (buf == NULL || size == 0)
		return;
	buf[0] = '\0';
	if (slot < 0 || slot >= SCOREBOARD_PENALTY_SLOTS)
		return;
	const struct scoreboard_penalty *p =
		home ? &g_state.home_penalties[slot]
		     : &g_state.away_penalties[slot];
	if (!p->active)
		return;
	if (p->player_number > 0)
		snprintf(buf, size, "#%d", p->player_number);
	else
		snprintf(buf, size, " ");
}

void scoreboard_format_penalty_time(int slot, bool home, char *buf, size_t size)
{
	if (buf == NULL || size == 0)
		return;
	buf[0] = '\0';
	if (slot < 0 || slot >= SCOREBOARD_PENALTY_SLOTS)
		return;
	const struct scoreboard_penalty *p =
		home ? &g_state.home_penalties[slot]
		     : &g_state.away_penalties[slot];
	if (!p->active)
		return;
	int total_seconds = p->remaining_tenths / 10;
	int minutes = total_seconds / 60;
	int seconds = total_seconds % 60;
	snprintf(buf, size, "%d:%02d", minutes, seconds);
}

void scoreboard_format_all_penalty_numbers(bool home, char *buf, size_t size)
{
	if (buf == NULL || size == 0)
		return;
	buf[0] = '\0';
	const struct scoreboard_penalty *penalties =
		home ? g_state.home_penalties : g_state.away_penalties;
	size_t offset = 0;
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (!penalties[i].active)
			continue;
		char line[32];
		if (penalties[i].player_number > 0)
			snprintf(line, sizeof(line), "#%d",
				 penalties[i].player_number);
		else
			snprintf(line, sizeof(line), " ");
		size_t len = strlen(line);
		size_t need = (offset > 0 ? 1 : 0) + len;
		if (offset + need >= size)
			break;
		if (offset > 0)
			buf[offset++] = '\n';
		memcpy(buf + offset, line, len);
		offset += len;
	}
	buf[offset] = '\0';
}

void scoreboard_format_all_penalty_times(bool home, char *buf, size_t size)
{
	if (buf == NULL || size == 0)
		return;
	buf[0] = '\0';
	const struct scoreboard_penalty *penalties =
		home ? g_state.home_penalties : g_state.away_penalties;
	size_t offset = 0;
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (!penalties[i].active)
			continue;
		int total_seconds = penalties[i].remaining_tenths / 10;
		int minutes = total_seconds / 60;
		int seconds = total_seconds % 60;
		char line[32];
		snprintf(line, sizeof(line), "%d:%02d", minutes, seconds);
		size_t len = strlen(line);
		size_t need = (offset > 0 ? 1 : 0) + len;
		if (offset + need >= size)
			break;
		if (offset > 0)
			buf[offset++] = '\n';
		memcpy(buf + offset, line, len);
		offset += len;
	}
	buf[offset] = '\0';
}

/* ---- file output ---- */

void scoreboard_set_output_directory(const char *path)
{
	safe_copy(g_state.output_directory, path,
		  sizeof(g_state.output_directory));
}

const char *scoreboard_get_output_directory(void)
{
	return g_state.output_directory;
}

bool scoreboard_write_all_files(void)
{
	const char *dir = g_state.output_directory;
	if (dir[0] == '\0')
		return false;

	char buf[64];
	bool ok = true;

	scoreboard_clock_format(buf, sizeof(buf));
	ok = write_text_file(dir, "clock.txt", buf) && ok;

	scoreboard_format_period(buf, sizeof(buf));
	ok = write_text_file(dir, "period.txt", buf) && ok;

	ok = write_text_file(dir, "home_name.txt", g_state.home_name) && ok;
	ok = write_text_file(dir, "away_name.txt", g_state.away_name) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.home_score);
	ok = write_text_file(dir, "home_score.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.away_score);
	ok = write_text_file(dir, "away_score.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.home_shots);
	ok = write_text_file(dir, "home_shots.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.away_shots);
	ok = write_text_file(dir, "away_shots.txt", buf) && ok;

	char pen_buf[512];

	scoreboard_format_all_penalty_numbers(true, pen_buf, sizeof(pen_buf));
	ok = write_text_file(dir, "home_penalty_numbers.txt", pen_buf) && ok;

	scoreboard_format_all_penalty_times(true, pen_buf, sizeof(pen_buf));
	ok = write_text_file(dir, "home_penalty_times.txt", pen_buf) && ok;

	scoreboard_format_all_penalty_numbers(false, pen_buf, sizeof(pen_buf));
	ok = write_text_file(dir, "away_penalty_numbers.txt", pen_buf) && ok;

	scoreboard_format_all_penalty_times(false, pen_buf, sizeof(pen_buf));
	ok = write_text_file(dir, "away_penalty_times.txt", pen_buf) && ok;

	return ok;
}

bool scoreboard_read_all_files(void)
{
	const char *dir = g_state.output_directory;
	if (dir[0] == '\0')
		return false;

	char buf[512];
	bool ok = true;

	if (read_text_file(dir, "clock.txt", buf, sizeof(buf))) {
		int tenths = parse_clock_text(buf);
		if (tenths >= 0)
			g_state.clock_tenths = tenths;
	} else {
		ok = false;
	}

	if (read_text_file(dir, "period.txt", buf, sizeof(buf))) {
		int p = parse_period_text(buf);
		if (p > 0)
			scoreboard_set_period(p);
	} else {
		ok = false;
	}

	if (read_text_file(dir, "home_name.txt", buf, sizeof(buf)))
		scoreboard_set_home_name(buf);
	else
		ok = false;

	if (read_text_file(dir, "away_name.txt", buf, sizeof(buf)))
		scoreboard_set_away_name(buf);
	else
		ok = false;

	if (read_text_file(dir, "home_score.txt", buf, sizeof(buf)))
		scoreboard_set_home_score(atoi(buf));
	else
		ok = false;

	if (read_text_file(dir, "away_score.txt", buf, sizeof(buf)))
		scoreboard_set_away_score(atoi(buf));
	else
		ok = false;

	if (read_text_file(dir, "home_shots.txt", buf, sizeof(buf)))
		scoreboard_set_home_shots(atoi(buf));
	else
		ok = false;

	if (read_text_file(dir, "away_shots.txt", buf, sizeof(buf)))
		scoreboard_set_away_shots(atoi(buf));
	else
		ok = false;

	char nums_buf[512], times_buf[512];
	bool hn = read_text_file(dir, "home_penalty_numbers.txt", nums_buf,
				 sizeof(nums_buf));
	bool ht = read_text_file(dir, "home_penalty_times.txt", times_buf,
				 sizeof(times_buf));
	if (hn && ht)
		parse_penalty_files(nums_buf, times_buf, true);
	else if (!hn || !ht)
		ok = false;

	bool an = read_text_file(dir, "away_penalty_numbers.txt", nums_buf,
				 sizeof(nums_buf));
	bool at = read_text_file(dir, "away_penalty_times.txt", times_buf,
				 sizeof(times_buf));
	if (an && at)
		parse_penalty_files(nums_buf, times_buf, false);
	else if (!an || !at)
		ok = false;

	return ok;
}

/* ---- state persistence ---- */

bool scoreboard_save_state(const char *path)
{
	if (path == NULL)
		return false;
	FILE *f = fopen(path, "w");
	if (f == NULL)
		return false;

	fprintf(f, "{\n");
	fprintf(f, "  \"clock_tenths\": %d,\n", g_state.clock_tenths);
	fprintf(f, "  \"clock_running\": %s,\n",
		g_state.clock_running ? "true" : "false");
	fprintf(f, "  \"clock_direction\": %d,\n",
		(int)g_state.clock_direction);
	fprintf(f, "  \"period_length\": %d,\n", g_state.period_length);
	fprintf(f, "  \"period\": %d,\n", g_state.period);
	fprintf(f, "  \"overtime_enabled\": %s,\n",
		g_state.overtime_enabled ? "true" : "false");
	write_json_string(f, "home_name", g_state.home_name, false);
	write_json_string(f, "away_name", g_state.away_name, false);
	fprintf(f, "  \"home_score\": %d,\n", g_state.home_score);
	fprintf(f, "  \"away_score\": %d,\n", g_state.away_score);
	fprintf(f, "  \"home_shots\": %d,\n", g_state.home_shots);
	fprintf(f, "  \"away_shots\": %d,\n", g_state.away_shots);

	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		fprintf(f, "  \"home_penalty%d_number\": %d,\n", i,
			g_state.home_penalties[i].player_number);
		fprintf(f, "  \"home_penalty%d_tenths\": %d,\n", i,
			g_state.home_penalties[i].remaining_tenths);
		fprintf(f, "  \"home_penalty%d_active\": %s,\n", i,
			g_state.home_penalties[i].active ? "true" : "false");
	}
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		bool last = (i == SCOREBOARD_PENALTY_SLOTS - 1);
		fprintf(f, "  \"away_penalty%d_number\": %d,\n", i,
			g_state.away_penalties[i].player_number);
		fprintf(f, "  \"away_penalty%d_tenths\": %d,\n", i,
			g_state.away_penalties[i].remaining_tenths);
		fprintf(f, "  \"away_penalty%d_active\": %s%s\n", i,
			g_state.away_penalties[i].active ? "true" : "false",
			last ? "" : ",");
	}

	fprintf(f, "}\n");
	fclose(f);
	return true;
}

bool scoreboard_load_state(const char *path)
{
	if (path == NULL)
		return false;
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return false;

	fseek(f, 0, SEEK_END);
	long file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (file_size <= 0 || file_size > 65536) {
		fclose(f);
		return false;
	}

	char *json = (char *)malloc((size_t)file_size + 1);
	size_t read_size = fread(json, 1, (size_t)file_size, f);
	fclose(f);
	json[read_size] = '\0';

	g_state.clock_tenths =
		parse_json_int(json, "clock_tenths", g_state.clock_tenths);
	g_state.clock_running =
		parse_json_bool(json, "clock_running", g_state.clock_running);
	g_state.clock_direction = (enum scoreboard_clock_direction)parse_json_int(
		json, "clock_direction", (int)g_state.clock_direction);
	g_state.period_length =
		parse_json_int(json, "period_length", g_state.period_length);
	g_state.period = parse_json_int(json, "period", g_state.period);
	g_state.overtime_enabled = parse_json_bool(json, "overtime_enabled",
						   g_state.overtime_enabled);

	parse_json_string(json, "home_name", g_state.home_name,
			  sizeof(g_state.home_name));
	parse_json_string(json, "away_name", g_state.away_name,
			  sizeof(g_state.away_name));

	g_state.home_score =
		parse_json_int(json, "home_score", g_state.home_score);
	g_state.away_score =
		parse_json_int(json, "away_score", g_state.away_score);
	g_state.home_shots =
		parse_json_int(json, "home_shots", g_state.home_shots);
	g_state.away_shots =
		parse_json_int(json, "away_shots", g_state.away_shots);

	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		char key[64];
		snprintf(key, sizeof(key), "home_penalty%d_number", i);
		g_state.home_penalties[i].player_number = parse_json_int(
			json, key, g_state.home_penalties[i].player_number);
		snprintf(key, sizeof(key), "home_penalty%d_tenths", i);
		g_state.home_penalties[i].remaining_tenths = parse_json_int(
			json, key, g_state.home_penalties[i].remaining_tenths);
		snprintf(key, sizeof(key), "home_penalty%d_active", i);
		g_state.home_penalties[i].active = parse_json_bool(
			json, key, g_state.home_penalties[i].active);

		snprintf(key, sizeof(key), "away_penalty%d_number", i);
		g_state.away_penalties[i].player_number = parse_json_int(
			json, key, g_state.away_penalties[i].player_number);
		snprintf(key, sizeof(key), "away_penalty%d_tenths", i);
		g_state.away_penalties[i].remaining_tenths = parse_json_int(
			json, key, g_state.away_penalties[i].remaining_tenths);
		snprintf(key, sizeof(key), "away_penalty%d_active", i);
		g_state.away_penalties[i].active = parse_json_bool(
			json, key, g_state.away_penalties[i].active);
	}

	free(json);
	return true;
}

/* ---- game management ---- */

void scoreboard_new_game(void)
{
	g_state.home_score = 0;
	g_state.away_score = 0;
	g_state.home_shots = 0;
	g_state.away_shots = 0;
	g_state.period = 1;
	g_state.clock_running = false;

	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		g_state.home_penalties[i].active = false;
		g_state.home_penalties[i].player_number = 0;
		g_state.home_penalties[i].remaining_tenths = 0;
		g_state.away_penalties[i].active = false;
		g_state.away_penalties[i].player_number = 0;
		g_state.away_penalties[i].remaining_tenths = 0;
	}

	if (g_state.clock_direction == SCOREBOARD_CLOCK_COUNT_DOWN)
		g_state.clock_tenths = g_state.period_length * 10;
	else
		g_state.clock_tenths = 0;
}

/* ---- CLI settings ---- */

void scoreboard_set_cli_executable(const char *path)
{
	safe_copy(g_state.cli_executable, path,
		  sizeof(g_state.cli_executable));
}

const char *scoreboard_get_cli_executable(void)
{
	return g_state.cli_executable;
}

void scoreboard_set_main_config_path(const char *path)
{
	safe_copy(g_state.main_config_path, path,
		  sizeof(g_state.main_config_path));
}

const char *scoreboard_get_main_config_path(void)
{
	return g_state.main_config_path;
}

void scoreboard_set_override_config_path(const char *path)
{
	safe_copy(g_state.override_config_path, path,
		  sizeof(g_state.override_config_path));
}

const char *scoreboard_get_override_config_path(void)
{
	return g_state.override_config_path;
}

/* ---- action log ---- */

void scoreboard_add_action_log(const char *message)
{
	if (message == NULL)
		return;
	safe_copy(g_state.action_logs[g_state.action_log_head], message,
		  SCOREBOARD_ACTION_LOG_ENTRY_SIZE);
	g_state.action_log_head =
		(g_state.action_log_head + 1) % SCOREBOARD_ACTION_LOG_CAPACITY;
	if (g_state.action_log_count < SCOREBOARD_ACTION_LOG_CAPACITY)
		g_state.action_log_count++;
}

size_t scoreboard_copy_action_logs(char *buffer, size_t buffer_size)
{
	if (buffer == NULL || buffer_size == 0)
		return 0;
	buffer[0] = '\0';

	size_t written = 0;
	int start;
	if (g_state.action_log_count < SCOREBOARD_ACTION_LOG_CAPACITY)
		start = 0;
	else
		start = g_state.action_log_head;

	for (int i = 0; i < g_state.action_log_count; i++) {
		int idx = (start + i) % SCOREBOARD_ACTION_LOG_CAPACITY;
		size_t entry_len = strlen(g_state.action_logs[idx]);
		if (written + entry_len + 2 > buffer_size)
			break;
		if (written > 0) {
			buffer[written++] = '\n';
		}
		memcpy(buffer + written, g_state.action_logs[idx], entry_len);
		written += entry_len;
		buffer[written] = '\0';
	}

	return written;
}
