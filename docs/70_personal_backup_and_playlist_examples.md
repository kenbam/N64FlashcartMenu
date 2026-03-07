# Personal Backup + Playlist Examples

This is a personal workflow document for backing up menu metadata and playlists from an SD card, then restoring them later.

## Why keep this in-repo
- Keeps your backup process versioned and repeatable.
- Keeps playlist directive examples close to menu source behavior.
- Makes it easy to diff metadata/playlist changes over time.

## Recommended backup scope
- `/menu/metadata/`
- `/Playlists/`
- Optional: `/menu/backgrounds/`, `/menu/music/`

## One-command backup (WSL)
Adjust the paths for your setup.

```bash
# From repo root
STAMP=$(date +%Y%m%d_%H%M%S)
DEST="$PWD/backups/sd_snapshot_$STAMP"
mkdir -p "$DEST"

rsync -a --delete /mnt/d/menu/metadata/ "$DEST/menu/metadata/"
rsync -a --delete /mnt/d/Playlists/ "$DEST/Playlists/"

# Optional assets
rsync -a --delete /mnt/d/menu/backgrounds/ "$DEST/menu/backgrounds/"
rsync -a --delete /mnt/d/menu/music/ "$DEST/menu/music/"

echo "Backup created at: $DEST"
```

## One-command restore (WSL)
Use carefully: this overwrites SD card data in those folders.

```bash
SNAP="/mnt/b/dev/N64FlashcartMenu/backups/sd_snapshot_YYYYMMDD_HHMMSS"

rsync -a --delete "$SNAP/menu/metadata/" /mnt/d/menu/metadata/
rsync -a --delete "$SNAP/Playlists/" /mnt/d/Playlists/

# Optional assets
rsync -a --delete "$SNAP/menu/backgrounds/" /mnt/d/menu/backgrounds/
rsync -a --delete "$SNAP/menu/music/" /mnt/d/menu/music/
```

## Quick integrity checks
```bash
find /mnt/d/menu/metadata -name metadata.ini | wc -l
find /mnt/d/Playlists -name '*.m3u' | wc -l
```

## Playlist directive examples
See:
- `examples/playlists/Playlist Example - Racing.m3u`
- `examples/playlists/Playlist Example - Chill Grid.m3u`
- `examples/playlists/Playlist Example - Minimal.m3u`

These are safe templates and can be copied/renamed directly.

## Metadata key example
Smart playlists can now filter on richer metadata fields. A minimal `metadata.ini` example:

```ini
[metadata]
name=Wave Race 64
author=Nintendo
developer=Nintendo EAD
genre=Racing
series=Wave Race
players=1-2
modes=Championship, Time Trials, Stunt, Versus
release-year=1996
age-rating=3
short-desc=Arcade jet-ski racing with strong split-screen versus.
```

## Full personal playlist snapshot (in-repo)
The full current SD playlist set is tracked at:
- `playlists/personal/`

This includes:
- `playlists/personal/Genres/` (genre playlists from SD `/Genres`)
- `playlists/personal/By Year/`, `Custom/`, `Fun/`, `History/`, `Ranked/`, `Studios/`

To refresh that snapshot from SD:

```bash
rsync -a /mnt/d/Playlists/ /mnt/b/dev/N64FlashcartMenu/playlists/personal/
rsync -a /mnt/d/Genres/ /mnt/b/dev/N64FlashcartMenu/playlists/personal/Genres/
```
