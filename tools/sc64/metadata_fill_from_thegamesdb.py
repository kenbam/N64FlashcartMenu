#!/usr/bin/env python3
"""
Fill SC64 metadata from TheGamesDB search results.

Conservative behavior by default:
- matches only Nintendo 64 search results (platform=3)
- strongly prefers exact normalized title matches
- uses region and existing release-year as tie-breakers
- fills only missing fields unless --overwrite is used
- records TheGamesDB game id for future enrichment/manual linking
"""

from __future__ import annotations

import argparse
import configparser
import json
import os
import re
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


UA = "N64FlashcartMenuMetadataTool/1.0 (TheGamesDB)"
API_ENV_KEY = "THEGAMESDB_API_KEY"
N64_PLATFORM_ID = 3
DEFAULT_DELAY_MS = 125
MAX_RETRIES = 2

REGION_HINTS = {
    "JP": (("japan", "jpn"), {"country_id": {28}, "region_id": {4}}),
    "US": (("usa", "us", "north america", "ntsc-u"), {"country_id": {50}, "region_id": {1}}),
    "EU": (("europe", "pal", "eur", "uk", "germany", "france", "spain", "italy"), {"country_id": set(), "region_id": {6}}),
    "AU": (("australia",), {"country_id": set(), "region_id": set()}),
}

VARIANT_PATTERNS = (
    r"\bplayer s choice\b",
    r"\bnot for resale\b",
    r"\bprototype\b",
    r"\bdemo\b",
    r"\bbeta\b",
    r"\b64dd\b",
    r"\bchaos edition\b",
    r"\bplus\b",
    r"\bcoop deluxe\b",
    r"\bthrough the ages\b",
    r"\bmissing stars\b",
    r"\broyal legacy\b",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fill SC64 metadata from TheGamesDB")
    parser.add_argument("--metadata-dir", required=True, type=Path)
    parser.add_argument("--limit", type=int, default=0, help="Max metadata.ini files to update (0 = unlimited)")
    parser.add_argument("--delay-ms", type=int, default=DEFAULT_DELAY_MS, help="Delay between API calls")
    parser.add_argument("--overwrite", action="store_true", help="Replace existing values for supported keys")
    parser.add_argument(
        "--fields",
        default="thegamesdb-id,release-date,release-year",
        help="Comma-separated fields to fill: thegamesdb-id,release-date,release-year",
    )
    parser.add_argument(
        "--report-dir",
        type=Path,
        default=Path("reports") / "thegamesdb",
        help="Directory for match/unresolved reports",
    )
    parser.add_argument(
        "--dotenv",
        type=Path,
        default=Path(".env"),
        help="Path to dotenv file containing THEGAMESDB_API_KEY",
    )
    return parser.parse_args()


def load_dotenv(path: Path) -> dict[str, str]:
    env: dict[str, str] = {}
    if not path.exists():
        return env
    for raw_line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        env[key.strip()] = value.strip().strip('"').strip("'")
    return env


def get_api_key(dotenv_path: Path) -> str:
    if os.environ.get(API_ENV_KEY):
        return os.environ[API_ENV_KEY].strip()
    env = load_dotenv(dotenv_path)
    key = env.get(API_ENV_KEY, "").strip()
    if not key:
        raise SystemExit(f"missing {API_ENV_KEY}; set it in environment or {dotenv_path}")
    return key


def http_json(url: str) -> dict[str, Any]:
    last_error: Exception | None = None
    for attempt in range(1, MAX_RETRIES + 1):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=10) as resp:
                return json.loads(resp.read().decode("utf-8", errors="ignore"))
        except Exception as exc:  # noqa: BLE001 - deliberate retry wrapper
            last_error = exc
            if attempt < MAX_RETRIES:
                time.sleep(0.4 * attempt)
    assert last_error is not None
    raise last_error


