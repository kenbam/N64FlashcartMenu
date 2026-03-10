## Manual Viewer Notes

This file started as the design plan for manual support. Parts of that original plan are now implemented, while other sections remain historical ideas only. The notes below call out what is current versus what is still speculative.

### Current goal
Show full manual scans directly in the menu as browsable page images, with the emphasis on artifact quality and page-turning feel rather than ebook-style text rendering.

The key design choice is:
- `EPUB` is only an ingest format.
- The N64 should not render EPUB or PDF directly.
- The menu should read a prebuilt, menu-native manual package.

That keeps the console-side feature small and predictable while still letting us harvest manuals from EPUB, CBZ, PDF, or loose scans on a PC.

### Current status
This feature is implemented in the current customized build:
- ROM details expose `Open Manual` when a manual package is present.
- The viewer uses `manifest.ini` plus page assets under a per-game `manual/` directory.
- Page assets can use native `.png.nimg` replacements for faster loads.
- Optional higher-resolution zoom assets can live in a separate `zoom/` directory.

### Runtime format: `manual/`
For each game metadata directory, add an optional `manual/` folder.

Example:
`menu/metadata/N/C/T/E/manual/`

Suggested contents:
- `manifest.ini`
- `pages/0001.png`
- `pages/0002.png`
- `pages/0003.png`
- optional native replacements such as `pages/0001.png.nimg`
- optional zoom assets such as `zoom/0001.png` or `zoom/0001.png.nimg`

Manifest fields:
- `title=` display title
- `source=` origin note (`Archive.org EPUB`, `scan`, `PDF`, etc.)
- `page_count=` total number of pages
- `start_page=` optional 1-based default page
- `pages_dir=` defaults to `pages`
- `zoom_dir=` defaults to `zoom` when present
- `max_zoom=` defaults to `3`

Current runtime behavior:
- the viewer always opens the stable `manual/manifest.ini` path
- `pages/` is required
- `zoom/` is optional
- if a `.png.nimg` replacement exists for a page, the native image is used first

### Historical tiled-beta ideas
The older `manual/tiled/` and bundled-tile ideas in this document are not the current mainline UI. They remain here as future-facing notes only.

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
- `tools/sc64/manuals_batch_import.py` for matching PDFs to metadata and building page-based `manual/` packages
- `tools/sc64/manual_pages_native.py` for converting manual page PNGs to `.png.nimg`

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
- `C-left/C-right`: previous/next page
- `L/R`: zoom out/in
- `Start`: toggle UI chrome
- stick / D-pad: pan when zoomed

Current display behavior:
- loads one page at a time
- fits the page to screen
- supports optional higher-resolution zoom assets
- keeps page UI minimal and page-turn focused

V2 display behavior:
- add pan/zoom only after paging is fast and stable
- use precomputed tile pyramids or alternate page resolutions
- require explicit memory budgeting and cache eviction

### Recommended Implementation Phases
#### Completed baseline
- detect `manual/manifest.ini` under the ROM metadata directory
- `Open Manual` action in ROM details
- page-based scan viewer
- one decoded page in memory
- optional prefetch of the next page

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
