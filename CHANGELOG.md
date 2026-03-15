# Rolling release
built from latest commit on main branch.  
**Note**: the attached source code files may be out of date.

- For the SummerCart64, use the `sc64menu.n64` file in the root of your SD card.
- For the 64Drive, use the `menu.bin` file in the root of your SD card.
- For the ares emulator, use the `N64FlashcartMenu.n64` file.

## Fork Notes (custom `main`)

### Overview
- Browsing now combines filesystem browsing, `.m3u` playlists, metadata-driven smart playlists, history/favorites, playtime views, and grid-based media browsing.
- Presentation features now include custom backgrounds, theme presets, menu music, reactive visualizers, screensavers, attract mode, richer ROM details, and manual viewing.
- Launch-related additions now include 64DD pairing flows, virtual Controller Pak slot management, ROM patch groundwork, load-path recovery, and stronger save/config write hardening.
- The repository now also carries local content and tooling including personal playlists, metadata snapshots, reports, playlist templates, patch examples, and helper scripts for assets and metadata.

### Browser, Playlists, and Library Navigation
- Added `.m3u` playlist browsing with preserved playlist order through `Custom` sorting. (`ce7c65c1`)
- Added browser sort modes (`Custom`, `A-Z`, `Z-A`) and persisted sort selection in settings. (`890a835b`, `9f4d5d96`)
- Added browser random jump and later smart-random browsing modes with persistent selection. (`35aae24e`, `25e002ac`, `40b4cc0c`, `d3a58a2f`)
- Added playlist profile intro toasts and companion text/context handling for playlist presentation. (`019a2364`, `ced29ac4`)
- Improved browser UX with faster scrolling, cleaner list behavior, selector alignment fixes, clipped scroll regions, better empty-folder escape, and hiding of Datel sidecar clutter. (`3b28ea5f`, `09a22fe6`, `d78c0c0b`, `3ec19fb7`)
- Expanded favorites capacity and improved navigation from history/playtime-driven entry points. (`bc3ce63a`, `a9e57fd0`, `5054f486`)
- Additional browsing changes:
- normal filesystem browsing
- playlist browsing with preserved authored order
- random pick / smart-random pick flows
- favorites and recently played entry points
- playtime leaderboard and play-history style discovery
- playlist-led curated browsing rather than only folder-led browsing

### Smart Playlists, ROM Identity, and Discovery
- Added metadata-driven smart playlists and expanded their rule set. (`259b6347`, `35c4da35`)
- Added ROM identity tracking so playlists and references can survive moved ROM paths. (`1beef2d0`, `e1119ead`, `b25cebe1`)
- Added descriptive text for fun playlists and fixed blank playlist description panels. (`057cb422`, `97de0ae3`)
- Added and documented playlist performance telemetry and the internal 3-tier cache model. (`9fe524cf`, `6e754ec7`)
- Additional smart-playlist and discovery changes:
- metadata-derived library slices
- playlist references that can survive path churn
- companion text/context files for curated lists
- telemetry and caching work to keep heavier discovery features responsive

### Grid View and Media Browsing
- Added playlist boxart grid view with toggle shortcuts. (`cef0a124`, `05f136e0`)
- Reworked grid loading with queued thumbnail prep, metadata caching, neighboring-page prefetch, LRU caching, incremental prewarm, and cleaner tile rendering. (`b075dae5`, `59178952`, `005b86f9`, `39e3a0bd`, `87b93981`)
- Simplified grid rendering by removing redundant fallback work, dead cache paths, white selection fill, and grid-only thumbnail behavior that was not paying for itself. (`5939d16e`, `4d775edf`, `bdaf1f7d`, `1fbecb5a`)
- Added lightweight ROM-header reads and cached boxart directory resolution to reduce repeated SD lookups. (`8603d874`, `30993627`, `9f073d26`)
- Additional media-browsing changes:
- a dedicated playlist grid mode
- queued thumbnail generation and caching
- neighboring-page prefetch and prewarm logic
- lighter ROM-header probes and reduced repeated SD-card stats
- cleanup passes that removed grid work that hurt responsiveness without enough payoff