def normalize_title(text: str) -> str:
    title = (text or "").strip().lower()
    title = re.sub(r"\([^\)]*\)", " ", title)
    title = title.replace("&", " and ")
    title = title.replace("'", "")
    title = title.replace("’", "")
    title = re.sub(r"[^a-z0-9]+", " ", title)
    title = re.sub(r"\s+", " ", title).strip()
    return title


def tokenize(text: str) -> list[str]:
    return [token for token in normalize_title(text).split() if token]


def extract_region_hint(name: str) -> str | None:
    lowered = (name or "").lower()
    for code, (needles, _matcher) in REGION_HINTS.items():
        if any(needle in lowered for needle in needles):
            return code
    return None


def extract_region_hint_from_path(path: Path) -> str | None:
    parts = path.parts
    if len(parts) < 5:
        return None
    region = parts[-2].upper()
    if region in {"E", "U", "A"}:
        return "US"
    if region == "P":
        return "EU"
    if region == "J":
        return "JP"
    return None




def search_name_for_query(name: str) -> str:
    text = (name or "").strip()
    patterns = (
        r"\s*\((?:Japan|USA|US|Europe|PAL|Australia|Germany|France|Spain|Italy|UK|World)[^\)]*\)\s*$",
        r"\s*\([^\)]*(?:NTSC|PAL|REV|PROTO|BETA|DEMO|EN|FR|DE|ES|IT|NL|SV|NO|DA|FI|ZH|KO|JA|JPN|USA)[^\)]*\)\s*$",
        r"\s*\([^\)]*\|[^\)]*\)\s*$",
    )
    changed = True
    while changed:
        changed = False
        for pattern in patterns:
            stripped = re.sub(pattern, "", text, flags=re.IGNORECASE)
            if stripped != text:
                text = stripped.strip()
                changed = True
    return text.strip()

def extract_year(value: str) -> int | None:
    match = re.search(r"(19|20)\d{2}", value or "")
    if not match:
        return None
    year = int(match.group(0))
    return year if 1980 <= year <= 2099 else None


def is_variant_title(title: str) -> bool:
    normalized = normalize_title(title)
    return any(re.search(pattern, normalized) for pattern in VARIANT_PATTERNS)


def exactish_match(query: str, candidate: str) -> bool:
    q = normalize_title(query)
    c = normalize_title(candidate)
    if not q or not c:
        return False
    if q == c:
        return True
    q_tokens = tokenize(query)
    c_tokens = tokenize(candidate)
    if not q_tokens or not c_tokens:
        return False
    return q_tokens == c_tokens


def title_overlap_score(query: str, candidate: str) -> int:
    q_tokens = set(tokenize(query))
    c_tokens = set(tokenize(candidate))
    if not q_tokens or not c_tokens:
        return 0
    overlap = len(q_tokens & c_tokens)
    return overlap * 8 - max(0, len(c_tokens) - len(q_tokens)) * 3


def region_score(region_hint: str | None, game: dict[str, Any]) -> int:
    if not region_hint:
        return 0
    _needles, matcher = REGION_HINTS[region_hint]
    score = 0
    region_id = int(game.get("region_id") or 0)
    country_id = int(game.get("country_id") or 0)
    if matcher["region_id"] and region_id in matcher["region_id"]:
        score += 15
    if matcher["country_id"] and country_id in matcher["country_id"]:
        score += 15
    if region_id == 0 and country_id == 0:
        score += 3
    if region_hint == "JP" and region_id == 1:
        score -= 10
    if region_hint == "US" and region_id == 4:
        score -= 10
    if region_hint == "EU" and region_id in {1, 4}:
        score -= 10
    return score


