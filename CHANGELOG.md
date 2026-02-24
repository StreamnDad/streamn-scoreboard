# Changelog

All notable changes to Streamn Scoreboard will be documented in this file.

## [Unreleased]

### Added
- Cross-platform build support (macOS, Windows, Linux)
- GitHub Actions CI: build and test on all 3 platforms with coverage enforcement
- GitHub Actions release workflow: platform packaging with optional code signing
- About dialog accessible from dock menu (shows version, license, repo link)
- Windows and Linux CMake presets (`windows`, `linux`)

### Changed
- CMakeLists.txt refactored for cross-platform Qt5/OBS/SIMDE discovery
- `merge_path_value()` now uses platform-correct PATH separator (`;` on Windows, `:` elsewhere)
- Compiler flags platform-gated: MSVC uses `/W4`, GCC/Clang uses `-Wall -Wextra -Wpedantic`

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
