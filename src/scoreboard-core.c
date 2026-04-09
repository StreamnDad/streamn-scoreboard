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
#define SCOREBOARD_DEFAULT_MAJOR_PENALTY_DURATION 300
#define SCOREBOARD_PENALTY_SLOTS SCOREBOARD_MAX_PENALTIES
#define SCOREBOARD_SEGMENT_NAME_SIZE 16

static const struct scoreboard_sport_preset k_sport_presets[SCOREBOARD_SPORT_COUNT] = {
	/* sport, segment_name, segment_count, duration_seconds, ot_max, has_shots, has_faceoffs, has_penalties, default_direction, has_fouls, foul_label, foul_label2, log_scores, score_label, default_penalty_secs, default_major_penalty_secs */
	{SCOREBOARD_SPORT_HOCKEY,     "Period",  3, 900,  4, true,  true,  true,  SCOREBOARD_CLOCK_COUNT_DOWN, false, "",      "", true,  "Goal",  120, 300},
	{SCOREBOARD_SPORT_BASKETBALL, "Quarter", 4, 480,  1, false, false, false, SCOREBOARD_CLOCK_COUNT_DOWN, true,  "Fouls", "", false, "Score", 0,   0},
	{SCOREBOARD_SPORT_SOCCER,     "Half",    2, 2700, 1, false, false, false, SCOREBOARD_CLOCK_COUNT_UP,   true,  "YC",    "RC", true,  "Goal",  0,   0},
	{SCOREBOARD_SPORT_FOOTBALL,   "Half",    2, 1800, 1, false, false, false, SCOREBOARD_CLOCK_COUNT_DOWN, true,  "Flags", "", false, "Score", 0,   0},
	{SCOREBOARD_SPORT_LACROSSE,   "Quarter", 4, 720,  1, true,  true,  true,  SCOREBOARD_CLOCK_COUNT_DOWN, false, "",      "", true,  "Goal",  60,  180},
	{SCOREBOARD_SPORT_RUGBY,      "Half",    2, 2400, 1, false, false, true,  SCOREBOARD_CLOCK_COUNT_UP,   false, "",      "", true,  "Try",   120, 600},
	{SCOREBOARD_SPORT_GENERIC,    "Segment", 1, 0,    0, false, false, false, SCOREBOARD_CLOCK_COUNT_UP,   false, "",      "", true,  "Score", 120, 300},
};

static struct {
	int clock_tenths;
	bool clock_running;
	enum scoreboard_clock_direction clock_direction;
	int period_length;

	int period;
	bool overtime_enabled;
	int default_penalty_duration;
	int default_major_penalty_duration;

	enum scoreboard_sport sport;
	char segment_name[SCOREBOARD_SEGMENT_NAME_SIZE];
	int segment_count;
	int ot_max;
	bool has_shots;
	bool has_penalties;

	char period_labels[SCOREBOARD_MAX_PERIOD_LABELS]
			  [SCOREBOARD_PERIOD_LABEL_SIZE];
	int period_label_count;

	char home_name[SCOREBOARD_MAX_NAME];
	char away_name[SCOREBOARD_MAX_NAME];

	int home_score;
	int away_score;
	int home_shots;
	int away_shots;
	int home_faceoffs;
	int away_faceoffs;
	bool has_faceoffs;
	int home_fouls;
	int away_fouls;
	bool has_fouls;
	char foul_label[16];
	int home_fouls2;
	int away_fouls2;
	char foul_label2[16];
	bool log_scores;
	char score_label[16];

	struct scoreboard_penalty home_penalties[SCOREBOARD_PENALTY_SLOTS];
	struct scoreboard_penalty away_penalties[SCOREBOARD_PENALTY_SLOTS];

	char output_directory[SCOREBOARD_MAX_PATH];

	char cli_executable[SCOREBOARD_MAX_PATH];
	char cli_extra_args[SCOREBOARD_MAX_PATH];

	scoreboard_log_fn log_fn;

	char action_logs[SCOREBOARD_ACTION_LOG_CAPACITY]
			[SCOREBOARD_ACTION_LOG_ENTRY_SIZE];
	int action_log_head;
	int action_log_count;
} g_state;

static bool g_dirty;

/* ---- game event log ---- */
static struct scoreboard_game_event
	g_event_log[SCOREBOARD_MAX_EVENTS];
static int g_event_count;

/* ---- helpers ---- */

static void mark_dirty(void)
{
	g_dirty = true;
}

bool scoreboard_is_dirty(void)
{
	return g_dirty;
}

void scoreboard_mark_dirty(void)
{
	g_dirty = true;
}

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

static void generate_default_period_labels(void);

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
	/* Search labels array for an exact match */
	for (int i = 0; i < g_state.period_label_count; i++) {
		if (strcmp(text, g_state.period_labels[i]) == 0)
			return i + 1;
	}
	/* Fallback: try parsing as a number */
	int p = 0;
	if (sscanf(text, "%d", &p) == 1 && p >= 1 &&
	    p <= g_state.period_label_count)
		return p;
	return -1;
}

