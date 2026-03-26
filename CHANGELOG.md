# Changelog

All notable changes to Streamn Scoreboard will be documented in this file.

## [Unreleased]

### Added
- Configurable period labels — period display names are now customizable per-game via `period_labels.txt`, with sport-specific defaults (e.g. hockey: 1, 2, 3, OT, OT2, OT3, OT4)
- "Edit..." button in Game Settings to open `period_labels.txt` in default text editor for quick customization
- File watcher on `period_labels.txt` — edits in external editors apply immediately without restarting OBS
- Faceoff wins counter for hockey and lacrosse — tracks home/away faceoff wins with +/- buttons, displayed below shots on goal
- `home_faceoffs.txt` and `away_faceoffs.txt` output files for OBS Text sources
- 4 new OBS hotkeys: Home/Away Faceoff Win +/-
- Tooltips on all stat row buttons and labels (SOG, FO) for discoverability
- Penalty queue system — only 2 penalties per team run simultaneously (matching hockey rules), additional penalties queue and auto-start when earlier ones expire
- `SCOREBOARD_MAX_RUNNING_PENALTIES` constant for configurable penalty concurrency

### Fixed
- Start/Stop button now resets to green "Start" when clock auto-stops at 0:00 — previously stayed red "Stop" until manually clicked
- Penalty timers now freeze when period clock reaches 0:00 — previously kept ticking after clock auto-stop

### Changed
- Period max is now derived from period label count instead of hardcoded segment_count + ot_max
- Toggling overtime on/off regenerates period labels and clamps current period accordingly
- Penalty text file output (`home_penalty_numbers.txt`, etc.) now shows only the 2 running penalties, not queued ones
- Dock UI shows all active penalties but marks queued penalties with "(queued)" suffix

## [0.4.1] - 2026-03-21

### Fixed
- "Copy Timestamps to Clipboard" button now remains visible after OBS restarts or stream stops, as long as `timestamps.txt` exists on disk
- reeln-cli commands (segment highlights, game highlights) now fire regardless of streaming state — previously the button did nothing when not actively streaming

### Changed
- New Game clears `timestamps.txt` and hides the copy button
- Stream start warns if previous game timestamps exist, with option to keep or start fresh

## [0.4.0] - 2026-03-11

### Added
- Game event timestamps for YouTube chapter markers — automatically logs goals, penalties, period changes, and game end events with stream-relative timestamps
- "Copy Timestamps to Clipboard" button for pasting chapter markers into YouTube descriptions
- `timestamps.txt` output file written incrementally after every event
- Event removal support: decrementing a score or clearing a penalty removes the corresponding timestamp entry
- Goal timestamps subtract a 10-second delay offset to better align with the actual moment
- Power Play labels on penalty events showing which team has the advantage
- Recording chapter markers — embeds chapters into Hybrid MP4/MOV recordings via OBS 32+ API when enabled (requires Hybrid MP4 output format, not Standard/FFmpeg)
- Companion `.chapters.txt` file written next to any recording (MP4 or MKV) for reeln-cli integration or manual ffmpeg chapter injection
- "Record chapters in game file" checkbox in Game Settings with tooltip showing API availability
- Sport-aware score labels in timestamps: "Goal" for hockey/soccer/lacrosse, "Try" for rugby; score logging disabled by default for basketball and football (too frequent)
- Clock auto-stop when it reaches 0:00 (count-down) or period length (count-up)
- Scrollable dock layout — process queue no longer pushes scoreboard controls out of view

### Changed
- Segment CLI command (`reeln game segment`) only runs when OBS is actively streaming — no more spurious failures during offline testing
- Process queue section now uses remaining dock space and scrolls internally instead of growing unbounded

## [0.3.1] - 2026-03-03

### Changed
- Renamed "Streamn CLI" to "reeln-cli" in Game Settings dialog with clickable link to repository
- Simplified CLI integration: period advance fires `reeln game segment {period}` automatically
- Removed main config and override config fields (replaced by hardcoded reeln-cli commands)
- Removed auto-highlights checkbox (segment command fires automatically on period advance)
- Goal scoring and new game no longer fire CLI commands
- Highlights button and period advance are disabled while clock is running
- Highlights button text is now sport-aware (e.g. "Generate Quarter Highlights" for basketball)

### Added
- "CLI arguments" text field in Game Settings for extra args (e.g. `--profile my-profile`)
- Mid-period confirmation dialog when generating highlights or advancing period with clock stopped mid-segment
- "Game Finished" checkbox next to highlights button — switches to `reeln game highlights` command

## [0.3.0] - 2026-03-03

### Added
- Multi-sport support: hockey, basketball, soccer, football, lacrosse, rugby, and generic sport presets
- Sport selector dropdown in Game Settings dialog
- Sport-specific segment naming in dock UI (Period, Quarter, Half, Segment)
- `sport.txt` output file for CLI integration
- Sport field in JSON state persistence
- File watcher monitors `sport.txt` for external sport changes
- Foul/card/flag counter for basketball ("Fouls"), soccer ("YC"/"RC"), and football ("Flags") with +/- buttons in dock UI
- Soccer split yellow card (YC) and red card (RC) into separate counter rows
- `home_fouls.txt`, `away_fouls.txt`, `home_fouls2.txt`, `away_fouls2.txt` output files for foul counters
- Fouls and fouls2 persisted in JSON state and text file round-trips
- 8 new OBS hotkeys for foul counters: Home/Away Foul +/-, Home/Away Foul2 +/-
- Rugby preset: 2 halves, 40-minute periods, count-up clock, sin-bin penalties
- 31 new sport preset and foul tests (`test-scoreboard-core-sport.c`)

