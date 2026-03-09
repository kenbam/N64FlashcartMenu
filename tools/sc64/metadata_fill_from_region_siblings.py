#!/usr/bin/env python3
"""
Fill missing SC64 metadata fields from sibling-region metadata entries.

Designed for conservative enrichment of US-facing entries (E/U/A):
- only fills blanks
- prefers same US-family donors first
- copies description files when backfilling long descriptions
- keeps region-sensitive fields (like TheGamesDB IDs) untouched
"""

from __future__ import annotations

import argparse
import configparser
import shutil
from pathlib import Path

US_REGIONS = {"E", "U", "A"}
SHARED_FIELDS = ("developer", "genre", "website", "players", "short-desc")
LONG_DESC_FIELDS = ("long-desc",)
REGION_SENSITIVE_FIELDS = ("release-date", "release-year")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Backfill missing metadata from sibling regions")
    parser.add_argument("--metadata-dir", required=True, type=Path)
    parser.add_argument("--target-regions", default="E,U,A", help="Comma-separated target regions")
    return parser.parse_args()


def read_ini(path: Path) -> tuple[configparser.ConfigParser, str] | None:
    cp = configparser.ConfigParser(interpolation=None)
    try:
        with path.open("r", encoding="utf-8", errors="ignore") as handle:
            cp.read_file(handle)
    except Exception:
        return None
    section = "meta" if cp.has_section("meta") else ("metadata" if cp.has_section("metadata") else None)
    if not section:
        return None
    return cp, section


def write_ini(cp: configparser.ConfigParser, path: Path) -> None:
    with path.open("w", encoding="utf-8") as handle:
        cp.write(handle, space_around_delimiters=False)


def donor_order_for(region: str) -> list[str]:
    if region == "E":
        return ["U", "A", "P", "J"]
    if region == "U":
        return ["E", "A", "P", "J"]
    if region == "A":
        return ["E", "U", "P", "J"]
    return ["E", "U", "A", "P", "J"]


def code_from_ini(ini: Path) -> str | None:
    parts = ini.parts
    if len(parts) < 4:
        return None
    letters = ini.parent.parts[-4:]
    if len(letters) != 4:
        return None
    return "".join(letters)


def donor_paths(metadata_dir: Path, code: str) -> list[Path]:
    prefix = code[:3]
    region = code[3].upper()
    donors: list[Path] = []
    for donor_region in donor_order_for(region):
        donor = metadata_dir.joinpath(*list(prefix + donor_region), "metadata.ini")
        if donor.exists():
            donors.append(donor)
    regionless = metadata_dir.joinpath(*list(prefix), "metadata.ini")
    if regionless.exists():
        donors.append(regionless)
    return donors


def get_value(cp: configparser.ConfigParser, section: str, key: str, ini_path: Path) -> str:
    value = cp.get(section, key, fallback="").strip()
    if key == "long-desc" and not value:
        desc = ini_path.parent / "description.txt"
        if desc.exists() and desc.is_file() and desc.stat().st_size > 0:
            return "description.txt"
    return value


def copy_description_if_needed(donor_ini: Path, target_ini: Path) -> bool:
    donor_desc = donor_ini.parent / "description.txt"
    target_desc = target_ini.parent / "description.txt"
    if target_desc.exists() or not donor_desc.exists() or not donor_desc.is_file():
        return False
    target_desc.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(donor_desc, target_desc)
    return True


def main() -> int:
    args = parse_args()
    metadata_dir: Path = args.metadata_dir
    target_regions = {value.strip().upper() for value in args.target_regions.split(",") if value.strip()}

    updated_files = 0
    copied_descriptions = 0
    fields_filled: dict[str, int] = {}

    for ini in sorted(metadata_dir.rglob("metadata.ini")):
        region = ini.parent.name.upper()
        if region not in target_regions:
            continue
        code = code_from_ini(ini)
        if not code:
            continue
        current = read_ini(ini)
        if current is None:
            continue
        cp, section = current
        dirty = False

        donors = []
        for donor_ini in donor_paths(metadata_dir, code):
            if donor_ini == ini:
                continue
            donor = read_ini(donor_ini)
            if donor is not None:
                donors.append((donor_ini, donor[0], donor[1]))

        if not donors:
            continue

        for key in SHARED_FIELDS:
            if get_value(cp, section, key, ini):
                continue
            for donor_ini, donor_cp, donor_section in donors:
                donor_value = get_value(donor_cp, donor_section, key, donor_ini)
                if donor_value:
                    cp.set(section, key, donor_value)
                    fields_filled[key] = fields_filled.get(key, 0) + 1
                    dirty = True
                    break

        for key in LONG_DESC_FIELDS:
            if get_value(cp, section, key, ini):
                continue
            for donor_ini, donor_cp, donor_section in donors:
                donor_value = get_value(donor_cp, donor_section, key, donor_ini)
                if donor_value:
                    cp.set(section, key, donor_value)
                    if donor_value == "description.txt" and copy_description_if_needed(donor_ini, ini):
                        copied_descriptions += 1
                    fields_filled[key] = fields_filled.get(key, 0) + 1
                    dirty = True
                    break

        for key in REGION_SENSITIVE_FIELDS:
            if get_value(cp, section, key, ini):
                continue
            family_donors = [item for item in donors if item[0].parent.name.upper() in US_REGIONS]
            for donor_ini, donor_cp, donor_section in family_donors:
                donor_value = get_value(donor_cp, donor_section, key, donor_ini)
                if donor_value:
                    cp.set(section, key, donor_value)
                    fields_filled[key] = fields_filled.get(key, 0) + 1
                    dirty = True
                    break

        if dirty:
            write_ini(cp, ini)
            updated_files += 1

    print(f"updated_files={updated_files}")
    print(f"copied_descriptions={copied_descriptions}")
    for key in sorted(fields_filled):
        print(f"filled_{key}={fields_filled[key]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