static void parse_penalty_files(const char *numbers_text,
				const char *times_text, bool home)
{
	/* Save phase2_tenths before clearing — text files cannot carry
	   compound phase info, so we preserve it for matching penalties */
	struct scoreboard_penalty *penalties =
		home ? g_state.home_penalties : g_state.away_penalties;
	int saved_phase2[SCOREBOARD_PENALTY_SLOTS];
	int saved_player[SCOREBOARD_PENALTY_SLOTS];
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		saved_phase2[i] = penalties[i].phase2_tenths;
		saved_player[i] = penalties[i].player_number;
	}

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
				int slot;
				if (home)
					slot = scoreboard_home_penalty_add(
						player, duration_secs);
				else
					slot = scoreboard_away_penalty_add(
						player, duration_secs);
				/* Restore phase2_tenths if the same player
				   had compound data before the re-parse */
				if (slot >= 0) {
					for (int j = 0;
					     j < SCOREBOARD_PENALTY_SLOTS;
					     j++) {
						if (saved_player[j] ==
							    player &&
						    saved_phase2[j] > 0) {
							penalties[slot]
								.phase2_tenths =
								saved_phase2[j];
							saved_phase2[j] = 0;
							break;
						}
					}
				}
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
	g_dirty = false;
	g_event_count = 0;
	memset(g_event_log, 0, sizeof(g_event_log));
	g_state.period = 1;
	g_state.period_length = SCOREBOARD_DEFAULT_PERIOD_LENGTH;
	g_state.clock_direction = SCOREBOARD_CLOCK_COUNT_DOWN;
	g_state.clock_tenths = SCOREBOARD_DEFAULT_PERIOD_LENGTH * 10;
	g_state.overtime_enabled = true;
	g_state.default_penalty_duration = SCOREBOARD_DEFAULT_PENALTY_DURATION;
	g_state.default_major_penalty_duration =
		SCOREBOARD_DEFAULT_MAJOR_PENALTY_DURATION;
	safe_copy(g_state.home_name, "Home", sizeof(g_state.home_name));
	safe_copy(g_state.away_name, "Away", sizeof(g_state.away_name));

	/* Hockey defaults for sport fields */
	g_state.sport = SCOREBOARD_SPORT_HOCKEY;
	safe_copy(g_state.segment_name, "Period",
		  sizeof(g_state.segment_name));
	g_state.segment_count = 3;
	g_state.ot_max = 4;
	g_state.has_shots = true;
	g_state.has_faceoffs = true;
	g_state.has_penalties = true;
	g_state.has_fouls = false;
	g_state.foul_label[0] = '\0';
	g_state.foul_label2[0] = '\0';
	g_state.log_scores = true;
	safe_copy(g_state.score_label, "Goal", sizeof(g_state.score_label));
	generate_default_period_labels();
}

/* ---- clock ---- */

void scoreboard_clock_start(void)
{
	g_state.clock_running = true;
	mark_dirty();
}

void scoreboard_clock_stop(void)
{
	g_state.clock_running = false;
	mark_dirty();
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
	mark_dirty();
}

void scoreboard_clock_tick(int elapsed_tenths)
{
	if (!g_state.clock_running)
		return;

	if (g_state.clock_direction == SCOREBOARD_CLOCK_COUNT_DOWN) {
		g_state.clock_tenths -= elapsed_tenths;
		if (g_state.clock_tenths <= 0) {
			g_state.clock_tenths = 0;
			g_state.clock_running = false;
		}
	} else {
		g_state.clock_tenths += elapsed_tenths;
		int max_tenths = g_state.period_length * 10;
		if (g_state.clock_tenths >= max_tenths) {
			g_state.clock_tenths = max_tenths;
			g_state.clock_running = false;
		}
	}

	mark_dirty();
	if (g_state.clock_running)
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
	mark_dirty();
}

void scoreboard_clock_adjust_seconds(int delta)
{
	int before = g_state.clock_tenths;
	g_state.clock_tenths += delta * 10;
	if (g_state.clock_tenths < 0)
		g_state.clock_tenths = 0;
	mark_dirty();
	int actual_delta = g_state.clock_tenths - before;
	if (actual_delta != 0)
		scoreboard_penalty_adjust(actual_delta);
}

void scoreboard_clock_adjust_minutes(int delta)
{
	int before = g_state.clock_tenths;
	g_state.clock_tenths += delta * 600;
	if (g_state.clock_tenths < 0)
		g_state.clock_tenths = 0;
	mark_dirty();
	int actual_delta = g_state.clock_tenths - before;
	if (actual_delta != 0)
		scoreboard_penalty_adjust(actual_delta);
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
	mark_dirty();
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
	mark_dirty();
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
	if (period > g_state.period_label_count)
		period = g_state.period_label_count;
	g_state.period = period;
	mark_dirty();
}

void scoreboard_period_advance(void)
{
	if (g_state.period < g_state.period_label_count) {
		g_state.period++;
		scoreboard_clock_reset();
		mark_dirty();
	}
}

void scoreboard_period_rewind(void)
{
	if (g_state.period > 1) {
		g_state.period--;
		scoreboard_clock_reset();
		mark_dirty();
	}
}

void scoreboard_format_period(char *buf, size_t size)
{
	if (buf == NULL || size == 0)
		return;
	int idx = g_state.period - 1;
	if (idx >= 0 && idx < g_state.period_label_count) {
		snprintf(buf, size, "%s", g_state.period_labels[idx]);
		return;
	}
	/* Fallback for periods beyond labels */
	snprintf(buf, size, "%d", g_state.period);
}

void scoreboard_set_overtime_enabled(bool enabled)
{
	g_state.overtime_enabled = enabled;
	generate_default_period_labels();
	if (g_state.period > g_state.period_label_count)
		g_state.period = g_state.period_label_count;
	mark_dirty();
}

bool scoreboard_get_overtime_enabled(void)
{
	return g_state.overtime_enabled;
}

