# Personal Ideas Backlog

## Rank 1: Best ROI (Do First)
- Accurate session lifecycle API in SC64 settings:
  - `active`, `rom_id`, `duration_sec`, `last_end_epoch`, `end_reason`.
- Stable game identity beyond path matching (ROM-derived ID) so stats survive renames/moves.
- Smart random v2 using telemetry (`neglected`, `short-session`, `favorites`, `party`).
- Save confidence panel in ROM details:
  - last writeback time, pending writeback, warning states.
- Weekly/monthly playtime + streaks view.

## Rank 2: Strong UX/Fun Upgrades
- Session timeline view (last N sessions with duration + end reason).
- Quick tags per game (`beaten`, `wishlist`, `drop-in`) + filter shortcuts.
- Playlist scene packs:
  - theme + music + background + visualizer preset + text panel alpha.
- "Now Playing" mini-OSD for menu music.
- Challenge mode + party mode launchers based on curated playlists.
- Emulator experience upgrades:
  - emulator details page should show which core will be used, expected save type, and whether a matching emulator binary exists in `/menu/emulators`.
  - add per-core compatibility / caveat notes (for example header stripping, save quirks, unsupported mappers/chips, likely performance issues).
  - surface emulator ROM metadata where cheap:
    title fallback from filename, file size, save path, last played, last save modified.
  - add emulator-specific curated art / manuals / metadata roots similar to native ROM metadata where practical.
  - support quick launch presets per emulator family (last played, favorites, recently used per core).
  - add missing-system guidance:
    if the required emulator binary is absent, show the exact expected filename and target directory before launch.
  - add save-management shortcuts for emulated ROMs:
    inspect save path, delete/reset save, maybe duplicate save for alternate playthroughs.
  - consider per-core settings profiles:
    region / speed / video mode / known boot flags if supported by the emulator frontend.
  - add archive-awareness if worthwhile:
    let the menu extract or stage common emulator ROM archives into cache before launch.
  - unify emulator file typing/extensions:
    `file_info.c` and browser extension tables are slightly inconsistent (`sfc` missing in one place).
  - longer-term: emulator library browser mode / shelves grouped by system, instead of treating emulator ROMs as generic files.

## Rank 3: Performance/Architecture
- Firmware-assisted I/O prefetch hints for backgrounds/boxart/music.
- Menu decode queue improvements with explicit frame budget and hard audio priority.
- Persistent thumbnail + metadata warm caches with integrity/version checks.
- Background asset format pipeline:
  - preconverted display-native assets where practical.

## Rank 4: Advanced Firmware + Tooling
- Firmware event log ring buffer (reset cause, exceptions, save events, diagnostics).
- Expose diagnostics in menu:
  - voltage/temp trends and stability indicators.
- USB telemetry/debug stream while game is running for profiling.
- Crash breadcrumbs persisted by firmware and shown by menu on next boot.
- Metadata source stitching pipeline:
  - keep stable external IDs (e.g. TheGamesDB) in `metadata.ini` for safer future refreshes.
  - use those IDs to enrich release dates, publishers, player counts, and later manual/archive links.
  - prefer conservative matching + audit reports over aggressive one-shot mass rewrites.

## Rank 4.5: Manual Library / Scans
- Manual scan viewer tied to ROM metadata directories:
  - ingest `EPUB` / `PDF` / `CBZ` offline, but ship a menu-native `manual/` package.
  - prefer full image scans over OCR/reflow for artifact fidelity.
  - phase 1: page-turning only; phase 2: thumbnails; phase 3: optional zoom tiles.
  - design notes: `docs/71_manual_scan_viewer_plan.md`.

## Libdragon Preview Branch Ideas
- Controller Pak manager upgrades:
  - expose `cpakfs_fsck()` as a first-class `Verify / Repair` flow in the pak manager.
  - add recovery-oriented UX: detect corruption, explain likely outcomes, offer read-only inspect before repair.
  - surface multi-bank pak support explicitly in UI for Datel-style large paks.
  - add bank-aware backup / restore / clone tools instead of treating every pak as a simple single-image case.
  - consider `Recover notes` and `Export recovered notes to SD` actions when fsck can salvage partial data.
- Typography / localization:
  - expand `mkfont` usage beyond a single override font into curated language/font packs.
  - support larger glyph subsets, icon fonts, stronger outlines, and better regional coverage without recompiling.
  - consider bitmap/BMFont-based decorative theme fonts for headings while keeping body text practical.
- Audio:
  - move more menu music flows toward preview-era WAV64/XM64 tooling where stability or memory use benefits.
  - evaluate XM64-based lightweight menu themes / playlist scene packs loaded from SD.
  - revisit WAV64 handling with newer APIs where practical, especially around safer playback setup and future visualizer metering.
- Video / attract mode:
  - preview branch MPEG-1 support could enable title-card loops, playlist intro videos, or per-game attract clips.
  - lower priority than Controller Pak work, but high upside for showcase / kiosk-style presentation.
- Frame pacing / animation polish:
  - evaluate `display_get_delta_time()` and newer FPS reporting to smooth screensavers, visualizers, and motion-heavy UI.
- 3D / rendering experiments:
  - OpenGL support is interesting for a 3D shelf / carousel / showcase mode, but not a near-term ROI feature for core menu UX.

## Rank 5: Stretch / Experimental
- Controller hotkey bridge for return-to-menu (only if technically safe per game context).
- Runtime heartbeat hook from game to firmware (idle vs active precision).
- Local companion tool (USB) for playlist/metadata/session analytics editing.
- Live ROM patching system:
  - Download/manage patch packs (e.g., GoldenEye texture/quality mods) and apply at launch time.
  - Prefer non-destructive layered patching (base ROM kept untouched), with patch profile selection per game.
  - Feasibility note: viable for IPS/BPS/APS at launch in menu flow; true in-game/on-the-fly patching is much harder and likely not safe.

## Active Issues To Fix Alongside New Features
- Keep non-user-facing sidecar files hidden in browser (e.g. `*.datel.txt`).
- Ensure empty directories/playlists always allow immediate escape (`A/B` up one level).
