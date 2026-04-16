# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OBS Studio plugin (C/C++/Qt) that tracks youth hockey scoreboard state and writes it to individual text files. Users compose their scoreboard overlay in OBS using standard Text sources pointed at these files. Provides a dock UI for manual control, OBS hotkeys for hands-free operation, and optional CLI integration for event-driven workflows.

## Build Commands

All commands use `make` wrappers. OBS paths are typically required since `pkg-config` rarely finds libobs:

```bash
# First-time setup
make setup                    # Install brew dependencies
make find-obs-dev-paths       # Discover local OBS header/lib paths

# Common dev cycle (configure + build + test)
make dev OBS_INCLUDE_DIR=... OBS_LIBRARY=... OBS_SOURCE_DIR=...

# Individual steps
make configure PRESET=default OBS_INCLUDE_DIR=... OBS_LIBRARY=...
make build
make test

# Coverage (enforces 100% line coverage on scoreboard-core)
make coverage OBS_INCLUDE_DIR=... OBS_LIBRARY=... OBS_SOURCE_DIR=...

# Install and run in OBS
make install
make run
make check-plugin-log
```

CMake presets: `default` (RelWithDebInfo), `debug` (Debug + -Werror), `ninja`, `coverage` (Debug + gcov, no plugin module).

Run a single test directly: `./build/<test-binary-name>` (e.g. `./build/scoreboard_core_tests`).

## Architecture

Two-layer design separating testable core logic from OBS-dependent code:

**Scoreboard Core** (`src/scoreboard-core.c`, `include/scoreboard-core.h`):
- Pure C static library, no OBS dependencies
- Manages game state: clock (tenths resolution), period, score, shots, faceoffs, penalties, fouls, team names
- Configurable period labels with sport-specific defaults and external file override
- Penalty queue: only `SCOREBOARD_MAX_RUNNING_PENALTIES` (2) tick simultaneously per team; extras auto-start on expiry; slot consolidation via `scoreboard_penalty_compact()` keeps penalties packed to lowest slots
- Compound penalties: `phase2_tenths` field on `scoreboard_penalty` struct enables two-phase penalties (2+2, 2+5, 2+10); phase 2 auto-starts when phase 1 expires or is cleared/edited to 0
- Penalty edit: `scoreboard_*_penalty_set_time()` changes remaining time; setting to 0 on a compound penalty transitions to phase 2 instead of clearing
- Penalty label format: configurable template (`{{ number }}`, `{{ time }}`, `{{ phase2 }}`, `{{ if_phase2 }}...{{ end_if }}`) for combined `home_penalty_labels.txt` / `away_penalty_labels.txt` output files
- Cumulative game clock: opt-in feature tracking total elapsed time across all periods; `game_clock_accumulated_tenths` stores completed period time, `scoreboard_game_clock_get_tenths()` computes live total; configurable display format (MM:SS or H:MM:SS)
- Text file output: writes 27 files (clock, period, scores, shots, faceoffs, fouls, penalties, penalty labels, period labels, sport, penalty durations, period length, cumulative clock) to configurable directory
- JSON state persistence (save/load), action log ring buffer (64 entries)
- All exported functions use `scoreboard_*` prefix
- `scoreboard_reset_state_for_tests()` resets global state between test runs

**OBS Module** (`src/plugin-main.c`):
- Minimal C entry point implementing `obs_module_load`/`obs_module_unload`
- Adapts core log callbacks to OBS logging API
- Initializes the dock UI

**Plugin Metadata** (`data/manifest.json.in`):
- OBS 32+ Plugin Manager metadata (display name, version, URLs)
- Generated at configure time from `data/manifest.json.in` → `build/data/manifest.json`
- Version auto-syncs from `CMakeLists.txt` via `@PROJECT_VERSION@`

**Dock UI** (`src/plugin-dock.cpp`, `include/scoreboard-dock.h`):
- C++17 with Qt5/Qt6 (conditional `QAction` include), scoreboard control interface
- QTimer (100ms) drives clock tick, file writes, and UI updates
- 45 OBS hotkeys for hands-free operation (score, shots, faceoffs, fouls, penalties, penalty edit, compound penalties, clock, period)
- CLI process queue with token expansion (`{event}`, `{home_name}`, `{away_name}`, etc.)
- Game event timestamps: stream-relative (`timestamps.txt`) for YouTube chapter descriptions; uses cumulative game clock time instead of stream time when game clock is enabled
- Recording chapter markers: embeds chapters into Hybrid MP4/MOV via `obs_frontend_recording_add_chapter()` (OBS 32+, resolved at runtime via `dlsym`/`GetProcAddress` for backwards compatibility; only works with Hybrid MP4 output, not Standard/FFmpeg muxer)
- Companion `.chapters.txt` file written next to any recording (all formats) for reeln-cli use
- Sport-aware score labels: hockey/soccer/lacrosse log "Goal", rugby logs "Try", basketball/football disable score logging by default (too frequent)

