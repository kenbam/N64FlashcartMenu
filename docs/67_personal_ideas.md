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