### ROM Details, Metadata, Saves, and Manuals
- Expanded the ROM details screen to load metadata from the menu database and show richer per-game information. (`e8a6275c`)
- Hardened metadata parsing and added `description.txt` fallback handling for full descriptions. (`91da44af`, `01a2e915`)
- Added metadata boxart fallback behavior and improved ROM-detail performance by moving repeated SD access out of the render loop. (`890a835b`, `2d577d03`, `ad730c50`)
- Added save backend and save-file health visibility in ROM details. (`3ee948cd`)
- Added release-year enrichment and SC64-backed playtime bookkeeping. (`5c5a2c20`)
- Added native manual support with image-backed viewing, then removed only the tiled-manual beta path while keeping the manual feature itself and a manual availability indicator. (`27209739`, `74f8059c`, `9ae2b9cc`)
- Hardened manual/image loading with duplicate-load removal, dimension caps, path sanitizing, and lower stack pressure. (`85e7ad64`, `d1fe5b6e`, `a386565d`, `05a2cd76`, `8bc63d42`)
- Additional ROM-detail and manual changes:
- database metadata with richer description support
- boxart fallback behavior
- save-backend and save-health visibility
- release-year enrichment
- manual availability detection
- native image-backed manual viewing
- lower-I/O ROM detail rendering compared with earlier builds

### Playtime, History, and Bookkeeping
- Added per-game playtime tracking with total time and last-played visibility. (`a2e6c76d`)
- Added recent play-session tracking and a playtime leaderboard tab. (`c0e9ed0f`, `639b1d43`)
- Tightened playtime storage and lookup behavior with cached-only lookups, INI bounds checks, safer formatting, and deferred writes. (`52a1cc85`, `b4a9e50a`, `b631dd83`, `02fa7221`)
- Additional bookkeeping changes:
- per-game total playtime
- recent-session tracking
- last-played style visibility
- playtime leaderboard surfacing
- safer and cheaper writes than the earlier implementation

### Themes, Backgrounds, Music, and Visualizers
- Added menu BGM autoplay and improved MP3 playback stability. (`62b9142e`)
- Added selectable menu music from `sd:/menu/music/`, then extended menu audio/background handling with WAV64 support and media-pipeline acceleration. (`15aa6241`, `4472ea81`, `99374aca`)
- Added configurable text-panel overlays to improve readability over custom backgrounds. (`c332a085`, `d1f26a2d`)
- Added in-menu background image picking, theme presets, additional themes, and a selected-row shimmer toggle. (`2abfef26`, `df2279d0`, `4444ebb0`, `e4172657`)
- Added music-reactive visualizer backgrounds including pulse wash, sunburst, and oscilloscope styles. (`41f650ae`, `274bce3a`, `b7146275`)
- Added playlist-level presentation overrides for theme, background, music, and visualizer behavior. (`7f7d1d91`, `70936409`, `b7146275`)
- Additional presentation changes:
- theme presets and additional themes
- per-menu background image selection
- selectable menu music
- MP3 plus WAV64-backed audio handling
- readability overlays over custom art
- playlist-specific visual and audio overrides
- reactive background modes rather than only static backdrops

### Screensavers and Attract Mode
- Added the idle DVD-logo screensaver and support for selectable custom screensaver logos from `sd:/menu/screensavers/`. (`df2279d0`, `15aa6241`)
- Added safe margins and motion tuning for display-crop compensation. (`a16342b1`)
- Added random and attract-mode screensavers, polished the attract description panel, and tuned attract pacing. (`87233637`, `7cd306c6`, `cf8298aa`)
- Refined the broader screensaver system, including pipes and living-gradient variants with textured blob rendering. (`a78e44ae`, `5be77748`, `fb5f83cb`, `994f784b`)
- Later removed the heavy attract transition effect and fixed attract selection so it draws from the full discovered pool instead of feeling biased toward early-scanned games. (`bac60710`)
- Additional screensaver changes:
- DVD-logo idle mode
- random screensaver selection
- attract mode with rotating games
- pipes and living-gradient variants
- custom logo loading from SD
- safer attract behavior after transition removal and full-pool randomization fixes

### 64DD Pairing and Combo Launch
- Added 64DD pairing helpers, remembered disk preferences, and browser-picker support for combo ROM flows. (`19a2be3f`, `8049f245`, `b92d343c`)
- Added auto-launch for single compatible disks and filtering/picker logic for multiple candidates. (`2127de9a`, `d3a010d3`)
- Added fallback behavior for missing or unmatched disks, including explicit ROM-only launch. (`a2733e16`, `4635f6f9`, `ffad2d8f`)
- Preserved combo auto-launch through the load-disk flow and later avoided unnecessary whole-card discovery scans. (`1ca45746`, `7415b486`)
- Restored and refined the 64DD pairing flow after merge work. (`423af155`)
- Additional 64DD changes:
- remembered disk pairing preferences
- combo-ROM-aware browser pickers
- auto-launch for simple one-match cases
- ROM-only fallback when no good disk match exists
- fewer expensive whole-card scans during discovery

