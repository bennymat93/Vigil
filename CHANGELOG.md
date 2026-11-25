# Changelog

All notable changes to Vigil are documented here.
This project follows [Semantic Versioning](https://semver.org/): every change,
addition or fix bumps the version number.

## [1.0.0] - 2025-11-25

First release.

### Added
- Tray application that keeps Windows awake and prevents the screen from locking.
- Two-layer engine: `SetThreadExecutionState` (sleep / display) **plus** a benign
  periodic input pulse that resets the input idle timer, which is what defeats a
  Group-Policy "machine inactivity limit" / secure-screensaver forced lock.
- **Left click** the tray icon toggles keep-awake on/off.
- **Right click** opens the full settings menu:
  - Keep awake on/off, keep the screen on/off.
  - Simulation method: F15 key (default), Scroll Lock tap, or mouse nudge.
  - Pulse interval: 30 / 50 / 59 / 120 seconds.
  - Timed session: 30 min / 1 h / 2 h / 4 h / until turned off (auto-off + notification).
  - Active-on-start default state.
  - Start with Windows (per-user, no admin).
  - Language: English / Hebrew (auto-detected by default).
  - About, Exit.
- Custom 3D application icon with distinct active and paused states.
- Settings persisted to `%APPDATA%\Vigil\settings.ini`.
- Single-instance guard; survives Explorer restarts; re-asserts state after sleep/resume.
- Native Win32, single static executable (~140 KB), no admin rights, no network access.
