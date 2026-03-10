[Return to the index](./00_index.md)
## Menu Settings
N64FlashcartMenu automatically creates a `config.ini` file in `sd:/menu/`, which contains various settings that can be set within the menu's Settings editor.
If required, you can manually adjust the file (required for some advanced settings) on the SD card using your computer.

> [!NOTE]
> This page covers the main built-in settings. The current customized build also exposes additional options such as theme presets, background image selection, menu BGM, visualizer settings, text panel controls, and multiple screensaver modes. See [Menu Enhancements](./63_menu_enhancements.md) for those branch-level additions.

### Show Hidden Files
Shows any N64FlashcartMenu system-related files. This setting is OFF by default.

### Use Save Folders
Controls whether N64FlashcartMenu should use `/saves` folders to store ROM save data. This setting is ON by default.
ON: ROM saves are saved in separate subfolders (called `\saves`, will create one `\saves` subfolder per folder).
OFF: ROM saves are saved alongside the ROM file.

### Sound Effects
The menu has default sound effects to improve the user experience. See the [sound documentation](./40_sound.md) for details. This setting is OFF by default.

### Background Music
Menu BGM can be enabled or disabled from Settings, and the menu music file can be picked from the SD card. Both `MP3` and `WAV64` are supported in the current customized build.

### Screensaver
The Settings editor currently exposes:
- `Screensaver Type`: `DVD Logo`, `3D Pipes`, or `Living Gradient`
- `Screensaver Logo`: custom logo picker for the DVD mode
- `Screensaver Smooth`: `60 FPS` or `30 FPS`
- per-edge margins: `Left`, `Right`, `Top`, `Bottom`

These settings are stored in `config.ini` and applied at runtime without requiring a rebuild.

### Theme, text panel, and visualizer options
The current customized build also includes:
- theme preset selection
- background image picker
- text panel overlay on/off
- text panel strength
- visualizer background on/off
- visualizer style and intensity
- selected-row shimmer toggle

### Fast ROM reboots
Certain flashcarts support the ability to use the N64 `RESET` button for re-loading the last game, rather than returning to the menu. When enabled (and if supported by your flashcart), the power switch must be toggled to return to the menu.  

> [!TIP] if a USB cable is connected to the flashcart, the last game will continue to be re-loaded. If debugging the menu, make sure this option is off! 

Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.
This setting is OFF by default.