### Virtual Controller Pak
- Added per-ROM virtual Controller Pak slots stored on SD card. (`c5f7c733`)
- Hardened the early virtual Pak work and reduced related playlist-loading overhead. (`145d9fff`)
- Reworked the feature into a safer physical-Pak swap flow with four slots per game, clearer slot controls, better physical-Pak detection, and stronger launch validation. (`1eff3028`)
- Fixed blank error-box behavior so launch failures show real text instead of a tiny empty black box. (`1eff3028`)
- Added progress reporting during virtual Pak backup, load, and verify stages so pre-launch swaps do not look like a frozen UI. (`1eff3028`)
- Added journaled recovery for interrupted sessions, browser-side recovery handling, and force-clear behavior for abandoned recovery state. (`1eff3028`)
- Additional virtual Pak changes:
- per-game SD-backed pak slots
- four slots per game
- physical Controller Pak required on original hardware
- clearer error messaging around hardware requirements
- visible progress during swap work
- recovery journaling rather than a one-shot blind swap
- browser-level recovery handling for pending sessions

### Performance, Reliability, and Safety Hardening
- Reduced SD-card I/O in hot paths through cached favorites, static-playlist caches, prewarm tuning, deferred heavy overrides, and playlist/load-path caching. (`4a8dff40`, `beaf04c3`, `ec68e03f`, `e7722a01`, `0f6bb857`, `35bbd7eb`, `af11844a`, `02fa7221`)
- Poll-audio safeguards were added during heavier smart-playlist scans to reduce BGM dropouts. (`f24451c7`)
- Hardened writes with atomic `mini_save`, FAT32-safe rename handling, atomic INI writes, controller pak dump safety, and `.tmp` recovery when loading INI files. (`dfd07f0d`, `4f87b9f9`, `f034a0e7`, `cceb93d8`, `86eac8d8`)
- Fixed broader memory-safety and robustness issues including unchecked realloc, double-free risk, signed-overflow growth, null-guarding, stack reduction, and widespread `snprintf` conversions. (`57d2ce70`, `52a1cc85`, `bfef645c`, `4a7c62bb`, `0b388958`, `d1badfe0`, `572dbce2`, `de1f0afd`)
- Enabled `-Wextra` and cleaned up the warning fallout across the codebase. (`a90a6cc0`)
- Additional reliability changes:
- much less per-frame or repeated SD I/O in browse/detail flows
- safer write patterns for config and pak-related files
- more resilience to resets/power loss during file updates
- lower stack pressure and fewer avoidable allocation hazards
- more compiler-driven cleanup than upstream

### ROM Patch Groundwork
- Added experimental ROM patch pipeline groundwork and later hardened the patch path with safer buffers and null guards. (`ef5843d9`, `0b388958`)
- Patch support in this fork is still groundwork-oriented rather than a finished flagship feature, but the menu and docs now carry examples and infrastructure for patch-driven workflows.

### Documentation, Tooling, and Fork Content
- Refreshed customized build documentation and added branch/fork feature writeups in the changelog and README. (`3ee113c4`, `12a19e95`, `0503b80e`, `5dec6046`, `c8246c09`, `e00937da`, `c20b4bf6`)
- Documented playlist immersion directives, visualizer behavior, and asset conversion helpers. (`473a3412`, `602e29f7`)
- Added backup docs, playlist templates, and tracked local metadata/playlist snapshots used by this fork. (`2272eb51`, `432deaa5`, `04d3ca71`, `a914b263`, `cfe7ac8f`)
- Repository content added by the fork now includes:
- example playlists and patch examples
- personal curated playlists by year, genre, studio, history, and ranked themes
- metadata and playlist audit reports
- menu asset tooling and metadata helper scripts
- manual-build experiments and planning docs
- branch-specific docs for backups, ideas, firmware notes, and feature behavior

