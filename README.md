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

## Enhanced features in this branch (personal/custom)
This branch includes substantial quality-of-life work on top of upstream.  
For full details, see `CHANGELOG.md` and commit history.

Core additions:
* M3U playlist support with relative and absolute paths, preserving playlist order (`ben/m3u-support`).
* Playlist profile directives for per-playlist theme/background/music/visualizer/text panel/screen saver/logo/view mode (`7f7d1d91`, `70936409`, `473a3412`).
* Smart random picker modes and quick random jump navigation (`05f136e0` and later follow-ups).
* Playtime tracking, leaderboard, and recent sessions (`5c5a2c20`, plus supporting UI commits).
* SC64 firmware setting integration hooks for persistent playtime handoff (`5c5a2c20`).

UI/UX improvements:
* Playlist boxart grid view with runtime toggle, caching, and async prefetch (`b075dae5`, `cef0a124`, `59178952`, `005b86f9`, `05f136e0`).
* Browser improvements: last-played secondary column, hidden sidecar clutter, safer empty-folder escape, and fast scroll in Playtime (`3ec19fb7`, `3b28ea5f`).
* Playlist description panel under tabs with reserved layout space and marquee-style scrolling for long text (`3b28ea5f`).
* Additional theme presets and selected-row shimmer toggle (`4444ebb0`, `e4172657`).
* Screen saver improvements and selectable logos (`df2279d0`, `15aa6241`, `a16342b1`).

Audio/media pipeline:
* Menu BGM improvements with WAV64 support and reduced playback stutter (`4472ea81`, `99374aca`).
* Music-reactive background visualizer modes: bars, pulse wash, sunburst, oscilloscope (`41f650ae`, `274bce3a`, `b7146275`).
* Visualizer/background performance tuning and cache behavior updates (`4472ea81`, `99374aca`).

Metadata and content tooling:
* Expanded metadata loading (publisher/rating/year handling), richer ROM details page fields, and metadata lookup hardening (`5c5a2c20`).
* Boxart restoration/improvements plus grid thumbnail workflow (`cef0a124`, `99374aca`).
* Asset conversion and optimization tooling for menu media (`602e29f7`, `99374aca`).
* Personal bug tracker + implementation notes (`docs/66_bug_tracker_personal.md`).

Beta/personal experiments:
* Playlist immersion profile toast and onboarding prompts (`019a2364`).
* Playlist-specific visualization overrides and mixed presentation modes (`b7146275`).
* Ongoing performance profiling/tuning for SD-heavy views (grid, metadata, boxart decode path).


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
