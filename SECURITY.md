# Security Policy

## Supported Versions

Streamn Scoreboard is pre-1.0 software. Security fixes are published against
the latest minor release only. We recommend always running the most recent
version from the [Releases page](https://github.com/StreamnDad/streamn-scoreboard/releases).

| Version | Supported          |
| ------- | ------------------ |
| 0.6.x   | :white_check_mark: |
| < 0.6   | :x:                |

## Scope

Streamn Scoreboard is an OBS Studio plugin that runs locally on the
streamer's machine. It reads and writes files in a user-configured output
directory, spawns user-configured CLI processes (e.g. `reeln-cli`), and
registers OBS hotkeys. It does **not** listen on any network sockets, collect
telemetry, or handle third-party credentials.

In-scope concerns include, but are not limited to:
- Path traversal or unsafe file handling in the output directory
- Command injection via CLI token expansion (`{event}`, `{home_name}`, etc.)
- Memory safety issues in `scoreboard-core` (C11) or the dock UI (C++17/Qt)
- Unsafe parsing of persisted JSON state or text-file round-trips
- Privilege escalation through the installer packages (`.pkg`, `.zip`, `.tar.gz`)

Out of scope:
- Vulnerabilities in OBS Studio itself — please report those to
  [obsproject/obs-studio](https://github.com/obsproject/obs-studio/security)
- Vulnerabilities in third-party tools invoked by the CLI queue (e.g.
  `reeln-cli`) — report those to the respective project
- Issues that require an attacker to already have local code execution on
  the streamer's machine

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub
issues, discussions, or pull requests.**

Report vulnerabilities using GitHub's private vulnerability reporting:

1. Go to the [Security tab](https://github.com/StreamnDad/streamn-scoreboard/security)
   of this repository
2. Click **"Report a vulnerability"**
3. Fill in as much detail as you can: affected version, reproduction steps,
   impact, and any suggested mitigation

If you cannot use GitHub's reporting, email **git-security@email.remitz.us**
instead.

### What to include

A good report contains:
- The version of Streamn Scoreboard and OBS Studio you tested against
- Your operating system and architecture (macOS / Windows / Linux, arch)
- Steps to reproduce the issue
- What you expected to happen vs. what actually happened
- The potential impact (data loss, code execution, denial of service, etc.)
- Any proof-of-concept code, if applicable

### What to expect

Streamn Scoreboard is maintained by a small team, so all timelines below are
best-effort rather than hard guarantees:

- **Acknowledgement:** typically within a week of your report
- **Initial assessment:** usually within two to three weeks, including
  whether we consider the report in scope and our planned next steps
- **Status updates:** roughly every few weeks until the issue is resolved
- **Fix & disclosure:** coordinated with you. We aim to ship a patch release
  reasonably quickly for high-severity issues, with lower-severity issues
  addressed in a future release. Credit will be given in the release notes
  and CHANGELOG unless you prefer to remain anonymous.

If a report is declined, we will explain why. You are welcome to disagree
and provide additional context.
