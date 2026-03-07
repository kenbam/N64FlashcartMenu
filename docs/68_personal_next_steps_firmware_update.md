# Next Steps: Apply New SC64 Firmware + Menu

## What Is Ready
- New menu binary (already copied to SD): `/mnt/d/sc64menu.n64`
- New SC64 firmware update file (MCU-only, playtime fix):
  - `/mnt/b/dev/SummerCart64/sc64-firmware-playtime-mcu-only.bin`

## Why You Need This
- Menu-side changes are already in place.
- Accurate playtime on power-off requires the patched SC64 controller firmware.

## Windows Flash Steps
Use PowerShell on Windows (not WSL), because USB detection in WSL is currently unavailable.

```powershell
cd B:\dev\SummerCart64
.\sw\deployer\target\release\sc64deployer.exe list
.\sw\deployer\target\release\sc64deployer.exe firmware update .\sc64-firmware-playtime-mcu-only.bin
```

If `sc64deployer.exe` is not present there, use your downloaded `sc64deployer.exe` and run:

```powershell
sc64deployer.exe list
sc64deployer.exe firmware update B:\dev\SummerCart64\sc64-firmware-playtime-mcu-only.bin
```

## Post-Flash Validation
1. Power-cycle SC64/N64.
2. Launch a game for a short test session.
3. Power off (no reset).
4. Boot menu again and verify playtime increments by real session length (not wall-clock gap).

## If Device Is Not Found
- Replug USB-C to SC64.
- Run `sc64deployer.exe list` again.
- Confirm no other app is holding SC64 USB connection.
