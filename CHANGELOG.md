# Changelog

All notable changes to Vigil are documented here.
This project follows [Semantic Versioning](https://semver.org/): every change,
addition or fix bumps the version number.

## [1.2.0] - 2025-11-25

### Added
- **Windows installer** (`Vigil-Setup.exe`, NSIS): branded welcome/header
  graphics, license page, optional Desktop shortcut and "Start with Windows"
  components, per-user install with **no admin required**, a proper uninstaller
  and Add/Remove Programs entry, in English and Hebrew. Downloadable directly
  from `downloads/`.
- Installer source at `installer/Vigil.nsi` (builds with `makensis`).

## [1.1.1] - 2025-11-25

### Fixed (QA pass)
- Timed-session status line no longer underflows if the session expired a moment
  before the timer fires (guarded like the tooltip).
- The tray tooltip's "min left" now refreshes each pulse during a timed session
  instead of showing the start value.
- Mouse-nudge simulation uses `MOUSEEVENTF_MOVE_NOCOALESCE` so the cursor cannot
  slowly drift.
- Startup registry: handle `GetModuleFileName` truncation safely.
- Tidy teardown: release the mutex/COM on the rare window-creation failure, and
  stop timers on `WM_ENDSESSION`.
- Manifest resource declared in the canonical raw form (`1 24`) for toolchain
  robustness.

## [1.1.0] - 2025-11-25

### Added
- **User guide** item in the right-click menu — opens a built-in, bilingual
  (English + Hebrew) HTML guide with an annotated, numbered walkthrough of the
  tray icon and every menu option, plus a "how it works" explanation. The guide
  and its images are embedded in the executable and extracted on demand.

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