## Testing

Tests are plain C using `assert()` — no external test framework. Six test binaries exercising scoreboard-core:

- `test-scoreboard-core.c` — clock, period, lifecycle, cumulative game clock
- `test-scoreboard-core-scoring.c` — score, shots, team names, new game
- `test-scoreboard-core-penalties.c` — penalty add/clear/tick/format, slot compaction, edit/set_time, compound penalties (phase transition, clear, adjust, leftover carry), penalty label format/preview
- `test-scoreboard-core-persistence.c` — file output, JSON save/load, action logs, CLI settings, game clock persistence, penalty label file output
- `test-scoreboard-core-sport.c` — sport presets, fouls, score labels
- `test-scoreboard-core-events.c` — event log add/remove/find/write lifecycle

Each test calls `scoreboard_reset_state_for_tests()` for isolation. Tests run via `ctest --preset default` or `make test`.

## Quality Requirements

- **100% line coverage** on `scoreboard-core.c` enforced by `make coverage` / `scripts/coverage.sh`
- Compiler warnings: `-Wall -Wextra -Wpedantic` (errors with `debug` preset)
- Standards: C11 (strict), C++17 (strict), no extensions
- Keep unit-testable logic in scoreboard-core, outside OBS entry points

## Release Process

Releasing a new version requires these steps in order:

1. **Update version** in `CMakeLists.txt` (`project(... VERSION X.Y.Z ...)`) — this automatically updates `plugin-version.h` and `data/manifest.json` via `configure_file()`
2. **Update CHANGELOG.md**: move items from `[Unreleased]` into a new `[X.Y.Z] - YYYY-MM-DD` section
3. **Rebuild and test locally**:
   - `make reconfigure` (picks up new version)
   - `make build && make test`
   - `make coverage` (must pass 100% line coverage on scoreboard-core)
   - `make install` then launch OBS and verify the dock loads, version shows correctly in header and About dialog
4. **Commit** the version bump + changelog update
5. **Tag**: `git tag vX.Y.Z && git push origin vX.Y.Z`
6. The `release.yml` workflow triggers on `v*` tags and builds platform packages (creates a **draft** release)
7. **Publish the release** with user-friendly notes: `gh release edit vX.Y.Z --draft=false --notes "..."`

### Release Notes Format

Release notes should be written for **non-technical users** (streamers, hockey parents), not developers. Use plain language — explain what changed from the user's perspective, not implementation details.

Use these sections as applicable:
- **What's New** — new features and capabilities
- **What's Improved** — enhancements to existing features
- **What's Fixed** — bug fixes described by the symptom the user would have seen
- **What's Changed** — behavioral changes users should be aware of
- **Download** — list each platform's file with a one-line install instruction, and link to the README installation section

Example:
```
## What's Fixed

- **Hotkey bindings now persist across OBS restarts.** Previously, any hotkeys
  you configured in OBS Settings > Hotkeys would be lost every time you closed
  OBS. They now save and restore automatically.

## Download

Pick the file for your platform:
- **macOS:** `streamn-obs-scoreboard-X.Y.Z-macos.pkg` — double-click to install
- **Windows:** `streamn-obs-scoreboard-X.Y.Z-windows-x64.zip` — extract and copy the DLL
- **Linux:** `streamn-obs-scoreboard-X.Y.Z-linux-x86_64.tar.gz` — extract and copy the .so

See the [README](https://github.com/StreamnDad/streamn-scoreboard#installation) for detailed steps.
```

### OBS Forum Update Format

After publishing a GitHub release, post an update to the OBS Forums plugin page. The format is:

**Title:** `[Update] Streamn Scoreboard vX.Y.Z — short summary`

**Body:** Uses the same sections as GitHub release notes (What's Fixed, What's New, What's Improved, What's Changed) but **without** the Download section — the OBS Forums page links to the GitHub release automatically.

Example:
```
[Update] Streamn Scoreboard v0.2.2 — Hotkey persistence fix + macOS installer fix

## What's Fixed

- **macOS installer now works correctly.** The previous releases either placed the
  plugin where OBS couldn't find it, or crashed OBS on startup due to a Qt version
  mismatch. Both issues are resolved — the installer now creates a proper .plugin
  bundle in the right location, built against the correct Qt version.
- **Hotkey bindings now persist across OBS restarts.** Previously, any hotkeys you
  configured in OBS Settings > Hotkeys would be lost every time you closed OBS.
  They now save and restore automatically.

## What's Improved

- **macOS install instructions** now include a note about the Gatekeeper warning —
  right-click the .pkg and choose **Open** to bypass it.
```

## Naming Conventions

- `g_` prefix for globals, `k` prefix for constants
- `scoreboard_*` for exported C API functions
- Event types: `SB_EVENT_HOME_GOAL`, `SB_EVENT_AWAY_GOAL`, etc.
- Struct names: `scoreboard_penalty` for penalty data
