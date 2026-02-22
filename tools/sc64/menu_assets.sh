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

  tools/sc64/menu_assets.sh screensaver <input> <output.png>
    Convert logo image to <=180x96 PNG (aspect preserved, transparent pad).

  tools/sc64/menu_assets.sh music-mp3 <input> <output.mp3> [bitrate_kbps]
    Re-encode menu music MP3 (default bitrate: 48k).

  tools/sc64/menu_assets.sh music-wav64 <input-audio>
    Convert one file (WAV/MP3/etc) to WAV64 (ADPCM, 32 kHz) next to source.

  tools/sc64/menu_assets.sh music-wav64-batch <dir>
    Batch-convert audio files in a directory to WAV64 (non-recursive).

  tools/sc64/menu_assets.sh rewrite-m3u-wav64 <m3u-or-dir> [more...]
    Rewrite #SC64_BGM=...mp3 directives to .wav64 when the matching file exists.

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

main() {
  [[ $# -ge 1 ]] || { usage; exit 1; }
  local cmd="$1"; shift
  case "$cmd" in
    bg) [[ $# -eq 2 ]] || die "usage: $0 bg <input> <output.png>"; convert_bg "$1" "$2" ;;
    screensaver) [[ $# -eq 2 ]] || die "usage: $0 screensaver <input> <output.png>"; convert_screensaver "$1" "$2" ;;
    music-mp3) [[ $# -ge 2 && $# -le 3 ]] || die "usage: $0 music-mp3 <input> <output.mp3> [bitrate_kbps]"; convert_music_mp3 "$1" "$2" "${3:-48}" ;;
    music-wav64) [[ $# -eq 1 ]] || die "usage: $0 music-wav64 <input-audio>"; convert_music_wav64_one "$1" ;;
    music-wav64-batch) [[ $# -eq 1 ]] || die "usage: $0 music-wav64-batch <dir>"; convert_music_wav64_batch "$1" ;;
    rewrite-m3u-wav64) [[ $# -ge 1 ]] || die "usage: $0 rewrite-m3u-wav64 <m3u-or-dir> [more...]"; rewrite_m3u_wav64 "$@" ;;
    -h|--help|help) usage ;;
    *) die "unknown command: $cmd" ;;
  esac
}

main "$@"
