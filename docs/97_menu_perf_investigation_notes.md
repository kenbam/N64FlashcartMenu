## Menu Performance Investigation Notes

This note captures the recent menu performance investigation so it can be resumed later without repeating the same dead ends.

### Scope

The original symptom set was:
- BGM hitching during menu use
- audible skips when opening the ROM details page
- periodic visual frame glitches / dropped frames in the browser and other menu screens
- later, input drops and general menu instability after more aggressive experiments

The goal was to improve performance without regressing normal usability.

### What Actually Helped

#### 1. Larger buffered audio output path

The one change that clearly helped the audio symptom was moving away from the most fragile just-in-time path and giving audio more cushion:
- larger decoded MP3 PCM ring in `mp3_player.c`
- larger mixed output buffering in `sound.c`
- letting AI drain buffered PCM instead of relying as directly on one-shot frame timing

This reduced audible BGM hitching significantly.

#### 2. Moderate deferred ROM details loading

A moderate staged ROM-details open path felt better than fully synchronous init:
- move ROM info load, metadata image scan, boxart init, and details cache/layout build out of `view_load_rom_init`
- stage them across frames in `view_load_rom_display`
- keep the normal page chrome visible
- show a simple `Loading details...` placeholder in the details pane until ready

This was the only ROM-details-specific idea that felt at least somewhat positive.

### What Did Not Help Enough

#### 1. Aggressive ROM-details UI trimming

A more aggressive version that replaced most of the screen with a stripped loader during warmup was not liked. It made the page feel worse, even if it reduced some synchronous work.

Conclusion:
- if deferred details loading is revisited, use the moderate version only

#### 2. File-list caching and other browser micro-optimizations

The browser view was instrumented and showed:
- steady list draw cost around the low-to-mid 20 ms range
- small chrome/action-bar cost
- major additional cost from BGM processing

Caching some file-list layout work did not move the needle enough to justify the churn.

Conclusion:
- browser list rendering is expensive, but the attempted caching approach was not a clear win

#### 3. ROM-details micro-optimizations after the moderate deferred version

Several extra tweaks were tried:
- split stats/manual probe and full paragraph layout into separate warmup stages
- front-boxart probe first, full metadata image scan later
- throttled boxart retry
- `sound_poll()` between warmup stages

User feedback was that these were not meaningfully helpful.

Conclusion:
- keep or revisit only the moderate staged loading idea
- do not keep layering details-page micro-optimizations unless they show a clear, measurable gain

### What Clearly Made Things Worse

#### 1. Over-instrumentation

Several builds added heavy runtime logging:
- per-frame timing buckets in `menu_run`
- per-view timing buckets in browser/details
- CSV snapshot writes to SD

These builds were useful diagnostically but absolutely changed the runtime behavior and could themselves amplify stutter.

Conclusion:
- use instrumentation only briefly
- remove it before judging usability

#### 2. MP3 process throttling experiments

`mp3player_process()` was identified as a major contributor to frame spikes. Multiple experiments attempted to cap or budget MP3 decode work:
- smaller frame budgets per process call
- hard time caps inside decode
- lower process thresholds
- calling `mp3player_process()` only under certain reserve conditions

These experiments often made audio continuity much worse:
- constant stutter
- broken streaming
- degraded overall usability

Conclusion:
- do not keep the experimental MP3 budget/throttle changes
- the issue is real, but the attempted fixes were not acceptable

### Instrumentation Findings

These findings came from temporary CSV logging and were directionally useful, even though the logger itself introduced overhead.

#### 1. `menu_bgm_poll()` / `mp3player_process()` was a real hotspot

When logging normal browser/menu use:
- `bgm_process_ms` frequently dominated the frame
- normal frames often showed large MP3 processing bursts
- this aligned with the visual hitch pattern even after audible audio underruns were reduced

#### 2. Browser rendering also had a steady cost

Browser instrumentation showed:
- the file-list draw path was the biggest steady render cost in the browser
- action-bar / chrome drawing was comparatively small

So the browser problem was not only audio. It was:
- browser list rendering cost
- plus MP3 processing cost

#### 3. ROM-details worst spikes were not purely final draw

For the ROM-details screen:
- the biggest outliers were not fully explained by the final draw sections alone
- that suggests the heavy cost is in init / warmup / layout-building work, not just the final render pass

That supports the idea that the moderate staged open path is the correct shape if this is revisited.

### Practical Conclusions

#### What is worth remembering

- Bigger output/decode buffering helped audible BGM hitching.
- Moderate deferred ROM-details loading was the only details-page idea that felt worth keeping.
- `mp3player_process()` is a real hotspot.
- Browser list rendering is a real steady cost.

#### What is not worth repeating blindly

- aggressive loader-only ROM-details UI
- heavy runtime logging while judging feel
- MP3 decode/time-budget throttling experiments that starve playback
- incremental browser micro-optimizations without a very clear measured gain

### Recommended Future Directions

If this gets revisited later, the best next directions are probably:

1. Re-evaluate menu BGM format expectations.
- Try very simple menu BGM constraints first:
  - short loop
  - lower-complexity asset
  - possibly WAV64 or another simpler path if compatible with the rest of the menu
- Do not assume MP3 decode can be made cheap enough through tiny throttling knobs alone.

2. Revisit ROM-details open path only with the moderate staged version.
- Move expensive init work out of `view_load_rom_init`
- stage it across frames
- keep the standard page chrome
- avoid extra speculative micro-optimizations unless they show a clear gain

3. If browser render work is revisited, profile the list path more surgically.
- Focus on paragraph/layout generation and per-frame text work
- Avoid broad experiments that also disturb audio timing at the same time

4. Keep instrumentation short-lived and remove it before judging feel.
- Use it only to answer a narrow question
- then back it out immediately

### Current Recommendation

The practical recommendation after this investigation is:
- revert performance experiments that were not clearly beneficial
- keep the codebase near the last known-good usable state
- only re-open this work later with a narrower target and shorter experiment loop
