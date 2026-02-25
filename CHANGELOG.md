# Changelog

All notable changes to Streamn Scoreboard will be documented in this file.

## [Unreleased]

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
