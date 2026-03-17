# 3DS Camera

A Nintendo 3DS homebrew camera application built with libctru and citro2d. Captures live camera input, applies real-time image filters, and saves photos to the SD card.

## Features

- Live camera preview on the top screen
- Real-time image filters: brightness, contrast, saturation, gamma
- Colour palette modes
- Adjustable pixel size (1–8) via snap slider
- Save photos to SD card at 2× upscaled resolution (800×480)

## Controls

| Input | Action |
|-------|--------|
| Y / CAM button (touch) | Capture photo |
| A / SAVE button (touch) | Save photo to SD card |
| L | Previous palette |
| R | Next palette |
| B | Reset filters |
| D-Pad Up/Down | Brightness |
| D-Pad Left/Right | Saturation |
| Touch sliders | Brightness, contrast, gamma, pixel size |
| START | Quit |

## Building

### Requirements

- [devkitARM](https://devkitpro.org/) with 3DS support
- libctru, citro2d, citro3d (via devkitPro)
- `makerom` binary in the project root (for CIA builds)

### Build 3DSX (homebrew launcher)

```bash
make
```

Output: `3ds_camera.3dsx`

### Build CIA (installable via FBI)

```bash
make cia
```

Output: `3ds_camera.cia`

This runs `make` first, then invokes `makerom` with the bundled `3ds_camera.rsf`.

### Clean

```bash
make clean
```

## Installing

- **3DSX**: Copy `3ds_camera.3dsx` to `/3ds/` on your SD card and launch via the Homebrew Launcher.
- **CIA**: Install `3ds_camera.cia` via [FBI](https://github.com/Steveice10/FBI) on a 3DS running CFW (e.g. Luma3DS).

## Project Structure

```
source/         C source files (main.c, filter.c, image_load.c)
filter/         Filter header files
romfs/          ROM filesystem assets
3ds_camera.rsf  makerom ROM spec file for CIA builds
makerom         makerom binary (macOS)
```