### Supported playlist directives
- `#SC64_THEME=...`
- `#SC64_BACKGROUND=...` or `#SC64_BG=...`
- `#SC64_BGM=...` or `#SC64_MUSIC=...`

## Release Notes 2025-12-04 - Tagged 0.3.1

- **New Features**
	- Settings contexts now preset to the saved option.
	- Added latest Viewpoint64 final proto ROM to database.
	- Added Rumble PAK and Transfer PAK features to ROM info screen.

- **Bug Fixes**
	- Fixed MP3 Player crashes menu if the MP3 file's sample rate is less than 44100 hz and menu SFX are enabled.
	- Fixed game_code_path size that caused crash when loading homebrew boxart.
	- Fixed boot process which could lead to blank screens or crashes.
	- Fixed a potential issue that could happen when a RTC was not detected.


- **Documentation**
	- Moved ED64 documentation to [98_flashcart_wip.md](./docs/98_flashcart_wip.md)
	- Other minor fixes.

- **Refactor**
	- Output 4MB files as MB, rather than kB.
	- Improved icons for direction.
	- Controller Pak now selects notes using up/down rather than left/right.

- **Other**
	- Updated libDragon SDK.
	- Updated docker container to Trixy

### Breaking changes
- None.


### Current known Issues
- Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
- Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.


### Deprecation notices
- None.


## Release Notes 2025-11-15 - Tagged 0.3.0

