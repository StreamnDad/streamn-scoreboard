# OBS Game Event Timestamps — Implementation Plan

## Overview
Capture timestamps of game events relative to when OBS goes live, then output
YouTube-compatible chapter markers at game finish time.

## Architecture

### Core Layer (`scoreboard-core.c/.h`)
Add a game event log — a simple append-only list of `(offset_seconds, label)` pairs.
The core has no concept of wall-clock or streaming time; it just stores events
with an integer offset from an arbitrary reference point (set by the plugin).

**New types:**
```c
#define SCOREBOARD_MAX_EVENTS 256
#define SCOREBOARD_EVENT_LABEL_SIZE 128

struct scoreboard_game_event {
    int offset_seconds;          // seconds from reference point (stream start)
    char label[128];             // e.g. "Goal: Eagles (3-1)"
};
```

**New API functions:**
```c
void scoreboard_event_log_clear(void);
int  scoreboard_event_log_add(int offset_seconds, const char *label);
int  scoreboard_event_log_count(void);
const struct scoreboard_game_event *scoreboard_event_log_get(int index);
bool scoreboard_event_log_write(const char *path);  // writes YouTube chapters format
```

**Output format** (`timestamps.txt`):
```
0:00:00 Stream Start
0:12:34 Period 1 Start
0:15:22 Goal: Eagles (1-0)
0:23:45 Penalty: Hawks #12
0:35:10 Period 1 End
0:35:42 Period 2 Start
...
1:45:00 Game End — Eagles 4, Hawks 2
```

### Plugin Layer (`plugin-dock.cpp`)
- Listen for `OBS_FRONTEND_EVENT_STREAMING_STARTED` → store `QElapsedTimer` start
- Listen for `OBS_FRONTEND_EVENT_STREAMING_STOPPED` → clear streaming state
- Compute `offset = elapsed_timer.elapsed() / 1000` for each event
- Auto-log events by hooking into existing UI/hotkey callbacks:
  - Clock start (first start per period) → "{Segment} N Start"
  - Score increment → "Goal: {Team} ({home_score}-{away_score})"
  - Penalty add → "Penalty: {Team} #{player}"
  - Period advance → "{Segment} N End"
  - Game finished → "Game End — {Home} {score}, {Away} {score}"
- Write `timestamps.txt` to output directory when game finishes
- Add "Copy Timestamps" button to copy to clipboard for pasting into YouTube description

### Tests (`test-scoreboard-core-events.c`)
- Test add/get/count/clear lifecycle
- Test write output format (HH:MM:SS labels)
- Test capacity limits (256 events)
- Test edge cases (empty log, zero offset)

## Files Modified
1. `include/scoreboard-core.h` — new types and function declarations
2. `src/scoreboard-core.c` — event log implementation
3. `src/plugin-dock.cpp` — OBS streaming hooks, auto-logging, UI
4. `tests/test-scoreboard-core-events.c` — new test file
5. `CMakeLists.txt` — add new test file