/* ---- period labels ---- */

static void generate_default_period_labels(void)
{
	int count = 0;
	/* Regular segments: "1", "2", "3", ... */
	for (int i = 0; i < g_state.segment_count &&
			count < SCOREBOARD_MAX_PERIOD_LABELS;
	     i++) {
		snprintf(g_state.period_labels[count],
			 SCOREBOARD_PERIOD_LABEL_SIZE, "%d", i + 1);
		count++;
	}
	/* Overtime labels: "OT", "OT2", "OT3", ... */
	if (g_state.overtime_enabled) {
		for (int i = 0; i < g_state.ot_max &&
				count < SCOREBOARD_MAX_PERIOD_LABELS;
		     i++) {
			if (i == 0)
				snprintf(g_state.period_labels[count],
					 SCOREBOARD_PERIOD_LABEL_SIZE, "OT");
			else
				snprintf(g_state.period_labels[count],
					 SCOREBOARD_PERIOD_LABEL_SIZE, "OT%d",
					 i + 1);
			count++;
		}
	}
	g_state.period_label_count = count;
}

void scoreboard_set_period_labels(const char *labels)
{
	if (labels == NULL)
		return;

	int count = 0;
	const char *p = labels;
	while (*p != '\0' && count < SCOREBOARD_MAX_PERIOD_LABELS) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		/* Skip empty lines */
		if (len > 0) {
			if (len >= SCOREBOARD_PERIOD_LABEL_SIZE)
				len = SCOREBOARD_PERIOD_LABEL_SIZE - 1;
			memcpy(g_state.period_labels[count], p, len);
			g_state.period_labels[count][len] = '\0';
			count++;
		}
		p = nl ? nl + 1 : p + len;
	}
	if (count > 0) {
		g_state.period_label_count = count;
		/* Clamp current period to new label count */
		if (g_state.period > count)
			g_state.period = count;
		mark_dirty();
	}
}

void scoreboard_get_period_labels(char *buf, size_t size)
{
	if (buf == NULL || size == 0)
		return;
	buf[0] = '\0';
	size_t offset = 0;
	for (int i = 0; i < g_state.period_label_count; i++) {
		size_t label_len = strlen(g_state.period_labels[i]);
		/* Need room for label + newline + null */
		if (offset + label_len + 1 >= size)
			break;
		memcpy(buf + offset, g_state.period_labels[i], label_len);
		offset += label_len;
		buf[offset++] = '\n';
	}
	buf[offset] = '\0';
}

int scoreboard_get_period_label_count(void)
{
	return g_state.period_label_count;
}

