#!/usr/bin/env python3
"""Build a tiled beta manual package alongside the stable manual package.

Output layout:

    manual/tiled/
      manifest.ini
      preview/0001.png
      pages/0001/r000_c000.png
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


def ensure_tool(name: str) -> None:
    if shutil.which(name):
        return
    raise SystemExit(f"error: required tool is not installed: {name}")


def read_png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as handle:
        header = handle.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"error: invalid PNG file: {path}")
    width, height = struct.unpack(">II", header[16:24])
    return width, height


def render_pages(pdf_path: Path, out_dir: Path, scale_to: int) -> list[Path]:
    prefix = out_dir / "page"
    subprocess.run(
        [
            "pdftoppm",
            "-png",
            "-scale-to",
            str(scale_to),
            str(pdf_path),
            str(prefix),
        ],
        check=True,
    )

    rendered = sorted(out_dir.glob("page-*.png"))
    digits = max(4, len(str(len(rendered))))
    ordered: list[Path] = []
    for index, path in enumerate(rendered, start=1):
        target = out_dir / f"{index:0{digits}d}.png"
        path.rename(target)
        ordered.append(target)
    return ordered


def crop_tile(source: Path, target: Path, width: int, height: int, x: int, y: int) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-y",
            "-i",
            str(source),
            "-vf",
            f"crop={width}:{height}:{x}:{y}",
            str(target),
        ],
        check=True,
    )


def crop_tile_rgba16(source: Path, width: int, height: int, x: int, y: int) -> bytes:
    raw = subprocess.check_output(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(source),
            "-vf",
            f"crop={width}:{height}:{x}:{y}",
            "-frames:v",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgba",
            "-",
        ],
    )
    expected = width * height * 4
    if len(raw) != expected:
        raise SystemExit(f"error: unexpected raw tile size {len(raw)} != {expected} for {source}")

    out = bytearray(width * height * 2)
    j = 0
    for i in range(0, len(raw), 4):
        r, g, b, a = raw[i], raw[i + 1], raw[i + 2], raw[i + 3]
        value = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (1 if a >= 0x80 else 0)
        out[j] = (value >> 8) & 0xFF
        out[j + 1] = value & 0xFF
        j += 2
    return bytes(out)


def append_file_to_bundle(source: Path, bundle_handle) -> tuple[int, int]:
    offset = bundle_handle.tell()
    with source.open("rb") as handle:
        data = handle.read()
    bundle_handle.write(data)
    return offset, len(data)


def append_bytes_to_bundle(data: bytes, bundle_handle) -> tuple[int, int]:
    offset = bundle_handle.tell()
    bundle_handle.write(data)
    return offset, len(data)


def write_manifest(
    out_dir: Path,
    title: str,
    source_name: str,
    page_count: int,
    preview_dir: str,
    levels: list[dict[str, int | str]],
    max_zoom: int,
) -> None:
    first_level = levels[0]
    lines = [
        "[manual]",
        f"title={title}",
        f"source=PDF: {source_name}",
        f"page_count={page_count}",
        f"pages_dir={preview_dir}",
        f"zoom_dir={preview_dir}",
        f"max_zoom={max_zoom}",
        "start_page=1",
        "fit_mode=contain",
        "",
        "[tiled]",
        f"preview_dir={preview_dir}",
        f"tiles_dir={first_level['dir']}",
        f"page_width={first_level['page_width']}",
        f"page_height={first_level['page_height']}",
        f"tile_size={first_level['tile_size']}",
        f"cols={first_level['cols']}",
        f"rows={first_level['rows']}",
        f"level_count={len(levels)}",
        "",
    ]

    for index, level in enumerate(levels, start=1):
        lines.extend([
            f"[tiled_level_{index}]",
            f"dir={level['dir']}",
            f"bundle_file={level['bundle_file']}",
            f"bundle_index={level['bundle_index']}",
            f"bundle_format={level['bundle_format']}",
            f"page_width={level['page_width']}",
            f"page_height={level['page_height']}",
            f"tile_size={level['tile_size']}",
            f"cols={level['cols']}",
            f"rows={level['rows']}",
            f"zoom_trigger={level['zoom_trigger']}",
            "",
        ])

    with (out_dir / "manifest.ini").open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pdf", help="Input PDF path")
    parser.add_argument("out_dir", help="Output directory, usually metadata/.../manual/tiled")
    parser.add_argument("--title", default="", help="Manual title override")
    parser.add_argument("--preview-scale", type=int, default=960, help="Max preview dimension")
    parser.add_argument("--tile-page-scales", default="", help="Comma-separated tiled page scales, e.g. 1024,1536")
    parser.add_argument("--tile-page-scale", type=int, default=1536, help="Max tiled page dimension")
    parser.add_argument("--tile-size", type=int, default=256, help="Tile size in pixels")
    parser.add_argument("--max-zoom", type=int, default=4, help="Max zoom level for beta viewer")
    parser.add_argument("--bundle-format", choices=("png", "rgba16"), default="rgba16", help="Bundle payload format")
    args = parser.parse_args()

    ensure_tool("pdftoppm")
    ensure_tool("ffmpeg")

    pdf_path = Path(args.pdf)
    out_dir = Path(args.out_dir)
    preview_dir_name = "preview"
    tile_scales = [args.tile_page_scale]
    if args.tile_page_scales.strip():
        tile_scales = [int(part.strip()) for part in args.tile_page_scales.split(",") if part.strip()]
    tile_scales = sorted(dict.fromkeys(tile_scales))

    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="manual-tiled-beta-") as tmp_name:
        tmp_dir = Path(tmp_name)
        preview_render_dir = tmp_dir / "preview"
        preview_render_dir.mkdir(parents=True, exist_ok=True)

        preview_pages = render_pages(pdf_path, preview_render_dir, args.preview_scale)
        preview_out_dir = out_dir / preview_dir_name
        preview_out_dir.mkdir(parents=True, exist_ok=True)

        level_specs: list[dict[str, int | str]] = []
        rendered_level_pages: list[tuple[int, list[Path], Path]] = []
        for level_index, scale in enumerate(tile_scales, start=1):
            render_dir = tmp_dir / f"render_{level_index}"
            render_dir.mkdir(parents=True, exist_ok=True)
            rendered_pages = render_pages(pdf_path, render_dir, scale)
            if len(preview_pages) != len(rendered_pages):
                raise SystemExit("error: preview and tiled page counts differ")
            level_dir_name = "pages" if len(tile_scales) == 1 else f"levels/{level_index}"
            rendered_level_pages.append((level_index, rendered_pages, out_dir / level_dir_name))

        for index, preview_page in enumerate(preview_pages, start=1):
            page_name = f"{index:04d}.png"
            shutil.copy2(preview_page, preview_out_dir / page_name)

        for level_index, rendered_pages, tile_out_dir in rendered_level_pages:
            tile_out_dir.parent.mkdir(parents=True, exist_ok=True)
            level_width = 0
            level_height = 0
            level_cols = 0
            level_rows = 0
            bundle_name = f"levels/{level_index}.bundle.bin" if len(tile_scales) > 1 else "pages.bundle.bin"
            index_name = f"levels/{level_index}.index.tsv" if len(tile_scales) > 1 else "pages.index.tsv"
            bundle_path = out_dir / bundle_name
            index_path = out_dir / index_name
            tmp_tile = tmp_dir / f"tile_level_{level_index}.png"
            with bundle_path.open("wb") as bundle_handle, index_path.open("w", encoding="utf-8", newline="\n") as index_handle:
                for page_index, tiled_page in enumerate(rendered_pages, start=1):
                    current_width, current_height = read_png_size(tiled_page)
                    if level_width == 0:
                        level_width = current_width
                        level_height = current_height
                        level_cols = (current_width + args.tile_size - 1) // args.tile_size
                        level_rows = (current_height + args.tile_size - 1) // args.tile_size

                    cols = (current_width + args.tile_size - 1) // args.tile_size
                    rows = (current_height + args.tile_size - 1) // args.tile_size
                    for row in range(rows):
                        for col in range(cols):
                            x = col * args.tile_size
                            y = row * args.tile_size
                            width = min(args.tile_size, current_width - x)
                            height = min(args.tile_size, current_height - y)
                            if args.bundle_format == "rgba16":
                                tile_data = crop_tile_rgba16(tiled_page, width, height, x, y)
                                offset, size = append_bytes_to_bundle(tile_data, bundle_handle)
                            else:
                                crop_tile(
                                    tiled_page,
                                    tmp_tile,
                                    width,
                                    height,
                                    x,
                                    y,
                                )
                                offset, size = append_file_to_bundle(tmp_tile, bundle_handle)
                            index_handle.write(f"{page_index - 1}\t{row}\t{col}\t{offset}\t{size}\n")
            if tmp_tile.exists():
                tmp_tile.unlink()
            level_specs.append({
                "dir": tile_out_dir.relative_to(out_dir).as_posix(),
                "bundle_file": bundle_name,
                "bundle_index": index_name,
                "bundle_format": args.bundle_format,
                "page_width": level_width,
                "page_height": level_height,
                "tile_size": args.tile_size,
                "cols": level_cols,
                "rows": level_rows,
                "zoom_trigger": min(args.max_zoom, level_index + 1),
            })

        title = args.title.strip() or pdf_path.stem
        write_manifest(
            out_dir,
            title,
            pdf_path.name,
            len(preview_pages),
            preview_dir_name,
            level_specs,
            args.max_zoom,
        )

    print(f"built tiled beta manual package at {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
