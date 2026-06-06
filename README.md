# Shaper Pak

Shaper is a NextUI tool for customizing the rounded cap shapes used in the system UI. It previews the selected shapes, applies them to the active NextUI assets, can restore the original assets from backup, and supports user-added PNG caps.

## Requirements

This pak is designed for and tested only on Trimui Brick `tg5040`.

## Installation

1. Mount your NextUI SD card.
2. Download the latest GitHub release: [`Shaper.pak.zip`](https://github.com/icoreee/nextui-shaper-pak/releases/latest).
3. Extract the archive and copy its contents to `/Tools/tg5040/Shaper.pak/` on your SD card.
4. Confirm that `/Tools/tg5040/Shaper.pak/launch.sh` exists on your SD card.
5. Unmount your SD card and insert it into your NextUI device.

## Custom Caps

You can add your own cap shapes by copying PNG files to:

```text
Shaper.pak/caps/
```

Cap filenames become labels in the app, so use clear lowercase names such as `soft_round.png` or `double_notch.png`. PNG files are loaded at app startup; restart the pak after adding or removing files.

Custom cap requirements:

- Format: transparent PNG.
- Size: 60x120 px.
- Orientation: draw a left cap only. The outer shape must face left, and the flat seam must be on the right. The app mirrors it automatically for right-side caps.

## Notes

`Restore` copies the backed-up original `assets@1x..4x.png` files back into `.system/res`, undoing the currently applied pill changes.

The app creates backup files in userdata:

```text
$SDCARD_PATH/.userdata/$PLATFORM/shaper/backup/
```

`Restore` can also read old backups from `.userdata/$PLATFORM/pill-shapes/backup/` and `Shaper.pak/backup/` for compatibility with earlier builds.

It stores the current selection in:

```text
$SDCARD_PATH/.userdata/$PLATFORM/shaper.cfg
```

## Build

Local macOS preview build:

```sh
make build
```

The repository vendors Apostrophe at:

```text
third_party/apostrophe
```

If you want to use another Apostrophe checkout, pass the path explicitly:

```sh
make build APOSTROPHE_DIR=/path/to/apostrophe
```

Device build requires the NextUI cross compiler/sysroot:

```sh
make device PLATFORM=tg5040 CC="${CROSS_COMPILE}gcc"
```

The generated folder is:

```text
Shaper.pak
```

For a real SD card install, make sure `Shaper.pak/pill-shapes.elf` is a Linux ARM executable, not a macOS Mach-O binary:

```sh
file "Shaper.pak/pill-shapes.elf"
```

Release builds create a Pak Store compatible archive:

```text
dist/Shaper.pak.zip
```

The archive contains the pak files at the zip root, as expected by Pak Store.

The easiest device release path is Docker:

```sh
make docker-release
```
