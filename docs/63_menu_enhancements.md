[Return to the index](./00_index.md)
## Menu Enhancements (Playlists, UI, Audio, Stats)

This page documents the major menu enhancements in the current customized build, with a focus on playlist-based organization, menu customization, and game tracking.

### M3U Playlist Support
- The File Browser can open `.m3u` playlists containing ROM paths.
- Playlist item order is preserved (the browser defaults to `Custom` sort mode for playlists).
- Supported path formats in playlists:
  - Absolute flashcart paths (for example `sd:/N64 - USA/Mario Kart 64 (USA).z64`)
  - Root-relative paths (for example `/N64 - USA/Mario Kart 64 (USA).z64`)
  - Relative paths (relative to the `.m3u` file location)
- Empty lines and comments are ignored.

### Playlist Comment Directives (Immersion Overrides)
M3U playlists can include `#SC64_...` directives near the top to temporarily override menu appearance while the playlist is open.

Supported directives:
- `#SC64_THEME=<theme name or theme id>`
- `#SC64_BACKGROUND=<path to png>`
- `#SC64_BG=<path to png>` (alias)
- `#SC64_BGM=<path to mp3|wav64>`
- `#SC64_MUSIC=<path to mp3|wav64>` (alias)
- `#SC64_VIZ_STYLE=<Bars|Pulse Wash|Sunburst|Oscilloscope|id>`
- `#SC64_VIZ_INTENSITY=<Subtle|Normal|Full|0..2>`
- `#SC64_TEXT_PANEL=<On|Off>`
- `#SC64_TEXT_PANEL_ALPHA=<0..255>`
- `#SC64_SCREENSAVER_LOGO=<path to png>`
- `#SC64_SMART_ROOT=<path>` or `#SC64_ROOT=<path>`
- `#SC64_FILTER_TITLE=<text>`
- `#SC64_FILTER_PUBLISHER=<text>`
- `#SC64_FILTER_DEVELOPER=<text>`
- `#SC64_FILTER_GENRE=<text>`
- `#SC64_FILTER_SERIES=<text>`
- `#SC64_FILTER_MODES=<text>`
- `#SC64_FILTER_DESCRIPTION=<text>`
- `#SC64_FILTER_YEAR=<1998 | 1998-2000 | >=1998 | <=2000>`
- `#SC64_FILTER_AGE=<3 | 10-17 | >=13>`
- `#SC64_FILTER_PLAYERS=<1 | 1-4 | >=2 | <=4>`
- `#SC64_FILTER_REGION=<E | P | J | USA | PAL | JPN>`
- `#SC64_SMART_SORT=<Title | Year | Publisher | Random>`

Notes:
- Relative directive paths are resolved from the playlist folder.
- A sibling text file named like the playlist with an underscore prefix, for example `_My Playlist.txt` next to `My Playlist.m3u`, appears as the first text entry when the playlist is opened.
- Absolute flashcart paths such as `sd:/menu/music/...` are supported.
- Overrides are runtime-only and are restored automatically when you leave the playlist.
- A short playlist intro toast appears when overrides are applied, summarizing the active profile (theme/BGM/viz/background).
- Smart playlists scan the configured roots recursively, then append matching ROMs after any explicit `.m3u` entries.
- If a smart playlist has filters but no `SMART_ROOT`, it defaults to scanning the flashcart root.
- Matching uses loaded ROM metadata (`name`, `author`, `developer`, `genre`, `series`, `modes`, `players`, `description`, `release_year`, `age_rating`) plus ROM region code.

Example:
```m3u
#SC64_THEME=Retrowave
#SC64_BACKGROUND=/menu/backgrounds/Countach.png
#SC64_BGM=/menu/music/Need For Speed 4 High Stakes Soundtrack - Quantum Singularity (HD 1080p).wav64
#SC64_VIZ_STYLE=Oscilloscope
#SC64_VIZ_INTENSITY=Subtle
#SC64_TEXT_PANEL_ALPHA=160
../../N64 - USA/Mario Kart 64 (USA).z64
../../N64 - USA/F-Zero X (USA).z64
```

Smart playlist example:
```m3u
#SC64_SMART_ROOT=/N64 - USA
#SC64_FILTER_PUBLISHER=Ubisoft
#SC64_FILTER_YEAR=1998-2000
#SC64_SMART_SORT=YEAR
```

Another smart playlist example:
```m3u
#SC64_SMART_ROOT=/N64 - USA
#SC64_FILTER_GENRE=Racing
#SC64_FILTER_PLAYERS=>=2
#SC64_FILTER_MODES=Battle
#SC64_SMART_SORT=TITLE
```

Suggested metadata keys for `metadata.ini`:
```ini
[metadata]
name=Mario Kart 64
author=Nintendo
developer=Nintendo EAD
genre=Racing
series=Mario Kart
players=1-4
modes=Grand Prix, Versus, Battle
release-year=1996
age-rating=3
short-desc=Arcade kart racer with battle arenas and four-player split-screen.
```

