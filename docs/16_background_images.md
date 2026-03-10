[Return to the index](./00_index.md)
## Background Images

### How to add a background image
First copy an image in .PNG format to anywhere on the SD card. Then when the N64FlashcartMenu is loaded on the N64, browse to the image and then select it to show it on the screen. Press the `A` Button again to open up the confirmation message, which will ask if you want to set a new background image.
<!-- Could use a sample screenshot here -->
Press the `A` Button to confirm and set the image as your new background or press the `B` Button to cancel and return to the image display screen.

### Optional native sidecar
Background images can optionally include a native sidecar next to the PNG:

- `MyBackground.png`
- `MyBackground.png.nimg`

If the `.nimg` file exists, the menu uses it first for background preview and playlist background overrides. If it does not exist, the menu falls back to the PNG path.

The current runtime also accepts a direct native path for playlist/background overrides:

- `MyBackground.png.nimg`

So both of these are valid workflows:
- keep `MyBackground.png` and add `MyBackground.png.nimg`
- point an override directly at `MyBackground.png.nimg`

Generate the sidecar with:

- `tools/sc64/menu_assets.sh bg-native <input> <output.png.nimg>`