def candidate_score(query_name: str, current_year: int | None, region_hint: str | None, game: dict[str, Any]) -> int:
    if int(game.get("platform") or 0) != N64_PLATFORM_ID:
        return -10_000

    title = game.get("game_title") or ""
    score = 0

    if exactish_match(query_name, title):
        score += 100
    else:
        overlap = title_overlap_score(query_name, title)
        if overlap < 12:
            return -10_000
        score += overlap

    if is_variant_title(title) and not is_variant_title(query_name):
        score -= 25

    candidate_year = extract_year(game.get("release_date") or "")
    if current_year and candidate_year:
        diff = abs(current_year - candidate_year)
        if diff == 0:
            score += 20
        elif diff == 1:
            score += 10
        elif diff > 1:
            score -= min(30, diff * 5)

    score += region_score(region_hint, game)

    title_norm = normalize_title(title)
    query_norm = normalize_title(query_name)
    if title_norm == query_norm:
        score += 10

    return score


def read_meta_ini(path: Path) -> tuple[configparser.ConfigParser, str]:
    cp = configparser.ConfigParser(interpolation=None)
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        cp.read_file(handle)
    section = "meta" if cp.has_section("meta") else ("metadata" if cp.has_section("metadata") else "meta")
    if not cp.has_section(section):
        cp.add_section(section)
    return cp, section


def write_ini(cp: configparser.ConfigParser, path: Path) -> None:
    with path.open("w", encoding="utf-8") as handle:
        cp.write(handle, space_around_delimiters=False)


def tgdb_search(api_key: str, name: str) -> list[dict[str, Any]]:
    params = {
        "apikey": api_key,
        "name": name,
    }
    url = "https://api.thegamesdb.net/v1/Games/ByGameName?" + urllib.parse.urlencode(params)
    payload = http_json(url)
    return payload.get("data", {}).get("games", []) or []


def pick_best_game(name: str, current_year: int | None, region_hint: str | None, games: list[dict[str, Any]]) -> tuple[dict[str, Any] | None, list[tuple[int, dict[str, Any]]]]:
    scored: list[tuple[int, dict[str, Any]]] = []
    for game in games:
        score = candidate_score(name, current_year, region_hint, game)
        if score > -10_000:
            scored.append((score, game))
    scored.sort(key=lambda item: (item[0], -int(item[1].get("id") or 0)), reverse=True)
    if not scored:
        return None, []
    best_score, best_game = scored[0]
    second_score = scored[1][0] if len(scored) > 1 else -10_000
    if best_score < 100:
        return None, scored
    if region_hint and region_score(region_hint, best_game) < 0:
        return None, scored
    if second_score > best_score - 4:
        return None, scored
    return best_game, scored


def should_fill(cp: configparser.ConfigParser, section: str, field: str, overwrite: bool) -> bool:
    return overwrite or not cp.get(section, field, fallback="").strip()


def ensure_report_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def append_report_row(path: Path, columns: list[str], row: dict[str, Any], header_written: set[Path]) -> None:
    ensure_report_dir(path.parent)
    write_header = path not in header_written and not path.exists()
    with path.open("a", encoding="utf-8") as handle:
        if write_header:
            handle.write("\t".join(columns) + "\n")
            header_written.add(path)
        handle.write("\t".join(str(row.get(col, "")) for col in columns) + "\n")