const char *scoreboard_get_period_label(int index)
{
	if (index < 0 || index >= g_state.period_label_count)
		return "";
	return g_state.period_labels[index];
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

void scoreboard_set_default_major_penalty_duration(int seconds)
{
	if (seconds < 1)
		seconds = 1;
	g_state.default_major_penalty_duration = seconds;
}

int scoreboard_get_default_major_penalty_duration(void)
{
	return g_state.default_major_penalty_duration;
}

/* ---- team names ---- */

void scoreboard_set_home_name(const char *name)
{
	safe_copy(g_state.home_name, name, sizeof(g_state.home_name));
	mark_dirty();
}

const char *scoreboard_get_home_name(void)
{
	return g_state.home_name;
}

void scoreboard_set_away_name(const char *name)
{
	safe_copy(g_state.away_name, name, sizeof(g_state.away_name));
	mark_dirty();
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
	mark_dirty();
}

void scoreboard_increment_home_score(void)
{
	g_state.home_score++;
	mark_dirty();
}

void scoreboard_decrement_home_score(void)
{
	if (g_state.home_score > 0)
		g_state.home_score--;
	mark_dirty();
}

int scoreboard_get_away_score(void)
{
	return g_state.away_score;
}

void scoreboard_set_away_score(int score)
{
	g_state.away_score = score < 0 ? 0 : score;
	mark_dirty();
}

void scoreboard_increment_away_score(void)
{
	g_state.away_score++;
	mark_dirty();
}

void scoreboard_decrement_away_score(void)
{
	if (g_state.away_score > 0)
		g_state.away_score--;
	mark_dirty();
}

/* ---- shots ---- */

int scoreboard_get_home_shots(void)
{
	return g_state.home_shots;
}

void scoreboard_set_home_shots(int shots)
{
	g_state.home_shots = shots < 0 ? 0 : shots;
	mark_dirty();
}

void scoreboard_increment_home_shots(void)
{
	g_state.home_shots++;
	mark_dirty();
}

void scoreboard_decrement_home_shots(void)
{
	if (g_state.home_shots > 0)
		g_state.home_shots--;
	mark_dirty();
}

int scoreboard_get_away_shots(void)
{
	return g_state.away_shots;
}

void scoreboard_set_away_shots(int shots)
{
	g_state.away_shots = shots < 0 ? 0 : shots;
	mark_dirty();
}

void scoreboard_increment_away_shots(void)
{
	g_state.away_shots++;
	mark_dirty();
}

void scoreboard_decrement_away_shots(void)
{
	if (g_state.away_shots > 0)
		g_state.away_shots--;
	mark_dirty();
}

/* ---- faceoffs ---- */

int scoreboard_get_home_faceoffs(void)
{
	return g_state.home_faceoffs;
}

void scoreboard_set_home_faceoffs(int faceoffs)
{
	g_state.home_faceoffs = faceoffs < 0 ? 0 : faceoffs;
	mark_dirty();
}

void scoreboard_increment_home_faceoffs(void)
{
	g_state.home_faceoffs++;
	mark_dirty();
}

void scoreboard_decrement_home_faceoffs(void)
{
	if (g_state.home_faceoffs > 0)
		g_state.home_faceoffs--;
	mark_dirty();
}

int scoreboard_get_away_faceoffs(void)
{
	return g_state.away_faceoffs;
}

void scoreboard_set_away_faceoffs(int faceoffs)
{
	g_state.away_faceoffs = faceoffs < 0 ? 0 : faceoffs;
	mark_dirty();
}

void scoreboard_increment_away_faceoffs(void)
{
	g_state.away_faceoffs++;
	mark_dirty();
}

void scoreboard_decrement_away_faceoffs(void)
{
	if (g_state.away_faceoffs > 0)
		g_state.away_faceoffs--;
	mark_dirty();
}

bool scoreboard_get_has_faceoffs(void)
{
	return g_state.has_faceoffs;
}

/* ---- fouls ---- */

int scoreboard_get_home_fouls(void)
{
	return g_state.home_fouls;
}

void scoreboard_set_home_fouls(int fouls)
{
	g_state.home_fouls = fouls < 0 ? 0 : fouls;
	mark_dirty();
}

void scoreboard_increment_home_fouls(void)
{
	g_state.home_fouls++;
	mark_dirty();
}

void scoreboard_decrement_home_fouls(void)
{
	if (g_state.home_fouls > 0)
		g_state.home_fouls--;
	mark_dirty();
}

int scoreboard_get_away_fouls(void)
{
	return g_state.away_fouls;
}

void scoreboard_set_away_fouls(int fouls)
{
	g_state.away_fouls = fouls < 0 ? 0 : fouls;
	mark_dirty();
}

void scoreboard_increment_away_fouls(void)
{
	g_state.away_fouls++;
	mark_dirty();
}

void scoreboard_decrement_away_fouls(void)
{
	if (g_state.away_fouls > 0)
		g_state.away_fouls--;
	mark_dirty();
}

/* ---- fouls2 ---- */

int scoreboard_get_home_fouls2(void)
{
	return g_state.home_fouls2;
}

void scoreboard_set_home_fouls2(int fouls)
{
	g_state.home_fouls2 = fouls < 0 ? 0 : fouls;
	mark_dirty();
}

void scoreboard_increment_home_fouls2(void)
{
	g_state.home_fouls2++;
	mark_dirty();
}

void scoreboard_decrement_home_fouls2(void)
{
	if (g_state.home_fouls2 > 0)
		g_state.home_fouls2--;
	mark_dirty();
}

int scoreboard_get_away_fouls2(void)
{
	return g_state.away_fouls2;
}

void scoreboard_set_away_fouls2(int fouls)
{
	g_state.away_fouls2 = fouls < 0 ? 0 : fouls;
	mark_dirty();
}

void scoreboard_increment_away_fouls2(void)
{
	g_state.away_fouls2++;
	mark_dirty();
}

void scoreboard_decrement_away_fouls2(void)
{
	if (g_state.away_fouls2 > 0)
		g_state.away_fouls2--;
	mark_dirty();
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
			mark_dirty();
			return i;
		}
	}
	return -1;
}

int scoreboard_home_penalty_add_compound(int player_number, int phase1_secs,
					 int phase2_secs)
{
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (!g_state.home_penalties[i].active) {
			g_state.home_penalties[i].player_number = player_number;
			g_state.home_penalties[i].remaining_tenths =
				phase1_secs * 10;
			g_state.home_penalties[i].phase2_tenths =
				phase2_secs * 10;
			g_state.home_penalties[i].active = true;
			mark_dirty();
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
		g_state.home_penalties[slot].phase2_tenths = 0;
		mark_dirty();
	}
}

void scoreboard_home_penalty_set_time(int slot, int duration_secs)
{
	if (slot < 0 || slot >= SCOREBOARD_PENALTY_SLOTS)
		return;
	if (!g_state.home_penalties[slot].active)
		return;
	if (duration_secs <= 0) {
		if (g_state.home_penalties[slot].phase2_tenths > 0) {
			g_state.home_penalties[slot].remaining_tenths =
				g_state.home_penalties[slot].phase2_tenths;
			g_state.home_penalties[slot].phase2_tenths = 0;
		} else {
			scoreboard_home_penalty_clear(slot);
			scoreboard_penalty_compact();
			return;
		}
	} else {
		g_state.home_penalties[slot].remaining_tenths =
			duration_secs * 10;
	}
	mark_dirty();
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
			mark_dirty();
			return i;
		}
	}
	return -1;
}

int scoreboard_away_penalty_add_compound(int player_number, int phase1_secs,
					 int phase2_secs)
{
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (!g_state.away_penalties[i].active) {
			g_state.away_penalties[i].player_number = player_number;
			g_state.away_penalties[i].remaining_tenths =
				phase1_secs * 10;
			g_state.away_penalties[i].phase2_tenths =
				phase2_secs * 10;
			g_state.away_penalties[i].active = true;
			mark_dirty();
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
		g_state.away_penalties[slot].phase2_tenths = 0;
		mark_dirty();
	}
}

