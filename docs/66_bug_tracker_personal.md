# Personal Bug Tracker (ben fork)

## Open

- `BGM CPU use and stutter during menu/playlist activity` (investigation; cause not yet proven)
  - Symptom: current background music can stutter under UI load. Software MP3 decoding competes for CPU, but synchronous playlist/SD operations can also delay audio buffer servicing.
  - Candidate: Libdragon's RSP-accelerated ULC codec in a `.wav64` container; **not** yet supported by this repo's pinned Libdragon commit.
  - Next step: baseline MP3/VADPCM behavior, trial ULC on a branch, instrument `mixer_try_play()` gaps, and fix independent blocking-I/O causes if necessary.
  - Investigation plan: [ULC BGM performance investigation](./72_ulc_bgm_performance_investigation.md).

- `WAV64 BGM wrapper crash` (temporary workaround applied)
  - Symptom: boot-time crash with `Read from invalid memory address` in `audio/wav64_vadpcm.c`, reported at `src/menu/menu.c:587` wrapper callsite.
  - Current status: mitigated by bypassing WAV64 meter wrapper and playing WAV64 directly.
  - Regression: WAV64 visualizer meter is currently disabled (falls back to non-reactive behavior for WAV64).
  - Next fix: rework WAV64 metering using a safer tap point (not the current waveform read wrapper path).

- `Playlist grid overlay leaking into non-grid mode`
  - Symptom: after grid feature work, an overlay appears in playlist non-grid (list) mode.
  - Current status: reported, not yet reproduced/fixed.
  - Next step: capture screenshot / exact screen and identify whether overlay is from grid footer, toast, or stale draw path.

