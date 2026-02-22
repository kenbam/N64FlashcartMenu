[Return to the index](./00_index.md)
## Menu Enhancements (Playlists, UI, Audio, Stats)

This page documents the major menu enhancements added on the `ben/m3u-support` branch, with a focus on playlist-based organization, menu customization, and game tracking.

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
- `#SC64_BGM=<path to mp3>`
- `#SC64_MUSIC=<path to mp3>` (alias)

Notes:
- Relative directive paths are resolved from the playlist folder.
- Absolute flashcart paths such as `sd:/menu/music/...` are supported.
- Overrides are runtime-only and are restored automatically when you leave the playlist.

Example:
```m3u
#SC64_THEME=Retrowave
#SC64_BACKGROUND=/menu/backgrounds/Countach.png
#SC64_BGM=/menu/music/Need For Speed 4 High Stakes Soundtrack - Quantum Singularity (HD 1080p).mp3
../../N64 - USA/Mario Kart 64 (USA).z64
../../N64 - USA/F-Zero X (USA).z64
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
- Full descriptions are supported, with `description.txt` fallback support.
- Boxart fallback logic improves art coverage across regions / metadata entries.
- ROM details also show save backend and save file health information.

### Backgrounds, Themes, and Text Overlay
- Theme presets can be selected in Menu Settings.
- Background image can be changed from Menu Settings (without removing the SD card).
- Text panel overlay can be enabled and its opacity adjusted to improve readability over bright or detailed backgrounds.

### Menu Background Music (BGM)
- Menu BGM autoplay is supported.
- BGM can be enabled/disabled in Menu Settings.
- Menu music file can be selected from `sd:/menu/music/`.
- Recommended menu MP3 settings for smooth playback:
  - `32 kHz`
  - `48 kbps` stereo (use `64 kbps` for denser tracks)

### Screensaver
- Idle DVD-logo screensaver is available.
- Screensaver logo can be selected from `sd:/menu/screensavers/`.
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