def main() -> int:
    args = parse_args()
    metadata_dir: Path = args.metadata_dir
    report_dir: Path = args.report_dir
    fields = {field.strip() for field in args.fields.split(",") if field.strip()}
    delay_seconds = max(0, args.delay_ms) / 1000.0
    limit = max(0, args.limit)
    api_key = get_api_key(args.dotenv)

    if not metadata_dir.is_dir():
        raise SystemExit(f"metadata directory not found: {metadata_dir}")

    updated = 0
    scanned = 0
    queried = 0
    matched = 0
    unresolved = 0
    search_cache: dict[str, list[dict[str, Any]]] = {}
    header_written: set[Path] = set()
    match_report = report_dir / "matches.tsv"
    unresolved_report = report_dir / "unresolved.tsv"

    for ini in sorted(metadata_dir.rglob("metadata.ini")):
        cp, section = read_meta_ini(ini)
        name = cp.get(section, "name", fallback="").strip()
        if not name:
            continue

        needs_any = False
        for field in fields:
            if field == "thegamesdb-id" and should_fill(cp, section, "thegamesdb-id", args.overwrite):
                needs_any = True
            elif field == "release-date" and should_fill(cp, section, "release-date", args.overwrite):
                needs_any = True
            elif field == "release-year" and should_fill(cp, section, "release-year", args.overwrite):
                needs_any = True
        if not needs_any:
            continue

        scanned += 1
        if limit and scanned > limit:
            break

        current_year = extract_year(cp.get(section, "release-year", fallback="") or cp.get(section, "release-date", fallback=""))
        region_hint = extract_region_hint_from_path(ini) or extract_region_hint(name)

        search_name = search_name_for_query(name) or name

        try:
            if search_name in search_cache:
                games = search_cache[search_name]
            else:
                games = tgdb_search(api_key, search_name)
                search_cache[search_name] = games
                queried += 1
                if delay_seconds:
                    time.sleep(delay_seconds)
        except Exception as exc:  # noqa: BLE001
            unresolved += 1
            append_report_row(
                unresolved_report,
                ["metadata_ini", "name", "search_name", "reason"],
                {"metadata_ini": ini, "name": name, "search_name": search_name, "reason": f"query_failed:{exc}"},
                header_written,
            )
            continue

        best_game, scored = pick_best_game(name, current_year, region_hint, games)
        if best_game is None:
            unresolved += 1
            top = scored[0][1] if scored else {}
            top_score = scored[0][0] if scored else ""
            append_report_row(
                unresolved_report,
                ["metadata_ini", "name", "search_name", "reason", "top_score", "top_title", "top_release_date", "top_region_id", "top_country_id"],
                {
                    "metadata_ini": ini,
                    "name": name,
                    "search_name": search_name,
                    "reason": "no_confident_match",
                    "top_score": top_score,
                    "top_title": top.get("game_title", ""),
                    "top_release_date": top.get("release_date", ""),
                    "top_region_id": top.get("region_id", ""),
                    "top_country_id": top.get("country_id", ""),
                },
                header_written,
            )
            continue

        dirty = False
        matched += 1
        game_id = str(best_game.get("id") or "").strip()
        release_date = (best_game.get("release_date") or "").strip()
        release_year = str(extract_year(release_date) or "")

        if "thegamesdb-id" in fields and game_id and should_fill(cp, section, "thegamesdb-id", args.overwrite):
            cp.set(section, "thegamesdb-id", game_id)
            dirty = True
        if "release-date" in fields and release_date and release_date != "1970-01-01" and should_fill(cp, section, "release-date", args.overwrite):
            cp.set(section, "release-date", release_date)
            dirty = True
        if "release-year" in fields and release_year and should_fill(cp, section, "release-year", args.overwrite):
            cp.set(section, "release-year", release_year)
            dirty = True

        if dirty:
            write_ini(cp, ini)
            updated += 1

        append_report_row(
            match_report,
            ["metadata_ini", "name", "thegamesdb_id", "matched_title", "release_date", "region_id", "country_id", "updated"],
            {
                "metadata_ini": ini,
                "name": name,
                "thegamesdb_id": game_id,
                "matched_title": best_game.get("game_title", ""),
                "release_date": release_date,
                "region_id": best_game.get("region_id", ""),
                "country_id": best_game.get("country_id", ""),
                "updated": int(dirty),
            },
            header_written,
        )

        if scanned % 50 == 0:
            print(f"scanned={scanned} queried={queried} matched={matched} updated={updated} unresolved={unresolved}", flush=True)

    print(f"scanned={scanned}")
    print(f"queried={queried}")
    print(f"matched={matched}")
    print(f"updated={updated}")
    print(f"unresolved={unresolved}")
    print(f"report_dir={report_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
