# ULC audio codec: menu BGM CPU/stutter investigation

**Status:** Proposed experiment — not implemented or benchmarked in this fork.
**Priority:** High-value audio performance investigation; evaluate before replacing the existing MP3 or WAV64 paths.
**Added:** 2026-11-05

## Why investigate

Dominic Szablewski's November 3, 2026 article describes a Libdragon implementation of the **Ultra Low Complexity (ULC)** audio codec, with RSP-accelerated inverse MDCT. The reported performance for one **44.1 kHz stereo track** is roughly **4% of N64 CPU time and 5% of RSP time** (author's test, not a measurement of this menu). ULC targets near-Opus compression with substantially lower playback complexity.

This appears well suited to the menu's long-running background music, especially when navigation, box-art/grid rendering, visualizers, and SD-card work compete for a ~93 MHz CPU.

**Hypothesis:** ULC-encoded `.wav64` BGM can lower decode CPU use relative to the current software MP3 path while keeping SD bandwidth and file sizes reasonable. It *might* reduce music stutter caused by CPU pressure. It **will not by itself eliminate** glitches caused by main-loop stalls, blocked mixer servicing, or I/O contention.

## Current architecture in this fork (main branch, inspected 2026-11-05)

- `src/menu/menu_bgm.c`: BGM supports an MP3 backend and a WAV64 backend, including per-playlist/runtime overrides and fallback files at `/menu/music/menu.wav64`, `/menu/music/menu.mp3`, `/menu/music/bgm.wav64`, and `/menu/music/bgm.mp3`. WAV64 already uses `wav64_open`, `wav64_set_loop`, and the Libdragon mixer. ULC WAV64 *should fit the same backend*; a new `.ulc` extension, music player, or backend enum is not required.
- `src/menu/mp3_player.c`: MP3 decoding uses `minimp3` (`mp3dec_decode_frame`) from the waveform read callback, i.e. decoding takes main-CPU time while mixing. The standalone MP3 player is a separate feature and need not be changed by a BGM experiment.
- `src/menu/sound.c`: audio runs at 44.1 kHz by default, with 8 buffers and 16 mixer channels. `sound_poll()` calls `mixer_try_play()`. `sound_reconfigure()` currently registers `wav64_init_compression(1)` (VADPCM), **not compression level 2 (ULC)**. Preserve level 1 for existing sound effects and WAV64 assets when adding ULC.
- `src/menu/menu.c`: `menu_bgm_poll()`, `sound_poll()`, then `png_decoder_poll()` are serviced in the main menu loop, including a separate screensaver path. Long blocking operations can delay all three.
- `docs/__tmp_perf.md`: static playlist load/override work is synchronous; repeated SD `stat()`/file operations and allocations can delay `sound_poll()` and cause underruns even with a cheap codec. Some BGM is intentionally stopped while PNG decoding, manual viewing, booting, or using the standalone music player (`menu_bgm_poll`).
- `docs/66_bug_tracker_personal.md`: documents an earlier WAV64 metering-wrapper crash. **Check actual current behavior before using it for ULC:** `menu_bgm.c` currently constructs a metering wrapper and passes `&menu_bgm_wav64_wrap.wave` to `mixer_ch_play`, despite the bug tracker describing a bypass workaround. Do not assume the WAV64/ULC visualizer tap is safe.
- `.gitmodules` follows Libdragon `preview`, but the submodule is pinned to commit `401831ca7900ad72623d006db74ebca6f34287fa`. The pinned commit's `src/audio/` **does not contain `wav64_ulc.c` or `rsp_ulc.S`**; the current preview branch contains both. The fork therefore cannot use this codec without updating Libdragon and associated build tools.

## Integration spike (separate branch; keep legacy playback intact)

