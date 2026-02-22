# Rolling release
built from latest commit on main branch.  
**Note**: the attached source code files may be out of date.

- For the SummerCart64, use the `sc64menu.n64` file in the root of your SD card.
- For the 64Drive, use the `menu.bin` file in the root of your SD card.
- For the ares emulator, use the `N64FlashcartMenu.n64` file.

## Branch Notes (ben/m3u-support)

This section summarizes the major user-facing work added on the `ben/m3u-support` branch.

### Playlists and Browser
- Added `.m3u` playlist support for ROM browsing, including preserved playlist order (`custom` sort mode). (`ce7c65c1`)
- Added browser sorting modes (`Custom`, `A-Z`, `Z-A`) and persisted sort preference in settings. (`890a835b`, `9f4d5d96`)
- Added random game jump from the browser and later smart-random modes with persistence. (`35aae24e`, `40b4cc0c`)
- Refined file list row rendering and selector alignment / visible-row handling. (`09a22fe6`)
- Clipped scrollable UI regions and reduced action-bar crowding. (`d78c0c0b`)

### ROM Metadata and Details
- Improved ROM details screen with metadata database loading and richer metadata display. (`e8a6275c`)
- Hardened metadata parsing and expanded description handling. (`91da44af`)
- Added `description.txt` fallback loading for full descriptions. (`01a2e915`)
- Added metadata boxart fallback behavior for improved image coverage. (`890a835b`)
- Added save backend and save file health information to ROM details. (`3ee948cd`)

### Playtime and Session Tracking
- Added per-game playtime tracking (including total time and last played display). (`a2e6c76d`)
- Added recent play session tracking in bookkeeping/history. (`c0e9ed0f`)
- Added a playtime leaderboard tab and navigation. (`639b1d43`)

### Menu Audio and MP3 Experience
- Added menu BGM autoplay with MP3 playback stability improvements. (`62b9142e`)
- Added settings support to enable/disable menu BGM and choose a menu music file from `sd:/menu/music/`. (`15aa6241`)
- Fixed playlist BGM override path handling for absolute prefixed paths (e.g. `sd:/...`). (`70936409`)

### Visual Customization and Themes
- Added configurable text panel overlay (including opacity/alpha) to improve readability over backgrounds. (`c332a085`, `d1f26a2d`)
- Added in-menu background image picker entry. (`2abfef26`)
- Added theme presets and theme selection support in settings. (`df2279d0`)

### Screensaver
- Added idle DVD-logo screensaver. (`df2279d0`)
- Added selectable screensaver logos from `sd:/menu/screensavers/`. (`15aa6241`)
- Added screensaver smooth mode / motion tuning and configurable safe margins for display crop compensation. (`a16342b1`)

### Per-Playlist Immersion Overrides
- Added temporary per-playlist theme, background, and BGM overrides via M3U comment directives (restored on exit). (`7f7d1d91`)

#### Supported M3U directives
- `#SC64_THEME=...`
- `#SC64_BACKGROUND=...` (or `#SC64_BG=...`)
- `#SC64_BGM=...` (or `#SC64_MUSIC=...`)

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
