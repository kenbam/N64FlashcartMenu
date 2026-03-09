#!/usr/bin/env bash
set -euo pipefail

AUDIOCONV64_DEFAULT="/mnt/b/dev/N64Toolchain/bin/audioconv64"
AUDIOCONV64_BIN="${AUDIOCONV64_BIN:-$AUDIOCONV64_DEFAULT}"
FFMPEG_BIN="${FFMPEG_BIN:-ffmpeg}"

die() {
  echo "error: $*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

need_file() {
  [[ -f "$1" ]] || die "file not found: $1"
}

usage() {
  cat <<'EOF'
SC64 Menu Asset Helper

Usage:
  tools/sc64/menu_assets.sh bg <input> <output.png>
    Convert/crop image to 640x480 PNG for menu backgrounds.

  tools/sc64/menu_assets.sh bg-native <input> <output.nimg>
    Convert/crop image to 640x480 native RGBA16 background sidecar.

  tools/sc64/menu_assets.sh bg-native-batch <dir>
    Create `.nimg` sidecars beside background images in a directory.

  tools/sc64/menu_assets.sh screensaver <input> <output.png>
    Convert logo image to <=180x96 PNG (aspect preserved, transparent pad).

  tools/sc64/menu_assets.sh screensaver-native <input> <output.nimg>
    Convert logo image to <=180x96 native RGBA16 sidecar.

  tools/sc64/menu_assets.sh screensaver-native-batch <dir>
    Create `.nimg` sidecars beside screensaver logo images in a directory.

  tools/sc64/menu_assets.sh boxart-grid-thumb <input> <output.png>
    Convert box art image to <=116x76 PNG (aspect preserved, transparent pad) for playlist grid.

  tools/sc64/menu_assets.sh boxart-grid-thumb-batch <metadata-dir>
    Recursively create boxart_front.grid.png beside each boxart_front.png under metadata.

  tools/sc64/menu_assets.sh boxart-grid-bxat-batch <metadata-dir> <thumb-cache-dir> [storage-prefix]
    Prebuild native BXAT thumbnail cache blobs for grid art (defaults storage-prefix to 'sd:').

  tools/sc64/menu_assets.sh music-mp3 <input> <output.mp3> [bitrate_kbps]
    Re-encode menu music MP3 (default bitrate: 48k).

  tools/sc64/menu_assets.sh music-wav64 <input-audio>
    Convert one file (WAV/MP3/etc) to WAV64 (ADPCM, 32 kHz) next to source.

  tools/sc64/menu_assets.sh music-wav64-batch <dir>
    Batch-convert audio files in a directory to WAV64 (non-recursive).

  tools/sc64/menu_assets.sh rewrite-m3u-wav64 <m3u-or-dir> [more...]
    Rewrite #SC64_BGM=...mp3 directives to .wav64 when the matching file exists.

  tools/sc64/menu_assets.sh rewrite-m3u-bg-nimg <m3u-or-dir> [more...]
    Rewrite #SC64_BACKGROUND=...png/.jpg directives to direct .nimg paths.

Notes:
  - Set AUDIOCONV64_BIN to override audioconv64 location.
  - Set FFMPEG_BIN to override ffmpeg path.
EOF
}

ensure_audioconv64() {
  [[ -x "$AUDIOCONV64_BIN" ]] || die "audioconv64 not found/executable: $AUDIOCONV64_BIN"
}

convert_bg() {
  local input="$1"
  local output="$2"
  need_cmd "$FFMPEG_BIN"
  need_file "$input"
  mkdir -p "$(dirname "$output")"
  "$FFMPEG_BIN" -y -i "$input" \
    -vf "scale=640:480:force_original_aspect_ratio=increase,crop=640:480" \
    -frames:v 1 -pix_fmt rgba "$output"
}

convert_bg_native() {
  local input="$1"
  local output="$2"
  need_cmd "$FFMPEG_BIN"
  need_file "$input"
  mkdir -p "$(dirname "$output")"
  python3 - "$input" "$output" "$FFMPEG_BIN" <<'PY'
from pathlib import Path
import struct
import subprocess
import sys

MAGIC = 0x4E494D47  # NIMG
WIDTH = 640
HEIGHT = 480

def rgba8_to_rgba16_be(rgba: bytes) -> bytes:
    out = bytearray((len(rgba) // 4) * 2)
    j = 0
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
        pixel = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (1 if a >= 0x80 else 0)
        out[j] = (pixel >> 8) & 0xFF
        out[j + 1] = pixel & 0xFF
        j += 2
    return bytes(out)

src = Path(sys.argv[1])
dst = Path(sys.argv[2])
ffmpeg = sys.argv[3]

raw = subprocess.check_output([
    ffmpeg, "-v", "error", "-y", "-i", str(src),
    "-vf", f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=increase,crop={WIDTH}:{HEIGHT}",
    "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgba", "-"
])

expected = WIDTH * HEIGHT * 4
if len(raw) != expected:
    raise SystemExit(f"unexpected raw size {len(raw)} != {expected}")

payload = rgba8_to_rgba16_be(raw)
header = struct.pack(">IIII", MAGIC, WIDTH, HEIGHT, len(payload))
dst.write_bytes(header + payload)
PY
}

convert_bg_native_batch() {
  local dir="$1"
  [[ -d "$dir" ]] || die "directory not found: $dir"
  need_cmd "$FFMPEG_BIN"
  python3 - "$dir" "$0" <<'PY'
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1])
script = sys.argv[2]
count = 0
skipped = 0
for src in sorted(root.iterdir()):
    if not src.is_file():
        continue
    if src.suffix.lower() not in {".png", ".jpg", ".jpeg"}:
        continue
    out = Path(str(src) + ".nimg")
    try:
        subprocess.run([script, "bg-native", str(src), str(out)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        count += 1
    except subprocess.CalledProcessError:
        skipped += 1
        print(f"skip: {src}", file=sys.stderr)
print(f"processed {count} background sidecars ({skipped} skipped)")
PY
}

convert_screensaver() {
  local input="$1"
  local output="$2"
  need_cmd "$FFMPEG_BIN"
  need_file "$input"
  mkdir -p "$(dirname "$output")"
  "$FFMPEG_BIN" -y -i "$input" \
    -vf "scale=180:96:force_original_aspect_ratio=decrease,pad=180:96:(ow-iw)/2:(oh-ih)/2:color=0x00000000" \
    -frames:v 1 -pix_fmt rgba "$output"
}

convert_screensaver_native() {
  local input="$1"
  local output="$2"
  need_cmd "$FFMPEG_BIN"
  need_file "$input"
  mkdir -p "$(dirname "$output")"
  python3 - "$input" "$output" "$FFMPEG_BIN" <<'PY'
from pathlib import Path
import struct
import subprocess
import sys

MAGIC = 0x4E494D47  # NIMG
WIDTH = 180
HEIGHT = 96

def rgba8_to_rgba16_be(rgba: bytes) -> bytes:
    out = bytearray((len(rgba) // 4) * 2)
    j = 0
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
        pixel = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (1 if a >= 0x80 else 0)
        out[j] = (pixel >> 8) & 0xFF
        out[j + 1] = pixel & 0xFF
        j += 2
    return bytes(out)

src = Path(sys.argv[1])
dst = Path(sys.argv[2])
ffmpeg = sys.argv[3]

raw = subprocess.check_output([
    ffmpeg, "-v", "error", "-y", "-i", str(src),
    "-vf", f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=decrease,pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2:color=0x00000000",
    "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgba", "-"
])

expected = WIDTH * HEIGHT * 4
if len(raw) != expected:
    raise SystemExit(f"unexpected raw size {len(raw)} != {expected}")

payload = rgba8_to_rgba16_be(raw)
header = struct.pack(">IIII", MAGIC, WIDTH, HEIGHT, len(payload))
dst.write_bytes(header + payload)
PY
}

convert_screensaver_native_batch() {
  local dir="$1"
  [[ -d "$dir" ]] || die "directory not found: $dir"
  need_cmd "$FFMPEG_BIN"
  python3 - "$dir" "$0" <<'PY'
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1])
script = sys.argv[2]
count = 0
skipped = 0
for src in sorted(root.iterdir()):
    if not src.is_file():
        continue
    if src.suffix.lower() not in {".png", ".jpg", ".jpeg"}:
        continue
    out = Path(str(src) + ".nimg")
    try:
        subprocess.run([script, "screensaver-native", str(src), str(out)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        count += 1
    except subprocess.CalledProcessError:
        skipped += 1
        print(f"skip: {src}", file=sys.stderr)
print(f"processed {count} screensaver sidecars ({skipped} skipped)")
PY
}

convert_boxart_grid_thumb() {
  local input="$1"
  local output="$2"
  need_cmd "$FFMPEG_BIN"
  need_file "$input"
  mkdir -p "$(dirname "$output")"
  "$FFMPEG_BIN" -y -i "$input" \
    -vf "scale=116:76:force_original_aspect_ratio=decrease,pad=116:76:(ow-iw)/2:(oh-ih)/2:color=0x00000000" \
    -frames:v 1 -pix_fmt rgba "$output"
}

convert_boxart_grid_thumb_batch() {
  local dir="$1"
  [[ -d "$dir" ]] || die "directory not found: $dir"
  need_cmd "$FFMPEG_BIN"
  python3 - "$dir" "$FFMPEG_BIN" <<'PY'
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1])
ffmpeg = sys.argv[2]
count = 0
fail = 0

for src in root.rglob("boxart_front.png"):
    dst = src.with_name("boxart_front.grid.png")
    dst.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg, "-y", "-i", str(src),
        "-vf", "scale=116:76:force_original_aspect_ratio=decrease,pad=116:76:(ow-iw)/2:(oh-ih)/2:color=0x00000000",
        "-frames:v", "1", "-pix_fmt", "rgba", str(dst),
    ]
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        count += 1
    except subprocess.CalledProcessError:
        fail += 1
        print(f"skip (ffmpeg failed): {src}", file=sys.stderr)
        continue
    if count % 200 == 0:
        print(f"processed {count}...")

print(f"processed {count} boxart grid thumbnails ({fail} skipped)")
PY
}

convert_boxart_grid_bxat_batch() {
  local metadata_dir="$1"
  local thumb_cache_dir="$2"
  local storage_prefix="${3:-sd:}"
  [[ -d "$metadata_dir" ]] || die "metadata directory not found: $metadata_dir"
  mkdir -p "$thumb_cache_dir"
  need_cmd "$FFMPEG_BIN"
  python3 - "$metadata_dir" "$thumb_cache_dir" "$storage_prefix" "$FFMPEG_BIN" <<'PY'
from pathlib import Path
import hashlib
import struct
import subprocess
import sys

metadata_root = Path(sys.argv[1]).resolve()
cache_root = Path(sys.argv[2]).resolve()
storage_prefix = sys.argv[3]
ffmpeg = sys.argv[4]

BOX_W = 116
BOX_H = 76
MAGIC = 0x42584154  # BXAT

def fnv1a64(data: bytes) -> int:
    h = 1469598103934665603
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h

def rgba8_to_rgba16_be(rgba: bytes) -> bytes:
    out = bytearray(len(rgba) // 2)
    j = 0
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i], rgba[i+1], rgba[i+2], rgba[i+3]
        v = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (1 if a >= 0x80 else 0)
        out[j] = (v >> 8) & 0xFF
        out[j+1] = v & 0xFF
        j += 2
    return bytes(out)

processed = 0
skipped = 0
for front in metadata_root.rglob("boxart_front.png"):
    grid = front.with_name("boxart_front.grid.png")
    src = grid if grid.exists() else front
    rel = src.relative_to(metadata_root.parent)  # "metadata/..."
    key = f"{storage_prefix}/menu/{rel.as_posix()}" if rel.parts[0] != "menu" else f"{storage_prefix}/{rel.as_posix()}"
    h = fnv1a64(key.encode("utf-8"))
    out_path = cache_root / f"bx_{h:016x}.cache"
    if out_path.exists():
        processed += 1
        continue
    try:
        raw = subprocess.check_output([
            ffmpeg, "-v", "error", "-i", str(src),
            "-vf", f"scale={BOX_W}:{BOX_H}:force_original_aspect_ratio=decrease,pad={BOX_W}:{BOX_H}:(ow-iw)/2:(oh-ih)/2:color=0x00000000",
            "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgba", "-"
        ])
    except subprocess.CalledProcessError:
        skipped += 1
        print(f"skip (ffmpeg failed): {src}", file=sys.stderr)
        continue
    expected = BOX_W * BOX_H * 4
    if len(raw) != expected:
        skipped += 1
        print(f"skip (unexpected raw size {len(raw)} != {expected}): {src}", file=sys.stderr)
        continue
    px = rgba8_to_rgba16_be(raw)
    size = len(px)
    header = struct.pack(">IIII", MAGIC, BOX_W, BOX_H, size)
    out_path.write_bytes(header + px)
    processed += 1
    if processed % 200 == 0:
        print(f"processed {processed}...")

print(f"processed {processed} bxat thumbnails ({skipped} skipped)")
PY
}

convert_music_mp3() {
  local input="$1"
  local output="$2"
  local bitrate="${3:-48}"
  need_cmd "$FFMPEG_BIN"
  need_file "$input"
  mkdir -p "$(dirname "$output")"
  "$FFMPEG_BIN" -y -i "$input" -map_metadata -1 -vn \
    -ar 32000 -ac 2 -b:a "${bitrate}k" -codec:a libmp3lame "$output"
}

convert_music_wav64_one() {
  local input="$1"
  need_file "$input"
  ensure_audioconv64
  local dir base out
  dir="$(dirname "$input")"
  base="$(basename "$input")"
  out="$(mktemp -d)"
  "$AUDIOCONV64_BIN" --wav-resample 32000 --wav-compress 1 -o "$out" "$input"
  rsync -r "$out"/ "$dir"/
  rm -rf "$out"
}

convert_music_wav64_batch() {
  local dir="$1"
  [[ -d "$dir" ]] || die "directory not found: $dir"
  ensure_audioconv64
  local out
  out="$(mktemp -d)"
  "$AUDIOCONV64_BIN" --wav-resample 32000 --wav-compress 1 -o "$out" "$dir"
  rsync -r "$out"/ "$dir"/
  rm -rf "$out"
}

rewrite_m3u_wav64() {
  python3 - "$@" <<'PY'
from pathlib import Path
import sys

def iter_m3us(paths):
    for raw in paths:
        p = Path(raw)
        if p.is_dir():
            yield from p.rglob("*.m3u")
            yield from p.rglob("*.m3u8")
        elif p.is_file():
            yield p

def exists_for(path_str, base_dir):
    p = Path(path_str)
    if p.suffix.lower() != ".mp3":
        return None
    cand = p.with_suffix(".wav64")
    if str(cand).startswith("/") or ":/" in str(cand):
        # Flashcart paths are checked by path string only; leave rewrite optimistic.
        return cand
    if (base_dir / cand).exists():
        return cand
    return None

updated = 0
for m3u in iter_m3us(sys.argv[1:]):
    try:
        text = m3u.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        continue
    changed = False
    out = []
    for line in text.splitlines():
        if line.startswith("#SC64_BGM=") or line.startswith("#SC64_MUSIC="):
            key, val = line.split("=", 1)
            cand = exists_for(val.strip(), m3u.parent)
            if cand is not None:
                line = f"{key}={str(cand).replace(chr(92), '/')}"
                changed = True
        out.append(line)
    if changed:
        m3u.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
        print(m3u)
        updated += 1
print(f"Updated {updated} playlist(s)", file=sys.stderr)
PY
}

rewrite_m3u_bg_nimg() {
  python3 - "$@" <<'PY'
from pathlib import Path
import sys

def iter_m3us(paths):
    for raw in paths:
        p = Path(raw)
        if p.is_dir():
            yield from p.rglob("*.m3u")
            yield from p.rglob("*.m3u8")
        elif p.is_file():
            yield p

updated = 0
for m3u in iter_m3us(sys.argv[1:]):
    try:
        text = m3u.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        continue
    changed = False
    out = []
    for line in text.splitlines():
        if line.startswith("#SC64_BACKGROUND="):
            key, val = line.split("=", 1)
            value = val.strip()
            lower = value.lower()
            if not lower.endswith(".nimg") and (lower.endswith(".png") or lower.endswith(".jpg") or lower.endswith(".jpeg")):
                line = f"{key}={value}.nimg"
                changed = True
        out.append(line)
    if changed:
        m3u.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
        print(m3u)
        updated += 1
print(f"Updated {updated} playlist(s)", file=sys.stderr)
PY
}

main() {
  [[ $# -ge 1 ]] || { usage; exit 1; }
  local cmd="$1"; shift
  case "$cmd" in
    bg) [[ $# -eq 2 ]] || die "usage: $0 bg <input> <output.png>"; convert_bg "$1" "$2" ;;
    bg-native) [[ $# -eq 2 ]] || die "usage: $0 bg-native <input> <output.nimg>"; convert_bg_native "$1" "$2" ;;
    bg-native-batch) [[ $# -eq 1 ]] || die "usage: $0 bg-native-batch <dir>"; convert_bg_native_batch "$1" ;;
    screensaver) [[ $# -eq 2 ]] || die "usage: $0 screensaver <input> <output.png>"; convert_screensaver "$1" "$2" ;;
    screensaver-native) [[ $# -eq 2 ]] || die "usage: $0 screensaver-native <input> <output.nimg>"; convert_screensaver_native "$1" "$2" ;;
    screensaver-native-batch) [[ $# -eq 1 ]] || die "usage: $0 screensaver-native-batch <dir>"; convert_screensaver_native_batch "$1" ;;
    boxart-grid-thumb) [[ $# -eq 2 ]] || die "usage: $0 boxart-grid-thumb <input> <output.png>"; convert_boxart_grid_thumb "$1" "$2" ;;
    boxart-grid-thumb-batch) [[ $# -eq 1 ]] || die "usage: $0 boxart-grid-thumb-batch <metadata-dir>"; convert_boxart_grid_thumb_batch "$1" ;;
    boxart-grid-bxat-batch) [[ $# -ge 2 && $# -le 3 ]] || die "usage: $0 boxart-grid-bxat-batch <metadata-dir> <thumb-cache-dir> [storage-prefix]"; convert_boxart_grid_bxat_batch "$1" "$2" "${3:-sd:}" ;;
    music-mp3) [[ $# -ge 2 && $# -le 3 ]] || die "usage: $0 music-mp3 <input> <output.mp3> [bitrate_kbps]"; convert_music_mp3 "$1" "$2" "${3:-48}" ;;
    music-wav64) [[ $# -eq 1 ]] || die "usage: $0 music-wav64 <input-audio>"; convert_music_wav64_one "$1" ;;
    music-wav64-batch) [[ $# -eq 1 ]] || die "usage: $0 music-wav64-batch <dir>"; convert_music_wav64_batch "$1" ;;
    rewrite-m3u-wav64) [[ $# -ge 1 ]] || die "usage: $0 rewrite-m3u-wav64 <m3u-or-dir> [more...]"; rewrite_m3u_wav64 "$@" ;;
    rewrite-m3u-bg-nimg) [[ $# -ge 1 ]] || die "usage: $0 rewrite-m3u-bg-nimg <m3u-or-dir> [more...]"; rewrite_m3u_bg_nimg "$@" ;;
    -h|--help|help) usage ;;
    *) die "unknown command: $cmd" ;;
  esac
}

main "$@"
