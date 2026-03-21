#ifndef SCOREBOARD_CORE_H
#define SCOREBOARD_CORE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum scoreboard_log_level {
	SCOREBOARD_LOG_INFO = 0,
	SCOREBOARD_LOG_WARNING,
	SCOREBOARD_LOG_ERROR
};

typedef void (*scoreboard_log_fn)(enum scoreboard_log_level level,
				  const char *message);

enum scoreboard_clock_direction {
	SCOREBOARD_CLOCK_COUNT_DOWN = 0,
	SCOREBOARD_CLOCK_COUNT_UP
};

enum scoreboard_sport {
	SCOREBOARD_SPORT_HOCKEY = 0,
	SCOREBOARD_SPORT_BASKETBALL,
	SCOREBOARD_SPORT_SOCCER,
	SCOREBOARD_SPORT_FOOTBALL,
	SCOREBOARD_SPORT_LACROSSE,
	SCOREBOARD_SPORT_RUGBY,
	SCOREBOARD_SPORT_GENERIC,
	SCOREBOARD_SPORT_COUNT
};

struct scoreboard_sport_preset {
	enum scoreboard_sport sport;
	char segment_name[16];
	int segment_count;
	int duration_seconds;
	int ot_max;
	bool has_shots;
	bool has_penalties;
	enum scoreboard_clock_direction default_direction;
	bool has_fouls;
	char foul_label[16];
	char foul_label2[16];
	bool log_scores;
	char score_label[16];
};

struct scoreboard_penalty {
	int player_number;
	int remaining_tenths;
	bool active;
};

/* Lifecycle */
const char *scoreboard_description(void);
bool scoreboard_on_load(scoreboard_log_fn log_fn);
void scoreboard_on_unload(scoreboard_log_fn log_fn);
void scoreboard_reset_state_for_tests(void);

/* Clock */
void scoreboard_clock_start(void);
void scoreboard_clock_stop(void);
bool scoreboard_clock_is_running(void);
void scoreboard_clock_reset(void);
void scoreboard_clock_tick(int elapsed_tenths);
int scoreboard_clock_get_tenths(void);
void scoreboard_clock_set_tenths(int tenths);
void scoreboard_clock_adjust_seconds(int delta);
void scoreboard_clock_adjust_minutes(int delta);
void scoreboard_clock_format(char *buf, size_t size);

void scoreboard_set_clock_direction(enum scoreboard_clock_direction dir);
enum scoreboard_clock_direction scoreboard_get_clock_direction(void);
void scoreboard_set_period_length(int seconds);
int scoreboard_get_period_length(void);

/* Period */
int scoreboard_get_period(void);
void scoreboard_set_period(int period);
void scoreboard_period_advance(void);
void scoreboard_period_rewind(void);
void scoreboard_format_period(char *buf, size_t size);
void scoreboard_set_overtime_enabled(bool enabled);
bool scoreboard_get_overtime_enabled(void);

/* Default penalty duration (seconds) */
void scoreboard_set_default_penalty_duration(int seconds);
int scoreboard_get_default_penalty_duration(void);

/* Team names */
void scoreboard_set_home_name(const char *name);
const char *scoreboard_get_home_name(void);
void scoreboard_set_away_name(const char *name);
const char *scoreboard_get_away_name(void);

/* Score */
int scoreboard_get_home_score(void);
void scoreboard_set_home_score(int score);
void scoreboard_increment_home_score(void);
void scoreboard_decrement_home_score(void);
int scoreboard_get_away_score(void);
void scoreboard_set_away_score(int score);
void scoreboard_increment_away_score(void);
void scoreboard_decrement_away_score(void);

/* Shots on goal */
int scoreboard_get_home_shots(void);
void scoreboard_set_home_shots(int shots);
void scoreboard_increment_home_shots(void);
void scoreboard_decrement_home_shots(void);
int scoreboard_get_away_shots(void);
void scoreboard_set_away_shots(int shots);
void scoreboard_increment_away_shots(void);
void scoreboard_decrement_away_shots(void);

/* Fouls / cards / flags (simple per-team counter) */
int scoreboard_get_home_fouls(void);
void scoreboard_set_home_fouls(int fouls);
void scoreboard_increment_home_fouls(void);
void scoreboard_decrement_home_fouls(void);
int scoreboard_get_away_fouls(void);
void scoreboard_set_away_fouls(int fouls);
void scoreboard_increment_away_fouls(void);
void scoreboard_decrement_away_fouls(void);