void scoreboard_away_penalty_set_time(int slot, int duration_secs)
{
	if (slot < 0 || slot >= SCOREBOARD_PENALTY_SLOTS)
		return;
	if (!g_state.away_penalties[slot].active)
		return;
	if (duration_secs <= 0) {
		if (g_state.away_penalties[slot].phase2_tenths > 0) {
			g_state.away_penalties[slot].remaining_tenths =
				g_state.away_penalties[slot].phase2_tenths;
			g_state.away_penalties[slot].phase2_tenths = 0;
		} else {
			scoreboard_away_penalty_clear(slot);
			scoreboard_penalty_compact();
			return;
		}
	} else {
		g_state.away_penalties[slot].remaining_tenths =
			duration_secs * 10;
	}
	mark_dirty();
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
	bool ticked = false;
	bool cleared = false;
	int home_running = 0;
	int away_running = 0;
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (g_state.home_penalties[i].active &&
		    home_running < SCOREBOARD_MAX_RUNNING_PENALTIES) {
			g_state.home_penalties[i].remaining_tenths -=
				elapsed_tenths;
			home_running++;
			ticked = true;
			if (g_state.home_penalties[i].remaining_tenths <= 0) {
				if (g_state.home_penalties[i].phase2_tenths >
				    0) {
					g_state.home_penalties[i]
						.remaining_tenths =
						g_state.home_penalties[i]
							.phase2_tenths;
					g_state.home_penalties[i]
						.phase2_tenths = 0;
				} else {
					scoreboard_home_penalty_clear(i);
					cleared = true;
				}
			}
		}
		if (g_state.away_penalties[i].active &&
		    away_running < SCOREBOARD_MAX_RUNNING_PENALTIES) {
			g_state.away_penalties[i].remaining_tenths -=
				elapsed_tenths;
			away_running++;
			ticked = true;
			if (g_state.away_penalties[i].remaining_tenths <= 0) {
				if (g_state.away_penalties[i].phase2_tenths >
				    0) {
					g_state.away_penalties[i]
						.remaining_tenths =
						g_state.away_penalties[i]
							.phase2_tenths;
					g_state.away_penalties[i]
						.phase2_tenths = 0;
				} else {
					scoreboard_away_penalty_clear(i);
					cleared = true;
				}
			}
		}
	}
	if (cleared)
		scoreboard_penalty_compact();
	if (ticked)
		mark_dirty();
}

void scoreboard_penalty_adjust(int delta_tenths)
{
	bool adjusted = false;
	bool cleared = false;
	int home_running = 0;
	int away_running = 0;
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (g_state.home_penalties[i].active &&
		    home_running < SCOREBOARD_MAX_RUNNING_PENALTIES) {
			g_state.home_penalties[i].remaining_tenths +=
				delta_tenths;
			home_running++;
			adjusted = true;
			if (g_state.home_penalties[i].remaining_tenths <= 0) {
				if (g_state.home_penalties[i].phase2_tenths >
				    0) {
					g_state.home_penalties[i]
						.remaining_tenths =
						g_state.home_penalties[i]
							.phase2_tenths;
					g_state.home_penalties[i]
						.phase2_tenths = 0;
				} else {
					scoreboard_home_penalty_clear(i);
					cleared = true;
				}
			}
		}
		if (g_state.away_penalties[i].active &&
		    away_running < SCOREBOARD_MAX_RUNNING_PENALTIES) {
			g_state.away_penalties[i].remaining_tenths +=
				delta_tenths;
			away_running++;
			adjusted = true;
			if (g_state.away_penalties[i].remaining_tenths <= 0) {
				if (g_state.away_penalties[i].phase2_tenths >
				    0) {
					g_state.away_penalties[i]
						.remaining_tenths =
						g_state.away_penalties[i]
							.phase2_tenths;
					g_state.away_penalties[i]
						.phase2_tenths = 0;
				} else {
					scoreboard_away_penalty_clear(i);
					cleared = true;
				}
			}
		}
	}
	if (cleared)
		scoreboard_penalty_compact();
	if (adjusted)
		mark_dirty();
}

static void compact_penalties(struct scoreboard_penalty *penalties)
{
	int w = 0;
	for (int r = 0; r < SCOREBOARD_PENALTY_SLOTS; r++) {
		if (penalties[r].active) {
			if (w != r)
				penalties[w] = penalties[r];
			w++;
		}
	}
	for (int i = w; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		penalties[i].active = false;
		penalties[i].player_number = 0;
		penalties[i].remaining_tenths = 0;
		penalties[i].phase2_tenths = 0;
	}
}

