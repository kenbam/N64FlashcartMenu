## Manual Scan Viewer Plan

### Goal
Show full manual scans directly in the menu as browsable page images, with the emphasis on artifact quality and page-turning feel rather than ebook-style text rendering.

The key design choice is:
- `EPUB` is only an ingest format.
- The N64 should not render EPUB or PDF directly.
- The menu should read a prebuilt, menu-native manual package.

That keeps the console-side feature small and predictable while still letting us harvest manuals from EPUB, CBZ, PDF, or loose scans on a PC.

### Why This Fits The Current Menu
Current viewer support is already close to a minimal v1:
- `src/menu/views/text_viewer.c` handles small text sidecars and can present context/articles.
- `src/menu/views/image_viewer.c` already displays a decoded image fullscreen.
- `src/menu/views/browser.c` already routes special file types into dedicated viewers.
- `src/menu/rom_info.c` already resolves ROM-specific metadata directories and `description.txt` sidecars.

The missing piece is not file discovery. It is a dedicated manual package and a page-oriented image viewer.

### Runtime Format: `manual/`
For each game metadata directory, add an optional `manual/` folder.

Example:
`menu/metadata/N/C/T/E/manual/`

Suggested contents:
- `manifest.ini`
- `cover.png`
- `pages/0001.png`
- `pages/0002.png`
- `pages/0003.png`
- `thumbs/0001.png`
- `thumbs/0002.png`

Manifest fields:
- `title=` display title
- `source=` origin note (`Archive.org EPUB`, `scan`, `PDF`, etc.)
- `page_count=` total number of pages
- `has_cover=` `0/1`
- `page_width=` native page width after conversion
- `page_height=` native page height after conversion
- `pages_dir=` defaults to `pages`
- `zoom_dir=` defaults to `zoom` when present
- `max_zoom=` defaults to `3`
- `fit_mode=` `contain` for v1
- `zoom_levels=` optional, for v2 tiled zoom

KISS rule:
- v1 uses pre-rendered page PNGs only.
- v2 can add tiles or alternate resolutions, but the package structure should not need to change dramatically.

### Beta Format: `manual/tiled/`
The stable path stays:
- `manual/manifest.ini`
- `manual/pages/*.png`

The beta path is separate so it cannot break the stable viewer:
- `manual/tiled/manifest.ini`
- `manual/tiled/preview/0001.png`
- `manual/tiled/pages/0001/r000_c000.png`

Current beta viewer behavior:
- ROM options expose `View Manual (Tiled Beta)`
- zoom level `1x` uses `preview/`
- zoomed views use `pages/<page>/rXXX_cXXX.png` tiles
- tile size is currently expected to stay well below the RDP `1024` coordinate limit
- newer beta packages can expose multiple tile levels, for example:
  - `manual/tiled/levels/1/...`
  - `manual/tiled/levels/2/...`
- the viewer picks a tile level based on zoom, so higher zoom can switch to a denser tile set instead of stretching one level
- newer beta packages can also pack each level into:
  - `manual/tiled/levels/1.bundle.bin`
  - `manual/tiled/levels/1.index.tsv`
- this reduces filesystem overhead versus thousands of loose tile PNG files
- current beta packages can store bundle payloads as native `RGBA16` tiles instead of PNG chunks
- that removes per-tile PNG decode from the zoom path and is the preferred beta format

### Ingest Pipeline
Create an offline converter on PC that normalizes source manuals into the `manual/` package.

Accepted inputs:
- `EPUB`
- `PDF`
- `CBZ`
- loose images

Useful tooling in this repo:
- `tools/sc64/manuals_extract_epub_images.py` for image-based EPUBs
- `tools/sc64/manuals_extract_pdf_pages.py` for PDF rasterization via `pdftoppm` or `mutool`
- `tools/sc64/manuals_build_tiled_beta_batch.py` for bulk building `manual/tiled/` packages from `reports/manuals/matched.tsv`

Pipeline:
1. unpack source
2. detect whether pages are image-based or text-heavy
3. extract or rasterize page images in source order
4. normalize orientation/crop/margins
5. render display pages for the menu
6. optionally generate higher-resolution zoom pages in `zoom/`
7. optionally generate thumbnails
8. write `manifest.ini`

Important constraint:
The converter should preserve page images as the source of truth. OCR/reflow is out of scope for this feature. Even for EPUB, the only worthwhile cases are the ones that can be reduced to ordered page images.

### EPUB Strategy
EPUB is useful only when it acts like an image container.

Why:
- some archive/manual EPUBs are just ordered XHTML wrappers around page images
- EPUB gives a stable reading order through the spine
- extracting images from EPUB is simpler than implementing a renderer on the N64

Preferred behavior for EPUB ingest:
- follow the spine in reading order
- extract referenced raster assets in that order
- reject EPUBs that are primarily text or CSS layout instead of page images

For this feature, EPUB is not the delivery format. It is only accepted when it can be reduced to ordered page images.

### Viewer Design
Add a dedicated manual viewer rather than stretching the current one-shot image viewer.

Suggested controls:
- `A`: open manual / advance page when in quick-read mode
- `B`: back out
- `L/R`: previous/next page
- `C-left/C-right`: jump 5 pages
- `C-up`: manual overview / thumbnails later
- `Start`: toggle UI chrome
- `Z`: zoom mode later
- stick / D-pad: pan in zoom mode later

V1 display behavior:
- load one page at a time
- fit page to screen with black or theme-aware bars
- show page number overlay briefly on page turn
- remember last page per manual

V2 display behavior:
- add pan/zoom only after paging is fast and stable
- use precomputed tile pyramids or alternate page resolutions
- require explicit memory budgeting and cache eviction

### Recommended Implementation Phases
#### Phase 1: Manual presence + launch
- detect `manual/manifest.ini` under ROM metadata directory
- add `Open Manual` action in ROM details/context menu when present

#### Phase 2: Paged scan viewer
- new `MENU_MODE_MANUAL_VIEWER`
- next/prev page
- last-page persistence
- one decoded page in memory
- optional prefetch of next page if cheap

#### Phase 3: Thumbnails and overview
- generate `thumbs/*.png`
- grid of pages for quick jump
- cover page on entry

#### Phase 4: Zoom
- only if needed after hardware testing
- switch from single-page PNGs to multi-resolution page assets or tiles
- likely Expansion Pak recommended if zoom is expected to feel good

### Metadata Integration
Manuals should live with ROM metadata so the feature stays deterministic.

Good fit:
- `menu/metadata/<gamecode>/manual/manifest.ini`
- same region-agnostic fallback behavior as existing metadata

That gives us:
- one manual package per game identity
- compatibility with the current metadata resolution path in `src/menu/rom_info.c`
- a natural place for cover art, source notes, and precomputed page assets

### What Not To Do
Avoid these for v1:
- direct PDF parsing on-console
- direct EPUB parsing on-console
- CSS/layout engine work
- OCR or text extraction
- loading an entire manual into RAM
- folder-level heuristics like "pick any sibling scan set"

### First Prototype Recommendation
Prototype only one manual end-to-end:
- choose a clean image-based manual source
- convert to `manual/manifest.ini + pages/*.png`
- add a temporary `Open Manual` path for one known game
- get page turn latency, memory use, and visual quality on hardware

Success criteria:
- page turn feels immediate enough
- pages are readable at fit-to-screen
- opening/closing the manual feels like a native menu feature

If fit-to-screen readability is poor, the next experiment should be zoom tiles, not richer document parsing.