/* Fouls2 — second foul counter (e.g. soccer red cards) */
int scoreboard_get_home_fouls2(void);
void scoreboard_set_home_fouls2(int fouls);
void scoreboard_increment_home_fouls2(void);
void scoreboard_decrement_home_fouls2(void);
int scoreboard_get_away_fouls2(void);
void scoreboard_set_away_fouls2(int fouls);
void scoreboard_increment_away_fouls2(void);
void scoreboard_decrement_away_fouls2(void);

#define SCOREBOARD_MAX_PENALTIES 8

/* Penalties (up to SCOREBOARD_MAX_PENALTIES per team) */
int scoreboard_home_penalty_add(int player_number, int duration_secs);
void scoreboard_home_penalty_clear(int slot);
const struct scoreboard_penalty *scoreboard_get_home_penalty(int slot);
int scoreboard_get_home_penalty_count(void);
int scoreboard_away_penalty_add(int player_number, int duration_secs);
void scoreboard_away_penalty_clear(int slot);
const struct scoreboard_penalty *scoreboard_get_away_penalty(int slot);
int scoreboard_get_away_penalty_count(void);
void scoreboard_penalty_tick(int elapsed_tenths);
void scoreboard_penalty_adjust(int delta_tenths);
void scoreboard_format_penalty_number(int slot, bool home, char *buf,
				      size_t size);
void scoreboard_format_penalty_time(int slot, bool home, char *buf,
				    size_t size);
void scoreboard_format_all_penalty_numbers(bool home, char *buf, size_t size);
void scoreboard_format_all_penalty_times(bool home, char *buf, size_t size);

/* Dirty flag — true when internal state has changed since last write */
bool scoreboard_is_dirty(void);
void scoreboard_mark_dirty(void);

/* File output */
void scoreboard_set_output_directory(const char *path);
const char *scoreboard_get_output_directory(void);
bool scoreboard_write_all_files(void);
bool scoreboard_read_all_files(void);

/* State persistence */
bool scoreboard_save_state(const char *path);
bool scoreboard_load_state(const char *path);

/* Game management */
void scoreboard_new_game(void);

/* CLI settings */
void scoreboard_set_cli_executable(const char *path);
const char *scoreboard_get_cli_executable(void);
void scoreboard_set_cli_extra_args(const char *args);
const char *scoreboard_get_cli_extra_args(void);

/* Sport presets */
void scoreboard_set_sport(enum scoreboard_sport sport);
enum scoreboard_sport scoreboard_get_sport(void);
const struct scoreboard_sport_preset *scoreboard_get_sport_preset(void);
const char *scoreboard_sport_name(enum scoreboard_sport sport);
enum scoreboard_sport scoreboard_sport_from_name(const char *name);
const char *scoreboard_get_segment_name(void);
bool scoreboard_get_has_shots(void);
bool scoreboard_get_has_penalties(void);
bool scoreboard_get_has_fouls(void);
const char *scoreboard_get_foul_label(void);
bool scoreboard_get_has_fouls2(void);
const char *scoreboard_get_foul_label2(void);
bool scoreboard_get_log_scores(void);
const char *scoreboard_get_score_label(void);

/* Action log */
void scoreboard_add_action_log(const char *message);
size_t scoreboard_copy_action_logs(char *buffer, size_t buffer_size);

/* Game event log — append-only timestamped events for YouTube chapters */
#define SCOREBOARD_MAX_EVENTS 256
#define SCOREBOARD_EVENT_LABEL_SIZE 128

struct scoreboard_game_event {
	int offset_seconds;
	char label[SCOREBOARD_EVENT_LABEL_SIZE];
};

void scoreboard_event_log_clear(void);
int scoreboard_event_log_add(int offset_seconds, const char *label);
bool scoreboard_event_log_remove(int index);
int scoreboard_event_log_find_last(const char *prefix);
int scoreboard_event_log_count(void);
const struct scoreboard_game_event *scoreboard_event_log_get(int index);
bool scoreboard_event_log_write(const char *path);
bool scoreboard_event_log_file_has_content(const char *path);
int scoreboard_event_log_read(const char *path);

#ifdef __cplusplus
}
#endif

#endif
