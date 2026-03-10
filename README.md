![Build](https://github.com/polprzewodnikowy/N64FlashcartMenu/actions/workflows/build.yml/badge.svg)
![GitHub Org's stars](https://img.shields.io/github/stars/Polprzewodnikowy/N64FlashcartMenu)
[![Average time to resolve an issue](http://isitmaintained.com/badge/resolution/Polprzewodnikowy/N64FlashcartMenu.svg)](http://isitmaintained.com/project/Polprzewodnikowy/N64FlashcartMenu "Average time to resolve an issue")
[![Percentage of issues still open](http://isitmaintained.com/badge/open/Polprzewodnikowy/N64FlashcartMenu.svg)](http://isitmaintained.com/project/Polprzewodnikowy/N64FlashcartMenu "Percentage of issues still open")
[![#yourfirstpr](https://img.shields.io/badge/first--timers--only-friendly-blue.svg)](https://github.com/Polprzewodnikowy/N64FlashcartMenu/blob/main/CONTRIBUTING.md)

# N64 Flashcart Menu
An open source menu for N64 flashcarts that aims to support as many as possible.  
This menu is not affiliated with any particular flashcart and does not necessarily expose all possible firmware features.

> [!TIP]
> Help sponsor development [NetworkFusion on Ko-Fi](https://ko-fi.com/networkfusion). Or submit your Pull Request.

> [!TIP]
> New users are invited to read the latest [Documentation / User Guide](./docs/00_index.md).

## Flashcart Support
This menu aims to support as many N64 flashcarts as possible.  
The current state of support is:

### Supported
* SummerCart64
* 64Drive

### Work in Progress
* EverDrive-64 (X and V series)
* ED64P (clones)

### Not yet planned
* Doctor V64
* PicoCart
* DaisyDrive


## Current (notable) menu features
* Fully Open Source.
* Loads all known N64 games, even if they are byteswapped.
* Fully emulates the 64DD and loads 64DD disks (SummerCart64 only).
* Emulator support (NES, SNES, GB, GBC, SMS, GG, CHF) ROMs.
* N64 ROM box art image support.
* Background image (PNG) support.
* Comprehensive ROM save database (including homebrew headers).
* Comprehensive ROM information display.
* Real Time Clock support.
* Music playback (MP3).
* Menu sound effects.
* N64 ROM fast reboot option (on reset).
* ROM history and favorites.  

Experimental (beta):
* ROM Datel code editor.
* Zip archive browsing and file extraction.
* Controller Pak backup and restore (including individual notes).
* Game art image switching.

## Enhanced features in this customized build
Important: this repo currently includes a personal/custom setup.  
It is tuned for one user workflow and includes experimental changes that are not aimed at broad compatibility.

### Stable quality-of-life additions
Playlist support (M3U):
* Relative and absolute paths.
* Playlist order preserved as authored.
* Better path handling when jumping from Favorites/Recent/Playtime.
* Browser sort modes (Custom/M3U order, A-Z, Z-A).
* Support for playlist-based organization without duplicating ROM/save files.

Playlist personalization:
* Per-playlist theme/background/music.
* Per-playlist view mode (list/grid).
* Per-playlist description text.
* Per-playlist screensaver logo and visualizer settings.
* Per-playlist text panel/alpha and immersion profile behavior.
* Metadata-based smart playlists (publisher/year/genre/series/players/modes/etc).

Playtime and browsing:
* Last played, total time, recent sessions, and leaderboard.
* “Last played” visible in browser/playlist secondary column.
* Fast scroll behavior in Playtime list.
* Quick random game jump and smart-random mode options.
* Cleaner browser view (sidecar clutter hidden, safer empty-folder behavior).
* Favorites/Recent/Playtime-to-browser pathing fixes.

ROM details + metadata:
* Expanded ROM details view with richer metadata display.
* Title, publisher, developer, genre, series, players, modes, year, rating fields, save/feature info, and long description rendering.
* Year field shown on ROM details page.
* ROM details page scrolling support (long content no longer clipped/overlapped).
* Metadata parser accepts both `[meta]` and `[metadata]`.
* Expanded year key parsing (`year`, `release-year`, `release-date`, etc).
* Supports richer metadata keys such as `developer`, `genre`, `series`, `players`, and `modes`.
* Metadata-to-ROM matching and coverage audits/backfills for large sets.
* Native manual viewer with per-game manifests, page packages, and optional higher-resolution zoom assets.

### Playtime + SC64 firmware integration
This is a major custom feature set in this customized build.

Playtime tracking:
* Tracks `last played`, `total playtime`, and recent sessions.
* Includes a Playtime leaderboard view.
* Uses `last played` in browser secondary column.

SC64 firmware handoff:
* Adds SC64 setting read/write plumbing in the flashcart layer.
* Starts session markers before ROM handoff.
* Finalizes session duration from firmware-side counters on return.
* Clears firmware tracker state after ingest.
* Uses fallback logic to reduce menu-side double counting when SC64 tracker is available.
* Current status: required firmware-side changes are local/unpublished right now.
* TODO: publish firmware changes to a dedicated fork branch.

Operational note:
* Behavior still depends on firmware-side counting policy while powered.
* Edge cases (e.g. USB-powered idle/off states) are firmware-sensitive and can affect totals.

### Visual and media enhancements
Themes and UI polish:
* Additional built-in themes.
* Includes presets such as Gruvbox, Nord, Solarized, Dracula, CRT Green, Retrowave, and more.
* Optional shimmer/highlight styling.
* Improved playlist description presentation (dedicated panel under tabs).
* Improved text/background contrast controls (text panel + alpha).

Screensaver:
* Three selectable modes: `DVD Logo`, `3D Pipes`, and `Living Gradient`.
* Selectable logo assets for the DVD mode.
* Motion/collision tuning improvements.
* Idle behavior integration with menu runtime settings.

Audio pipeline:
* WAV64 menu BGM support (alongside MP3).
* Better stability for long sessions and heavy UI scenes.
* Menu music selector flow and playlist BGM override handling.

### Beta / experimental (use with caution)
Visualizer system (experimental):
* Music-reactive background modes: Bars, Pulse Wash, Sunburst, Oscilloscope.
* Playlist-level visualizer overrides.
* Still beta; performance and smoothness vary by scene and track.

Grid view for playlists (experimental):
* Boxart grid mode with caching/prefetch.
* Ongoing perf tuning; behavior can vary with SD speed and metadata state.

Additional branch-level additions:
* Datel cheat files support and cleaner cheat-sidecar handling in browser.
* By-Year playlist generation and large curated playlist packs (ranked/history/studio/fun).
* Asset conversion/optimization workflow for backgrounds, boxart thumbs, and music.

### Notes and references
* See `CHANGELOG.md` for an ongoing log.
* See `docs/66_bug_tracker_personal.md` for known issues and personal tracking notes.
* See `docs/68_personal_next_steps_firmware_update.md` for SC64 firmware update workflow notes.
* Key commits for this customization wave include:
  `5c5a2c20`, `3b28ea5f`, `99374aca`, `fb5f83cb`, `994f784b`, `a78e44ae`, `5be77748`.


## Aims
* Support as many N64 Flashcarts as possible.
* Be open source, using permissively licensed third-party libraries.
* Be testable in an emulated environment (Ares).
* Encourage active development from community members and N64 FlashCart owners.
* Support as many common mods and features as possible (flashcart dependent).


## Flashcart specific information

### SummerCart64
Download the latest `sc64menu.n64` file from the [releases](https://github.com/Polprzewodnikowy/N64FlashcartMenu/releases/) page, then put it in the root directory of your SD card.  
  
> [!TIP]
> A quick video tutorial can be found here:
>
> [![Video tutorial](https://img.youtube.com/vi/IGX0XXf0wgo/default.jpg)](https://www.youtube.com/shorts/IGX0XXf0wgo)


### 64drive
* Ensure the cart has the latest [firmware](https://64drive.retroactive.be/support.php) installed.
* Download the latest `menu.bin` file from the [releases](https://github.com/Polprzewodnikowy/N64FlashcartMenu/releases/) page, then put it in the root directory of your SD card.


# Contributors
The features in this project were made possible by the [contributors](https://github.com/Polprzewodnikowy/N64FlashcartMenu/graphs/contributors).

# License
This project is released under the [GNU AFFERO GENERAL PUBLIC LICENSE](LICENSE.md) as compatible with all other dependent project licenses.  
Other license options may be available upon request with permissions of the original `N64FlashcartMenu` project authors / maintainers.  
* [Mateusz Faderewski / Polprzewodnikowy](https://github.com/Polprzewodnikowy)
* [Robin Jones / NetworkFusion](https://github.com/networkfusion)

# Open source software and licenses used
## Libraries
* [libdragon](https://github.com/DragonMinded/libdragon/tree/preview) - [UNLICENSE License](https://github.com/DragonMinded/libdragon/blob/preview/LICENSE.md)
* [libspng](https://github.com/randy408/libspng) - [BSD 2-Clause License](https://github.com/randy408/libspng/blob/master/LICENSE)
* [mini.c](https://github.com/univrsal/mini.c) - [BSD 2-Clause License](https://github.com/univrsal/mini.c?tab=BSD-2-Clause-1-ov-file#readme)
* [minimp3](https://github.com/lieff/minimp3) - [CC0 1.0 Universal](https://github.com/lieff/minimp3/blob/master/LICENSE)
* [miniz](https://github.com/richgel999/miniz) - [MIT License](https://github.com/richgel999/miniz/blob/master/LICENSE)

## Sounds
See [License](https://pixabay.com/en/service/license-summary/) for the following sounds:
* [Cursor sound](https://pixabay.com/en/sound-effects/click-buttons-ui-menu-sounds-effects-button-7-203601/) by Skyscraper_seven (Free to use)
* [Actions (Enter, Back) sound](https://pixabay.com/en/sound-effects/menu-button-user-interface-pack-190041/) by Liecio (Free to use)
* [Error sound](https://pixabay.com/en/sound-effects/error-call-to-attention-129258/) by Universfield (Free to use)

## Emulators
* [neon64v2](https://github.com/hcs64/neon64v2) by *hcs64* - [ISC License](https://github.com/hcs64/neon64v2/blob/master/LICENSE.txt)
* [sodium64](https://github.com/Hydr8gon/sodium64) by *Hydr8gon* - [GPL-3.0 License](https://github.com/Hydr8gon/sodium64/blob/master/LICENSE)
* [gb64](https://github.com/lambertjamesd/gb64) by *lambertjamesd* - [MIT License](https://github.com/lambertjamesd/gb64/blob/master/LICENSE)
* [smsPlus64](https://github.com/fhoedemakers/smsplus64) by *fhoedmakers* - [GPL-3.0 License](https://github.com/fhoedemakers/smsplus64/blob/main/LICENSE)
* [Press-F-Ultra](https://github.com/celerizer/Press-F-Ultra) by *celerizer* - [MIT License](https://github.com/celerizer/Press-F-Ultra/blob/master/LICENSE)

## Fonts
* [Firple](https://github.com/negset/Firple) by *negset* - (SIL Open Font License 1.1)