### Browser Sorting and Random Selection
- Browser sorting modes:
  - `Custom` (playlist order)
  - `A-Z`
  - `Z-A`
- Sort preference is persisted in `sd:/menu/config.ini`.
- Random game jump is available from the browser (`L` button).
- Smart random modes are supported and persisted.

### Playtime, Last Played, and Sessions
- Per-game playtime tracking is recorded.
- ROM details page shows playtime stats (such as total playtime and last played).
- Recent play sessions are tracked.
- A playtime leaderboard tab is available for quick discovery of most-played games.

### Metadata and ROM Details Improvements
- Metadata can be loaded from the menu metadata database.
- ROM details include improved metadata display (description, publisher/rating coverage when available in metadata).
- Boxart fallback logic improves art coverage across regions / metadata entries.
- ROM details also show save backend and save file health information.
- Native manual packages can be launched from ROM details via `Open Manual`.

### Backgrounds, Themes, and Text Overlay
- Theme presets can be selected in Menu Settings.
- Background image can be changed from Menu Settings (without removing the SD card).
- Text panel overlay can be enabled and its opacity adjusted to improve readability over bright or detailed backgrounds.
- Playlist background overrides now use a cache-first path after the first load (decoded background surfaces are cached under `sd:/menu/cache/` for faster re-open).
- Background overrides can point to either a normal image path (for example `.png`) or a direct native `.nimg` replacement.

### Menu Background Music (BGM)
- Menu BGM autoplay is supported.
- BGM can be enabled/disabled in Menu Settings.
- Menu music file can be selected from `sd:/menu/music/`.
- Menu BGM supports both `MP3` and `WAV64` (WAV64 preferred if selected/available).
- `WAV64` (ADPCM) is recommended if you prioritize playback stability over file size.
- Recommended menu MP3 settings for smooth playback:
  - `32 kHz`
  - `48 kbps` stereo (use `64 kbps` for denser tracks)

### Visualizers
- Optional animated visualizer background modes are available in Menu Settings.
- Visualizers are audio-reactive for both MP3 and WAV64 menu BGM.
- Supported styles:
  - `Bars`
  - `Pulse Wash`
  - `Sunburst`
  - `Oscilloscope`
- Visualizer intensity can be set to `Subtle`, `Normal`, or `Full`.
- Selected row text shimmer (theme-colored rainbow cycle) can be toggled in Menu Settings.

### Asset Tooling (Backgrounds / Music / WAV64)
The customized build includes a helper script to speed up background/music prep:

- `tools/sc64/menu_assets.sh bg <input> <output.png>`
  - converts and crops to `640x480` PNG for menu backgrounds
- `tools/sc64/menu_assets.sh bg-native <input> <output.png.nimg>`
  - converts and crops to a `640x480` native `RGBA16` replacement/sidecar for faster background loads
- `tools/sc64/menu_assets.sh screensaver <input> <output.png>`
  - resizes/pads logos to `<=180x96`
- `tools/sc64/menu_assets.sh screensaver-native <input> <output.png.nimg>`
  - converts screensaver logos to an opt-in native `RGBA16` sidecar
- `tools/sc64/menu_assets.sh music-mp3 <input> <output.mp3> [bitrate_kbps]`
  - re-encodes menu-safe MP3 (`32 kHz`, stereo)
- `tools/sc64/menu_assets.sh music-wav64 <input-audio>`
  - converts one track to `WAV64` (ADPCM, `32 kHz`) next to the source file
- `tools/sc64/menu_assets.sh music-wav64-batch <dir>`
  - batch converts a music folder to `.wav64`
- `tools/sc64/menu_assets.sh rewrite-m3u-wav64 <m3u-or-dir> [more...]`
  - rewrites playlist `#SC64_BGM` / `#SC64_MUSIC` directives from `.mp3` to `.wav64` when a matching `.wav64` file exists

Tool requirements:
- `ffmpeg`
- `audioconv64` (from libdragon toolchain)

### Screensaver
- Three screensaver types are currently available:
  - `DVD Logo`
  - `3D Pipes`
  - `Living Gradient`
- Screensaver logo can be selected from `sd:/menu/screensavers/` for the DVD mode.
- Smooth mode is available to improve motion.
- Per-edge screensaver margins can be tuned to account for display/scaler visible-area cropping:
  - Left / Right / Top / Bottom margins

### Notes on Save Files and Organization
- Playlist-based organization does not duplicate ROM files or save files.
- Saves continue to use the normal ROM path/config logic; playlists are only an alternate view over the same ROM files.

### Related Documentation
- [File Browser](./31_file_browser.md)
- [Menu Settings](./32_menu_settings.md)
- [MP3 Player](./41_mp3_player.md)
- [Game Art Images](./19_gamepak_boxart.md)
- [Background Images](./16_background_images.md)