void scoreboard_penalty_compact(void)
{
	compact_penalties(g_state.home_penalties);
	compact_penalties(g_state.away_penalties);
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
	int running = 0;
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (!penalties[i].active)
			continue;
		if (running >= SCOREBOARD_MAX_RUNNING_PENALTIES)
			break;
		running++;
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
	int running = 0;
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		if (!penalties[i].active)
			continue;
		if (running >= SCOREBOARD_MAX_RUNNING_PENALTIES)
			break;
		running++;
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
	if (!g_dirty)
		return true;

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

	snprintf(buf, sizeof(buf), "%d", g_state.home_faceoffs);
	ok = write_text_file(dir, "home_faceoffs.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.away_faceoffs);
	ok = write_text_file(dir, "away_faceoffs.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.home_fouls);
	ok = write_text_file(dir, "home_fouls.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.away_fouls);
	ok = write_text_file(dir, "away_fouls.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.home_fouls2);
	ok = write_text_file(dir, "home_fouls2.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d", g_state.away_fouls2);
	ok = write_text_file(dir, "away_fouls2.txt", buf) && ok;

	char pen_buf[512];

	scoreboard_format_all_penalty_numbers(true, pen_buf, sizeof(pen_buf));
	ok = write_text_file(dir, "home_penalty_numbers.txt", pen_buf) && ok;

	scoreboard_format_all_penalty_times(true, pen_buf, sizeof(pen_buf));
	ok = write_text_file(dir, "home_penalty_times.txt", pen_buf) && ok;

	scoreboard_format_all_penalty_numbers(false, pen_buf, sizeof(pen_buf));
	ok = write_text_file(dir, "away_penalty_numbers.txt", pen_buf) && ok;

	scoreboard_format_all_penalty_times(false, pen_buf, sizeof(pen_buf));
	ok = write_text_file(dir, "away_penalty_times.txt", pen_buf) && ok;

	ok = write_text_file(dir, "sport.txt",
			     scoreboard_sport_name(g_state.sport)) &&
	     ok;

	snprintf(buf, sizeof(buf), "%d",
		 g_state.default_penalty_duration);
	ok = write_text_file(dir, "default_penalty_duration.txt", buf) && ok;

	snprintf(buf, sizeof(buf), "%d",
		 g_state.default_major_penalty_duration);
	ok = write_text_file(dir, "default_major_penalty_duration.txt", buf) &&
	     ok;

	{
		char labels_buf[512];
		scoreboard_get_period_labels(labels_buf, sizeof(labels_buf));
		ok = write_text_file(dir, "period_labels.txt", labels_buf) &&
		     ok;
	}

	g_dirty = false;
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

	/* Faceoff files are optional — missing doesn't fail the read */
	if (read_text_file(dir, "home_faceoffs.txt", buf, sizeof(buf)))
		scoreboard_set_home_faceoffs(atoi(buf));
	if (read_text_file(dir, "away_faceoffs.txt", buf, sizeof(buf)))
		scoreboard_set_away_faceoffs(atoi(buf));

	/* Fouls files are optional — missing doesn't fail the read */
	if (read_text_file(dir, "home_fouls.txt", buf, sizeof(buf)))
		scoreboard_set_home_fouls(atoi(buf));
	if (read_text_file(dir, "away_fouls.txt", buf, sizeof(buf)))
		scoreboard_set_away_fouls(atoi(buf));
	if (read_text_file(dir, "home_fouls2.txt", buf, sizeof(buf)))
		scoreboard_set_home_fouls2(atoi(buf));
	if (read_text_file(dir, "away_fouls2.txt", buf, sizeof(buf)))
		scoreboard_set_away_fouls2(atoi(buf));

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

	/* Sport file is optional — missing doesn't fail the read */
	if (read_text_file(dir, "sport.txt", buf, sizeof(buf))) {
		enum scoreboard_sport s = scoreboard_sport_from_name(buf);
		if (s != g_state.sport)
			scoreboard_set_sport(s);
	}

	/* Penalty duration files are optional */
	if (read_text_file(dir, "default_penalty_duration.txt", buf,
			   sizeof(buf))) {
		int val = atoi(buf);
		if (val > 0)
			g_state.default_penalty_duration = val;
	}
	if (read_text_file(dir, "default_major_penalty_duration.txt", buf,
			   sizeof(buf))) {
		int val = atoi(buf);
		if (val > 0)
			g_state.default_major_penalty_duration = val;
	}

	/* Period labels file is optional — overrides sport defaults */
	if (read_text_file(dir, "period_labels.txt", buf, sizeof(buf)))
		scoreboard_set_period_labels(buf);

	g_dirty = false;
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
	fprintf(f, "  \"home_faceoffs\": %d,\n", g_state.home_faceoffs);
	fprintf(f, "  \"away_faceoffs\": %d,\n", g_state.away_faceoffs);
	fprintf(f, "  \"home_fouls\": %d,\n", g_state.home_fouls);
	fprintf(f, "  \"away_fouls\": %d,\n", g_state.away_fouls);
	fprintf(f, "  \"home_fouls2\": %d,\n", g_state.home_fouls2);
	fprintf(f, "  \"away_fouls2\": %d,\n", g_state.away_fouls2);
	write_json_string(f, "sport", scoreboard_sport_name(g_state.sport),
			  false);

	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		fprintf(f, "  \"home_penalty%d_number\": %d,\n", i,
			g_state.home_penalties[i].player_number);
		fprintf(f, "  \"home_penalty%d_tenths\": %d,\n", i,
			g_state.home_penalties[i].remaining_tenths);
		fprintf(f, "  \"home_penalty%d_active\": %s,\n", i,
			g_state.home_penalties[i].active ? "true" : "false");
		fprintf(f, "  \"home_penalty%d_phase2_tenths\": %d,\n", i,
			g_state.home_penalties[i].phase2_tenths);
	}
	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		fprintf(f, "  \"away_penalty%d_number\": %d,\n", i,
			g_state.away_penalties[i].player_number);
		fprintf(f, "  \"away_penalty%d_tenths\": %d,\n", i,
			g_state.away_penalties[i].remaining_tenths);
		fprintf(f, "  \"away_penalty%d_active\": %s,\n", i,
			g_state.away_penalties[i].active ? "true" : "false");
		fprintf(f, "  \"away_penalty%d_phase2_tenths\": %d,\n", i,
			g_state.away_penalties[i].phase2_tenths);
	}

	fprintf(f, "  \"period_label_count\": %d",
		g_state.period_label_count);
	for (int i = 0; i < g_state.period_label_count; i++) {
		char key[32];
		snprintf(key, sizeof(key), "period_label%d", i);
		fprintf(f, ",\n");
		bool last = (i == g_state.period_label_count - 1);
		write_json_string(f, key, g_state.period_labels[i], last);
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

	/* Load sport first — set_sport() applies preset defaults for
	   direction, period_length, etc., which explicit fields override. */
	{
		char sport_str[32];
		parse_json_string(json, "sport", sport_str,
				  sizeof(sport_str));
		if (sport_str[0] != '\0')
			scoreboard_set_sport(
				scoreboard_sport_from_name(sport_str));
	}

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
	g_state.home_faceoffs =
		parse_json_int(json, "home_faceoffs", g_state.home_faceoffs);
	g_state.away_faceoffs =
		parse_json_int(json, "away_faceoffs", g_state.away_faceoffs);
	g_state.home_fouls =
		parse_json_int(json, "home_fouls", g_state.home_fouls);
	g_state.away_fouls =
		parse_json_int(json, "away_fouls", g_state.away_fouls);
	g_state.home_fouls2 =
		parse_json_int(json, "home_fouls2", g_state.home_fouls2);
	g_state.away_fouls2 =
		parse_json_int(json, "away_fouls2", g_state.away_fouls2);

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
		snprintf(key, sizeof(key), "home_penalty%d_phase2_tenths", i);
		g_state.home_penalties[i].phase2_tenths = parse_json_int(
			json, key, g_state.home_penalties[i].phase2_tenths);

		snprintf(key, sizeof(key), "away_penalty%d_number", i);
		g_state.away_penalties[i].player_number = parse_json_int(
			json, key, g_state.away_penalties[i].player_number);
		snprintf(key, sizeof(key), "away_penalty%d_tenths", i);
		g_state.away_penalties[i].remaining_tenths = parse_json_int(
			json, key, g_state.away_penalties[i].remaining_tenths);
		snprintf(key, sizeof(key), "away_penalty%d_active", i);
		g_state.away_penalties[i].active = parse_json_bool(
			json, key, g_state.away_penalties[i].active);
		snprintf(key, sizeof(key), "away_penalty%d_phase2_tenths", i);
		g_state.away_penalties[i].phase2_tenths = parse_json_int(
			json, key, g_state.away_penalties[i].phase2_tenths);
	}

	{
		int lcount = parse_json_int(json, "period_label_count", -1);
		if (lcount > 0) {
			if (lcount > SCOREBOARD_MAX_PERIOD_LABELS)
				lcount = SCOREBOARD_MAX_PERIOD_LABELS;
			g_state.period_label_count = lcount;
			for (int i = 0; i < lcount; i++) {
				char key[32];
				snprintf(key, sizeof(key), "period_label%d",
					 i);
				parse_json_string(
					json, key, g_state.period_labels[i],
					SCOREBOARD_PERIOD_LABEL_SIZE);
			}
		}
	}

	free(json);
	mark_dirty();
	return true;
}

/* ---- game management ---- */

void scoreboard_new_game(void)
{
	g_state.home_score = 0;
	g_state.away_score = 0;
	g_state.home_shots = 0;
	g_state.away_shots = 0;
	g_state.home_faceoffs = 0;
	g_state.away_faceoffs = 0;
	g_state.home_fouls = 0;
	g_state.away_fouls = 0;
	g_state.home_fouls2 = 0;
	g_state.away_fouls2 = 0;
	g_state.period = 1;
	g_state.clock_running = false;

	for (int i = 0; i < SCOREBOARD_PENALTY_SLOTS; i++) {
		g_state.home_penalties[i].active = false;
		g_state.home_penalties[i].player_number = 0;
		g_state.home_penalties[i].remaining_tenths = 0;
		g_state.home_penalties[i].phase2_tenths = 0;
		g_state.away_penalties[i].active = false;
		g_state.away_penalties[i].player_number = 0;
		g_state.away_penalties[i].remaining_tenths = 0;
		g_state.away_penalties[i].phase2_tenths = 0;
	}

	if (g_state.clock_direction == SCOREBOARD_CLOCK_COUNT_DOWN)
		g_state.clock_tenths = g_state.period_length * 10;
	else
		g_state.clock_tenths = 0;
	mark_dirty();
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

void scoreboard_set_cli_extra_args(const char *args)
{
	safe_copy(g_state.cli_extra_args, args,
		  sizeof(g_state.cli_extra_args));
}

const char *scoreboard_get_cli_extra_args(void)
{
	return g_state.cli_extra_args;
}

/* ---- sport presets ---- */

static const char *k_sport_names[SCOREBOARD_SPORT_COUNT] = {
	"hockey", "basketball", "soccer", "football", "lacrosse", "rugby",
	"generic",
};

void scoreboard_set_sport(enum scoreboard_sport sport)
{
	if (sport < 0 || sport >= SCOREBOARD_SPORT_COUNT)
		sport = SCOREBOARD_SPORT_HOCKEY;
	const struct scoreboard_sport_preset *p = &k_sport_presets[sport];
	g_state.sport = sport;
	safe_copy(g_state.segment_name, p->segment_name,
		  sizeof(g_state.segment_name));
	g_state.segment_count = p->segment_count;
	g_state.ot_max = p->ot_max;
	g_state.has_shots = p->has_shots;
	g_state.has_faceoffs = p->has_faceoffs;
	g_state.has_penalties = p->has_penalties;
	g_state.has_fouls = p->has_fouls;
	safe_copy(g_state.foul_label, p->foul_label,
		  sizeof(g_state.foul_label));
	safe_copy(g_state.foul_label2, p->foul_label2,
		  sizeof(g_state.foul_label2));
	g_state.log_scores = p->log_scores;
	safe_copy(g_state.score_label, p->score_label,
		  sizeof(g_state.score_label));
	if (p->duration_seconds > 0)
		g_state.period_length = p->duration_seconds;
	g_state.clock_direction = p->default_direction;
	if (p->default_penalty_secs > 0)
		g_state.default_penalty_duration = p->default_penalty_secs;
	if (p->default_major_penalty_secs > 0)
		g_state.default_major_penalty_duration =
			p->default_major_penalty_secs;
	generate_default_period_labels();
	mark_dirty();
}

enum scoreboard_sport scoreboard_get_sport(void)
{
	return g_state.sport;
}

const struct scoreboard_sport_preset *scoreboard_get_sport_preset(void)
{
	return &k_sport_presets[g_state.sport];
}

const char *scoreboard_sport_name(enum scoreboard_sport sport)
{
	if (sport < 0 || sport >= SCOREBOARD_SPORT_COUNT)
		return k_sport_names[SCOREBOARD_SPORT_HOCKEY];
	return k_sport_names[sport];
}

enum scoreboard_sport scoreboard_sport_from_name(const char *name)
{
	if (name == NULL)
		return SCOREBOARD_SPORT_HOCKEY;
	for (int i = 0; i < SCOREBOARD_SPORT_COUNT; i++) {
		if (strcmp(name, k_sport_names[i]) == 0)
			return (enum scoreboard_sport)i;
	}
	return SCOREBOARD_SPORT_HOCKEY;
}

const char *scoreboard_get_segment_name(void)
{
	return g_state.segment_name;
}

bool scoreboard_get_has_shots(void)
{
	return g_state.has_shots;
}

bool scoreboard_get_has_penalties(void)
{
	return g_state.has_penalties;
}

bool scoreboard_get_has_fouls(void)
{
	return g_state.has_fouls;
}

const char *scoreboard_get_foul_label(void)
{
	return g_state.foul_label;
}

bool scoreboard_get_has_fouls2(void)
{
	return g_state.foul_label2[0] != '\0';
}

const char *scoreboard_get_foul_label2(void)
{
	return g_state.foul_label2;
}

bool scoreboard_get_log_scores(void)
{
	return g_state.log_scores;
}

const char *scoreboard_get_score_label(void)
{
	return g_state.score_label;
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

/* ---- game event log ---- */

void scoreboard_event_log_clear(void)
{
	g_event_count = 0;
}

int scoreboard_event_log_add(int offset_seconds, const char *label)
{
	if (g_event_count >= SCOREBOARD_MAX_EVENTS)
		return -1;
	if (label == NULL)
		return -1;
	if (offset_seconds < 0)
		offset_seconds = 0;
	int idx = g_event_count;
	g_event_log[idx].offset_seconds = offset_seconds;
	safe_copy(g_event_log[idx].label, label, SCOREBOARD_EVENT_LABEL_SIZE);
	g_event_count++;
	return idx;
}

bool scoreboard_event_log_remove(int index)
{
	if (index < 0 || index >= g_event_count)
		return false;
	for (int i = index; i < g_event_count - 1; i++)
		g_event_log[i] = g_event_log[i + 1];
	g_event_count--;
	return true;
}

int scoreboard_event_log_find_last(const char *prefix)
{
	if (prefix == NULL)
		return -1;
	size_t len = strlen(prefix);
	if (len == 0)
		return -1;
	for (int i = g_event_count - 1; i >= 0; i--) {
		if (strncmp(g_event_log[i].label, prefix, len) == 0)
			return i;
	}
	return -1;
}

int scoreboard_event_log_count(void)
{
	return g_event_count;
}

const struct scoreboard_game_event *scoreboard_event_log_get(int index)
{
	if (index < 0 || index >= g_event_count)
		return NULL;
	return &g_event_log[index];
}

bool scoreboard_event_log_write(const char *path)
{
	if (path == NULL)
		return false;
	FILE *f = fopen(path, "w");
	if (f == NULL)
		return false;

	for (int i = 0; i < g_event_count; i++) {
		int total = g_event_log[i].offset_seconds;
		int hours = total / 3600;
		int minutes = (total % 3600) / 60;
		int seconds = total % 60;
		fprintf(f, "%d:%02d:%02d %s\n", hours, minutes, seconds,
			g_event_log[i].label);
	}

	fclose(f);
	return true;
}

bool scoreboard_event_log_file_has_content(const char *path)
{
	if (path == NULL)
		return false;
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fclose(f);
	return size > 0;
}

int scoreboard_event_log_read(const char *path)
{
	if (path == NULL)
		return -1;
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return -1;

	int loaded = 0;
	char line[256];
	while (fgets(line, sizeof(line), f) != NULL) {
		/* Strip trailing newline */
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		/* Parse "H:MM:SS label" format */
		int hours = 0, minutes = 0, seconds = 0;
		int consumed = 0;
		if (sscanf(line, "%d:%d:%d %n", &hours, &minutes, &seconds,
			   &consumed) < 3 ||
		    consumed == 0) {
			continue; /* skip malformed lines */
		}

		const char *label = line + consumed;
		if (label[0] == '\0')
			continue;

		int offset = hours * 3600 + minutes * 60 + seconds;
		if (scoreboard_event_log_add(offset, label) < 0)
			break; /* capacity reached */
		loaded++;
	}

	fclose(f);
	return loaded;
}