- **New Features**
	- Added ability to hide save folders (on by default).
	- Added ability to reset the menu setting to default from the menu UI.
	- Updated the UI font to Firple-Bold which supports more characters.
	- Shows info message within the loading progress bar.
	- Add the ability to display ESRB age ratings (see [documentation](./docs/65_experimental.md)).
	- Add Beta Datel code GUI (see [documentation](./docs/13_datel_cheats.md)).
	- Add ability to load boxart from ROMs that use the homebrew header (see [documentation](./docs/19_gamepak_boxart.md)).
	- Add ability to extract files from ZIP archives (thanks [VicesOfTheMind](https://github.com/VicesOfTheMind)).
	- Add Alpha FEATURE_PATCHER_GUI_ENABLED (build flag to enable it).
	- Add Controller Pak manager (thanks [LuEnCam](https://github.com/LuEnCam))
	- Add Game art image switching (thanks [dpranker](https://github.com/dpranker))

- **Bug Fixes**
	- Fix ability to set the RTC via menu (Hotfixed in last release).
	- Fix Game ID (used by PixelFX HDMI mods) sent over Joybus is not working (Hotfixed in last release).
	- Fix GB / GBC emulator not saving in certain circumstances (Hotfixed in last release).
	- Fix issue with emulation of cold boot, as otherwise the FPU might start in an unexpected state.
	- Fix missing enum case for 1 Mbit SRAM saves (Hotfixed in last release).

- **Documentation**
	- Improved Emulator information for known working NES emulator version.
	- Updated experimental features to reflect feature change.
	- Added sounds documentation.
	- Updated autoload to reflect feature change.

- **Refactor**
	- Improve tab navigation by using any left/right control input and add cursor SFX.
	- Add ability for font style to be used in ui_components_main_text_draw and ui_components_actions_bar_text_draw.

- **Other**
	- Updated libDragon SDK.
	- Updated miniz library.
	- Updated Github templates.

### Breaking changes
* Deprecated "Autoload ROM" function was removed from menu (use `FEATURE_AUTOLOAD_ROM_ENABLED` as a build flag to re-enable it).
* Deprecated Boxart image handler was removed (see [documentation](./docs/19_gamepak_boxart.md) for new boxart link).
* ROM's that used custom CIC, TV and/or Save type set from the menu will need to re-set them, now uses "custom_boot" header within the ini file.


### Current known Issues
* Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
* Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.
* MP3 Player crashes menu if the MP3 file's sample rate is less than 44100 hz and menu SFX are enabled.


### Deprecation notices
* Boxart directory has changed to metadata directory.


## Release Notes 2025-03-31 - Tagged 0.2.0

- **New Features**
	- Introduced tabs in main menu for ROM favorites and recently played ROM history.
	- Introduced first run check to ensure users are aware of latest changes.
	- Introduced ability to turn off GUI loading bar.
	- BETA_FEATURE: Introduces ROM descriptions from files.
	- BETA_FEATURE: Enabled setting for fast ROM reboots on the SC64.
	- Add macOS metadata to hidden files.
	- Added settings schema version for future change versioning.
	- Added setting for PAL60 compatibility mode (see breaking changes).
	- BETA_FEATURE: Added setting for line doublers that need progressive output, enable using "force_progressive_scan" setting in `config.ini`.


- **Bug Fixes**
	- Menu sound FX issues (hissing, popping and white noise).
	- RTC not showing or setting correct date parameters in certain circumstances.
	- ~~GB / GBC emulator not saving in certain circumstances.~~


- **Documentation**
	- Re-orginised and improved user documentation.
	- Added a lot of doxygen compatible code comments.
	- Added project license.


- **Refactor**
	- RTC subsystem (align with libDragon improvements).
	- Boxart images (Deprecates old boxart image folder layout).
	- Settings (PAL60 compatibility, schema version, fast reboot, first run, progress bar).

- **Other**
	- Updated libDragon SDK.
	- Updated miniz library.

### Breaking changes
* ~~GB /GBC emulator changed save type to SRAM (from FRAM) to improve compatibility with Summercart64 (which only uses H/W compatible FRAM), this may break your ability to load existing saves.~~
* For similar PAL60 functionality, you may need to also enable the new "pal60_compatibility_mode" setting in `config.ini`.


### Current known Issues
* The RTC UI requires improvement (awaiting UI developer).
* Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
* Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.
* MP3 Player crashes menu if the MP3 file's sample rate is less than 44100 hz.


### Deprecation notices
* Autoload ROM's will be deprecated in favor of Fast Reboot in a future menu version.
* Old boxart images using filenames for game ID is deprecated and the compatibility mode will be removed in a future release.


## Release Notes 2025-01-10

- **Bug Fixes**
	- Fixed menu display (PAL60) by reverted libdragon to a known working point and re-applying old hacks.

### Current known Issues
* The RTC UI requires improvement (awaiting UI developer).
* Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
[Pre-release menu]:
* BETA_SETTING: PAL60 when using HDMI mods has regressed (awaiting libdragon fix).
* ALPHA_FEATURE: ED64 X Series detection does not occur properly (however this is not a problem as not tag released asset).
* ALPHA_FEATURE: ED64 V Series only supports loading ROMs (however this is not a problem as not tag released asset).


## Release Notes 2024-12-30

- **New Features**
	- Introduced menu sound effects for enhanced user experience (the default is off).
	- Added N64 ROM autoload functionality, allowing users to set a specific ROM to load automatically.
	- Added menu boot hotkey (hold `start` to return to menu when autoload is enabled).
	- Added context menu and settings management options GUI for managing various settings in `config.ini`.
	- Added functionality for editing the real-time clock (RTC) within the RTC menu view.
	- Improved flashcart info view for showing supported flashcart features and version.
	- Enhanced UI components with new drawing functions and improved organization.
	- Added emulator support for `SMS`, `GG`, and `CHF` ROMs.
	- Enhanced joypad input handling for menu actions, improving responsiveness.
	- Optimized boxart image loading from filesystem.
	- Improved various text to make the functionality more clear.

- **Bug Fixes**
	- Improved error handling in multiple areas, particularly in save loading and ROM management.
	- Enhanced memory management to prevent potential leaks during error conditions.
	- Fixed text flickering in certain circumstances.

- **Documentation**
	- Updated README and various documentation files to reflect new features and usage instructions.
	- Added detailed setup instructions for SD cards and menu customization.
	- Enhanced clarity in documentation for RTC settings and menu customization.
	- Improved organization and clarity of SD card setup instructions for various flashcarts.

- **Refactor**
	- Standardized naming conventions across UI components for better organization.
	- Restructured sound management and input handling for improved responsiveness.
	- Streamlined the loading state management for ROMs and disks within the menu system.
	- Improved clarity and usability of the developer guide and other documentation files.

### Current known Issues
* BETA_SETTING: PAL60 when using HDMI mods has regressed (awaiting libdragon fix).
* The RTC UI requires improvement (awaiting UI developer).
* Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
* ALPHA_FEATURE: ED64 X Series detection does not occur properly (however this is not a problem as not tag released asset).
* ALPHA_FEATURE: ED64 V Series only supports loading ROMs (however this is not a problem as not tag released asset).

### Breaking changes
* Disk drive expansion ROMs are now loaded with `Z|L` instead of `R` to align with ROM info context menu (and future functionality).