1. **Baseline first.** On real N64 + target flashcart/SD card, compare MP3 BGM on/off, current VADPCM WAV64 BGM on/off, and different UI loads. Use the *same source music* and equivalent output levels. Record frame times, audio servicing gaps, audio glitches, SD stalls, and any CPU/RSP profiling possible.
2. **Update dependency deliberately.** On a feature branch, move the Libdragon submodule and SDK/toolchain/host `audioconv64` together to compatible preview revisions. Review the Libdragon API/build delta; do not silently replace the pinned SDK on `main`. Check emulator and hardware boot/build regressions.
3. **Enable the decoder.** Libdragon's ULC codec is **WAV64 compression level 2**: add `wav64_init_compression(2)` in the audio initialization/reinitialization path, *alongside* `wav64_init_compression(1)`. The Libdragon wiki estimates ~16 KiB extra decoder ROM footprint. Ensure initialization occurs before `wav64_open()`.
4. **Convert test tracks.** Produce 44.1 kHz stereo WAV masters, then ULC-compressed `.wav64` with a matching recent `audioconv64`. Example:
   ```sh
   $N64_INST/bin/audioconv64 --wav-compress ulc,mode=abr,bitrate=96 music.wav
   ```
   Compare roughly 64, 96, and 128 kbps (or VBR quality settings), audio quality, file size, and playback robustness. The wiki warns of possible faint fixed-point noise and recommends avoiding unnecessary resampling. Do *not* overwrite an existing BGM file until a backup/fallback is available.
5. **Reuse WAV64 BGM route.** A ULC `.wav64` should be loadable with the existing playlist override/file resolution, looping and mixer handling. Prefer initially bypassing the custom WAV64 meter wrapper or explicitly verifying/fixing it with ULC before turning visualizers on. Preserve fallback to known-good MP3/VADPCM.
6. **Profile in difficult scenes.** Test idle file list, scrolling, 50+ entry playlists and playlist switching with overrides, box-art grid and cache misses, PNG/background transitions, 3D screensavers, visualizer on/off, SFX while music plays, and 30+ minute continuous playback. Measure both CPU impact **and** RSP contention from graphics/other overlays. Check start/stop/reload/loop and playlist transitions.
7. **Separate causes.** If ULC improves CPU time but stutter persists around playlist or SD operations, instrument long gaps between `mixer_try_play()` calls and fix synchronous work: reduce redundant `stat()`, defer/prefetch assets, give audio a stricter service deadline, and move/block-budget expensive operations. Do not conclude the codec 'failed' merely because a multi-frame blocking operation still underruns the mixer.
8. **Decide using data.** If stable, retain MP3 for the standalone player and backward-compatible BGM; prefer ULC for supplied/converted BGM or optionally auto-select formats. Document SD conversion steps, quality defaults, and any user-visible limitations.

## What success means

- **No audible BGM skips** during normal browsing, grid/visualizer/screen saver use, playlist switches, and long sessions on actual hardware.
- Lower measured decode CPU load than software MP3 with the same track, without unacceptable additional RSP/frame-time cost or glitches.
- Verified loop/restart/reload, reliable SFX mixing, and no WAV64 meter crash; visualizers either work safely or degrade gracefully.
- Compression significantly smaller than VADPCM for similar perceived quality, with a reasonable quality option for 44.1 kHz stereo.
- Any remaining SD or UI-blocking stutter has its own measured root cause and follow-up work item.

**Explicit non-goal for first spike:** replacing the standalone MP3 player or rewriting the mixer/decoder from scratch. The valuable first test is Libdragon's built-in ULC WAV64 path.

## References

- PhobosLab, *Why this Obscure Audio Codec is a Perfect Fit for the N64* (2026-11-03): https://phoboslab.org/log/2026/11/n64-ulc-codec
- Libdragon ULC wiki (encoding, `wav64_init_compression(2)`, streaming/looping): https://github.com/DragonMinded/libdragon/wiki/ULC-Audio-Codec
- Current Libdragon preview decoder: https://github.com/DragonMinded/libdragon/blob/preview/src/audio/wav64_ulc.c
- ULC N64 demo: https://github.com/phoboslab/ulc64
- Prior playlist/audio service analysis: [`docs/__tmp_perf.md`](./__tmp_perf.md)
- Existing WAV64 regression note: [`docs/66_bug_tracker_personal.md`](./66_bug_tracker_personal.md)
