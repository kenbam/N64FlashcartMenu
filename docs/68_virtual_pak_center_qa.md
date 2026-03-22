# Virtual Pak Center QA

## Quick

- Open `Virtual Pak Center` from browser tools. Confirm it opens, tabs switch, and `B` returns to browser.
- Open it from settings. Confirm `B` returns to settings.
- Open it from a ROM details page. Confirm it lands on `ROM Slot`, not `Status`.
- On a ROM that supports Controller Pak:
  - toggle virtual pak on/off
  - change slot up/down
  - back out and reopen ROM details to confirm persistence
- On a ROM that does not support Controller Pak:
  - open `ROM Slot`
  - confirm it clearly says not supported and does not silently misbehave
- `All Slots`:
  - list appears
  - titles resolve for known ROMs
  - selection moves cleanly
  - `A` opens owning ROM
  - backing out from that ROM returns to the center
- Delete flow:
  - first `Start` only arms delete
  - second `Start` deletes
  - moving selection cancels armed state
- Recovery:
  - with no pending session, `Recovery` should read cleanly and not act weird
  - with a pending session, retry and force-clear should behave as expected
- Browser recovery overlay:
  - when recovery is pending/failed, `R` should open the center

## In Depth

### 1. Entry/Return Flow

- Browser tools -> center -> `B` returns to browser
- Settings -> center -> `B` returns to settings
- ROM details -> center -> `B` returns to ROM details
- `All Slots` -> open owning ROM -> `B` returns to center, not browser

### 2. Tab Defaults

- Open from browser with no pending session: default tab should feel sensible
- Open from browser with pending session: should land on `Recovery`
- Open from ROM details: should land on `ROM Slot`

### 3. Status Accuracy

- `Status` should correctly reflect:
  - physical pak inserted/not inserted
  - bank count
  - slot/backup/rescue file presence
  - session active vs idle
- Remove/reinsert pak while in center and use `A` refresh on `Status`
- Confirm values update and don’t look stale

### 4. ROM Slot Controls

- Test a Controller Pak game:
  - enable virtual pak
  - change slot several times
  - leave/reenter ROM details
  - confirm setting persisted
- Test a non-Controller-Pak game:
  - no crashes
  - clear warning/message
- Test from a ROM with virtual pak already enabled:
  - center should show correct current slot immediately

### 5. All Slots List

- Use a library large enough that multiple slot files exist
- Confirm:
  - list order is stable
  - scrolling works at top/middle/bottom
  - visible titles populate without obvious hitching
  - orphaned slots show fallback identity and `ROM missing`
- Confirm `A` opens only when owner exists, and shows a sensible message otherwise

### 6. Delete Safety

- On `All Slots`:
  - `Start` once should arm delete and not remove file
  - `Start` twice should remove file
  - changing selection should disarm
  - tab switch should disarm
  - leaving/reentering center should not preserve armed state
- Confirm deleted file is actually gone on SD
- Confirm list refreshes correctly after delete

### 7. Recovery Flow

- Create or simulate a pending virtual-pak session
- Verify:
  - browser overlay appears
  - `A` retries
  - `B` snoozes/continues
  - `R` opens center
  - `Start` force clears
- In center `Recovery` tab:
  - retry with correct pak inserted
  - retry with wrong/no pak inserted
  - force clear
- Confirm messages make sense in each case

### 8. Regression Checks

- Existing physical Controller Pak manager still opens and works
- ROM details page still works after center integration
- No weird back-stack behavior after:
  - center -> pak manager -> back
  - center -> owning ROM -> back
  - center -> settings/browser -> back
- No major stalls entering center with many slot files

### 9. Data Integrity

- For one known game:
  - enable virtual pak
  - choose slot
  - launch game and create/update save
  - return to menu
  - confirm slot file updated
  - confirm physical pak restored
- Repeat with another slot for same game
- Repeat with another game using a different slot