### Changed
- **Soccer: "Cards" counter split into "YC" (yellow cards) and "RC" (red cards).** `home_fouls.txt`/`away_fouls.txt` now represent yellow cards only. Add OBS Text sources for the new `home_fouls2.txt`/`away_fouls2.txt` files to display red cards.
- Period/overtime logic now driven by sport preset (segment count + OT max) instead of hardcoded hockey values
- Shots row automatically hides for sports without shots (basketball, soccer, football, generic)
- Penalty section automatically hides for sports without penalties (basketball, soccer, football, generic)
- Game Settings dialog label changed from "Period length" to "Segment length" to align with reeln-cli vernacular
- Sport dropdown in Game Settings live-previews default duration, direction, and penalty visibility
- Lacrosse preset includes penalty support (same timed model as hockey)
- Fouls row automatically shows for sports with fouls (basketball, soccer, football) with sport-specific label
- Second foul row (fouls2) automatically shows for soccer (red cards)

## [0.2.3] - 2026-03-02

### Fixed
- Fixed Windows plugin failing to load — release was built against Qt 5 but OBS Studio 30 ships Qt 6
- Fixed Windows and Linux release packages using incorrect directory structure for OBS plugin loading
- Fixed CLI events firing when no CLI executable is configured (no longer spawns empty processes)

### Added
- OBS 32+ Plugin Manager metadata (`manifest.json`) now included in all platform packages
- `images/text_files.png` screenshot for documentation

### Changed
- Updated Windows and Linux installation instructions in README to match corrected package structure

## [0.2.2] - 2026-02-25

### Fixed
- Fixed macOS `.pkg` installer not being found by OBS — now installs as a proper `.plugin` bundle to the user-level plugins directory (`~/Library/Application Support/obs-studio/plugins/`)
- Updated README macOS install instructions with Gatekeeper workaround (right-click > Open)

## [0.2.1] - 2026-02-25

### Fixed
- Fixed OBS hotkey bindings not persisting across restarts (added explicit save/load via `obs_frontend_add_save_callback`)

### Changed
- Improved README installation instructions with step-by-step guides, post-install dock setup, and link to OBS Plugins Guide
- Added dock UI and scorebug screenshots to README

## [0.2.0] - 2026-02-24

### Added
- Main config, override config, and environment file fields in Game Settings dialog with Browse buttons
- Visible process queue in dock (auto-hidden when no jobs, appears on CLI events)
- Right-click context menu on process queue to clear completed jobs
- `clear_completed_jobs()` function for queue management
- Cross-platform build support (macOS, Windows, Linux)
- GitHub Actions CI: build and test on all 3 platforms with coverage enforcement
- GitHub Actions release workflow: platform packaging with optional code signing
- About dialog accessible from dock menu (shows version, license, repo link)
- Windows and Linux CMake presets (`windows`, `linux`)
- Qt6 build support with `USE_OBS_QT_FRAMEWORKS` CMake option for macOS local development

### Changed
- CMakeLists.txt refactored for cross-platform Qt5/Qt6/OBS/SIMDE discovery
- macOS build now links against OBS.app's bundled Qt6 frameworks when OBS.app is present, avoiding dual-Qt-runtime crash
- `configure.sh` auto-detects OBS.app and skips Homebrew Qt prefix when present (CI still uses qt@5)
- `QAction` include is now version-conditional (`QtGui` for Qt6, `QtWidgets` for Qt5)
- `merge_path_value()` now uses platform-correct PATH separator (`;` on Windows, `:` elsewhere)
- Compiler flags platform-gated: MSVC uses `/W4`, GCC/Clang uses `-Wall -Wextra -Wpedantic`
- Game Settings save now reloads CLI config (`load_cli_config()`) after updating paths

### Fixed
- Fixed shutdown double-free of `g_queue_container` (now owned by QScrollArea widget tree)

## [0.1.0] - 2026-02-23

### Added
- Initial release of Streamn Scoreboard OBS plugin
- Game clock with configurable period length (count up / count down)
- Period tracking: 3 standard periods + OT through OT4
- Home/away score and shots on goal with +/- buttons
- Editable team names directly in the dock UI
- Penalty tracking: up to 8 penalties per team with player number and countdown timer
- Clock quick-adjust buttons: minutes (left carets) and seconds (right carets, hold-to-repeat)
- Clock adjustments sync penalty timers symmetrically
- Dynamic Start/Stop button with red highlight when running
- 22 OBS hotkeys for hands-free operation
- Text file output: 12 files (clock, period, names, scores, shots, penalties) for OBS Text sources
- Refresh State: reload all values from text files (menu action)
- State persistence: reads text files on startup to restore game state across OBS restarts
- Save/Load State to JSON files
- New Game reset (preserves team names and configuration)
- Configure Paths dialog for output directory, CLI executable, and config files
- Game Settings dialog for period length, clock direction, and default penalty duration
- CLI process queue with event-driven actions and token expansion
- Theme-compliant UI using QPalette (adapts to OBS light/dark themes)

### Compatibility
- Requires OBS Studio 30.0 or later
- macOS (Apple Silicon and Intel)
