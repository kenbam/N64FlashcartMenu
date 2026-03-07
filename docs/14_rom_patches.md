[Return to the index](./00_index.md)
## ROM Patches (Hacks, Fan Translations, etc.)

`main` now includes an experimental, opt-in patch pipeline for N64 ROM launch.

Current scope (MVP):
- Only applies when per-ROM `Use Patches = On`.
- Manifest-based lookup under `menu/patches/...`.
- IPS patches can be applied at launch.
- XDELTA manifests are supported via `prepatched_file` (pre-generated ROM artifact).
- Strict compatibility checks via `expected_check_code` (recommended).
- Non-destructive: output is cached to `menu/cache/patched/` and original ROM remains untouched.

Directory layout:
- `menu/patches/<category>/<id0>/<id1>/<region>/default.ini`
- Optional fallback: `menu/patches/<category>/<id0>/<id1>/default.ini`
- You can store multiple manifests (`*.ini`) in either directory.
- Patch file is resolved relative to the manifest directory.

Manifest example:
```ini
[patch]
name = GoldenEye - Widescreen
type = ips
file = goldeneye_widescreen.ips
; optional stack (applied in order):
; file_1 = goldeneye_widescreen.ips
; file_2 = goldeneye_fps_unlock.ips

[compatibility]
expected_check_code = 0x0123456789ABCDEF
expected_game_code = NGEE
expected_rom_size = 33554432
```

Notes:
- `type` can be `ips` or `xdelta`.
- You can define either `file = ...` or a stack using `file_1`, `file_2`, etc.
- For `xdelta`, `prepatched_file` must point to an already-built patched ROM.
- For safety, byte-swapped/little-endian ROM files are rejected by this MVP path.
- Example files are in `examples/patches/goldeneye/`.
- A simple smoke-test patch (`unlock_all_levels.ips`) is included in `examples/patches/goldeneye/`.
- Profile selection order:
  - Selected ROM `patch_profile` from ROM `.ini` (if set),
  - `default.ini`,
  - first alphabetical `*.ini` in patch directory.
For `xdelta` manifests:
```ini
[patch]
name = Example Xdelta Patch
type = xdelta
file = mod.xdelta
prepatched_file = mod_patched.z64
```
