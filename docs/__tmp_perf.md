General:

  1. playlist_has_path() — O(n²) duplicate check (line 2799)

  playlist_append_rom_entry_unique() calls playlist_has_path() which does a linear scan of all existing entries for every smart playlist
   entry. With N entries, this is O(N²) total. For a large ROM collection (300+ games), this becomes significant on N64's ~93MHz CPU.

  Fix: Use a hash set for seen paths, or sort + binary search.

  2. Smart playlist: rom_config_load_ex() per file (line 2916)

  The smart_playlist_collect_dir() function reads ROM header/config for every ROM file found during directory traversal. On SD card,
  each file open is expensive (seek time). This is the dominant cost for smart playlists.

  Fix: Consider a persistent ROM metadata index/database that caches game_code, year, genre, etc. so smart queries don't need to re-read
   every ROM header.

  3. normalize_path() allocates 2 mallocs per entry (line 3763)

  Called for every entry in both parse and smart paths. It allocates a work buffer and a segments array every time.

  Fix: Use a single stack buffer or thread-local scratch buffer for normalization since paths have bounded length.

  4. playlist_append_rom_entry() — 2 strdup per entry (line 2836)

  Every entry does strdup(basename) + strdup(normalized_path). For 300 entries that's 600 small heap allocations.

  Fix: Use an arena/bump allocator for playlist entry strings, freed all at once when the browser list is freed.

  5. Memory cache stat() call on every access (line 300)

  playlist_mem_cache_try_load() calls stat() on the m3u file to check size/mtime even for a memory cache hit. On SD card, stat() can be
  costly.

  Fix: Skip the stat check for memory cache and trust the in-memory data (the cache is already invalidated on directory change). Or
  cache the stat result.

  6. playlist_cache_build_path() — allocates per invocation (line 670)

  Called for both disk cache read and write, builds a hash-based path string each time.

  Fix: Minor, but could use a stack buffer.


  Static M3U Playlist Loading — Root Cause Analysis

  The core issue: load_playlist() runs synchronously inside the view's show/event handler, which means the entire main loop is blocked.
  During that time, sound_poll() and menu_bgm_poll() don't get called, causing audio buffer underruns (stutters).

  What happens on a memory cache hit (best case):

  1. stat() on the .m3u file — SD card I/O (line 300)
  2. strcmp × 6 slots to find cache entry — trivial
  3. 5× strdup for metadata — trivial
  4. N × playlist_append_rom_entry() — each does strdup(basename) + strdup(path) + potential realloc — N×2 mallocs
  5. browser_apply_sort() — sorts entry array
  6. browser_apply_playlist_overrides() — calls file_exists() (= stat()) up to 3 more times for bgm, bg, screensaver_logo paths — 3 more
   SD card I/O operations

  So even with a warm memory cache, opening a 50-entry playlist with a theme/bgm/bg does: 1 stat + ~105 mallocs + 3 more stats. The
  stats are the killers — each SD card stat can take 5-20ms on typical flashcart SD interfaces.

  What happens on a disk cache hit (second visit, cold boot):

  All of the above, plus:
  - fopen + fread the binary cache file — more SD card I/O
  - playlist_cache_read_string() × (5 + N) — reads length-prefixed strings from disk
  - Then calls playlist_mem_cache_save() — allocates a full copy into memory cache (N more strdups)

  What happens on a full parse (first visit, no cache):

  All of the above, plus:
  - fopen + fgets line-by-line the .m3u file
  - normalize_path() per entry — 2 mallocs each
  - path_create/path_clone per entry
  - Then saves to both memory cache and disk cache — the disk cache fwrite is additional SD card I/O

  Why the cache doesn't help enough

  The cache saves re-parsing the text file, but it doesn't save:
  - The stat calls (1 for validation + up to 3 for override assets)
  - The N×2 strdups to rebuild the browser list
  - The browser_apply_sort
  - The override application

  For a small static playlist (3-50 entries), the m3u text file itself is tiny (< 2KB). Parsing it directly is probably faster than
  validating and loading the binary cache, because the cache path does: stat() + fopen(cache) + fread(header) + validation + N reads,
  whereas direct parse does: fopen(m3u) + fgets loop.

  Recommendations for static playlists

  P0: Eliminate redundant stat() calls in override application (line 3344-3350)

  browser_apply_playlist_overrides() calls file_exists() (which is stat()) on bgm, bg, and screensaver_logo paths. These could be
  deferred or checked during the deferred phase that already exists (browser_apply_playlist_overrides_deferred).

  P1: Skip disk cache for small playlists

  If the m3u file is small (say < 4KB or < 100 entries), skip the disk cache entirely. The raw parse of a few KB text file is fast; the
  overhead of cache validation + binary I/O + memory cache population is likely greater.

  P2: Avoid rebuilding the browser list if re-entering same playlist

  If menu->browser.directory already points to the same playlist and the list is populated, skip the entire load_playlist call. Just
  re-apply overrides.

  P3: Pre-allocate entry list capacity

  For the memory cache path, you know entry->entry_count upfront. Call browser_reserve_entry_capacity(menu, capacity,
  entry->entry_count) once instead of checking per-entry.

  P4: Use a single allocation for all entry strings

  Instead of N×2 strdups, compute total string length, malloc one block, and copy all strings into it. This reduces malloc pressure from
   ~100 calls to 1.

