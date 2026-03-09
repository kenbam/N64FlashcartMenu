#!/usr/bin/env python3
"""Build tiled beta manual packages in bulk from a matched-manual report."""

from __future__ import annotations

import argparse
import csv
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


def build_one(
    builder: Path,
    pdf_path: Path,
    out_dir: Path,
    title: str,
    preview_scale: int,
    tile_page_scales: str,
    tile_size: int,
    max_zoom: int,
    bundle_format: str,
    clean: bool,
) -> tuple[bool, str]:
    if clean and out_dir.exists():
        shutil.rmtree(out_dir)

    cmd = [
        sys.executable,
        str(builder),
        str(pdf_path),
        str(out_dir),
        "--title",
        title,
        "--preview-scale",
        str(preview_scale),
        "--tile-size",
        str(tile_size),
        "--max-zoom",
        str(max_zoom),
        "--bundle-format",
        bundle_format,
    ]
    if tile_page_scales.strip():
        cmd.extend(["--tile-page-scales", tile_page_scales])

    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return result.returncode == 0, result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matched-tsv", required=True, help="Path to reports/manuals/matched.tsv")
    parser.add_argument("--pdf-root", required=True, help="Directory containing the source PDF files")
    parser.add_argument("--preview-scale", type=int, default=960)
    parser.add_argument("--tile-page-scales", default="1024", help="Comma-separated scales, compact default is 1024")
    parser.add_argument("--tile-size", type=int, default=256)
    parser.add_argument("--max-zoom", type=int, default=4)
    parser.add_argument("--bundle-format", choices=("png", "rgba16"), default="rgba16")
    parser.add_argument("--clean", action="store_true", help="Remove existing tiled package before rebuilding")
    parser.add_argument("--only-missing", action="store_true", help="Skip manuals that already have tiled/manifest.ini")
    parser.add_argument("--jobs", type=int, default=1, help="Concurrent build jobs")
    parser.add_argument("--limit", type=int, default=0, help="Optional limit for testing")
    parser.add_argument("--report", default="reports/manuals/tiled_beta.tsv", help="Output TSV report")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    builder = repo_root / "tools" / "sc64" / "manuals_build_tiled_beta.py"
    matched_tsv = (repo_root / args.matched_tsv).resolve() if not Path(args.matched_tsv).is_absolute() else Path(args.matched_tsv)
    pdf_root = Path(args.pdf_root).resolve()
    report_path = (repo_root / args.report).resolve() if not Path(args.report).is_absolute() else Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)

    processed = 0
    built = 0
    skipped = 0
    failed = 0

    jobs: list[dict[str, object]] = []
    with matched_tsv.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for row in reader:
            if args.limit and len(jobs) >= args.limit:
                break

            pdf_path = pdf_root / row["pdf"]
            out_dir = (repo_root / row["target"] / "tiled").resolve()
            manifest_path = out_dir / "manifest.ini"

            if not pdf_path.exists():
                jobs.append({"row": row, "status": "missing-pdf", "detail": str(pdf_path)})
                continue

            if args.only_missing and manifest_path.exists():
                jobs.append({"row": row, "status": "skipped-existing", "detail": str(manifest_path)})
                continue

            jobs.append({
                "row": row,
                "pdf_path": pdf_path,
                "out_dir": out_dir,
            })

    with report_path.open("w", newline="", encoding="utf-8") as report_handle:
        writer = csv.writer(report_handle, delimiter="\t")
        writer.writerow(["pdf", "target", "status", "detail"])

        futures = {}
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
            for job in jobs:
                row = job["row"]
                processed += 1
                status = job.get("status")
                if status:
                    detail = str(job["detail"])
                    if status == "missing-pdf":
                        failed += 1
                    else:
                        skipped += 1
                    writer.writerow([row["pdf"], row["target"], status, detail])
                    continue

                future = executor.submit(
                    build_one,
                    builder=builder,
                    pdf_path=job["pdf_path"],
                    out_dir=job["out_dir"],
                    title=row["metadata_name"],
                    preview_scale=args.preview_scale,
                    tile_page_scales=args.tile_page_scales,
                    tile_size=args.tile_size,
                    max_zoom=args.max_zoom,
                    bundle_format=args.bundle_format,
                    clean=args.clean,
                )
                futures[future] = row

            completed = 0
            for future in as_completed(futures):
                row = futures[future]
                completed += 1
                ok, detail = future.result()
                if ok:
                    built += 1
                    writer.writerow([row["pdf"], row["target"], "built", detail])
                    print(f"[{completed}] built {row['target']}", flush=True)
                else:
                    failed += 1
                    writer.writerow([row["pdf"], row["target"], "failed", detail])
                    print(f"[{completed}] failed {row['target']}", file=sys.stderr, flush=True)

    print(f"processed={processed} built={built} skipped={skipped} failed={failed}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
