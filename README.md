# Live Dither Wallpaper

A cross-platform animated wallpaper engine that renders dithered, two-color animations directly on your desktop.

![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)

## Features

Cross-platform support for Windows [\[Progman/WorkerW\]](https://github.com/rocksdanister/lively/issues/2074#issuecomment-2954330622) and Linux X11. Uses optimized dithering that only animates ambiguous pixels while caching static regions. Desktop icons remain fully interactive with click-through support. Choose from static, random noise, and animated wave patterns with configurable brightness threshold, pixel size, and FPS limit.

## Requirements

### Linux (X11)

X11 development libraries and wmctrl for window management. XFCE requires [patched xfdesktop](https://github.com/arfelious/xfdesktop-live-wallpaper) since the standard xfdesktop covers the wallpaper window.

```bash
# Debian/Ubuntu
sudo apt install libx11-dev libxrandr-dev libgl1-mesa-dev wmctrl

# Arch
sudo pacman -S libx11 libxrandr mesa wmctrl
```

### Windows

OpenGL support (included with graphics drivers) and Visual Studio or MinGW.

## Building

```bash
make
```

The Makefile auto-detects your platform (Linux/Windows) and builds accordingly.

| Target | Description |
|--------|-------------|
| `make` | Build the executable |
| `make debug` | Build with debug symbols |
| `make run` | Build and run |
| `make install` | Install to /usr/local/bin (Linux) |
| `make clean` | Remove built files |

## Usage

```bash
./live-dither-wp [image] [algorithm] [threshold] [pixel_size] [max_fps] [profile] [chaos]
./live-dither-wp --restore  # Restore xfdesktop settings without running
```

| Argument | Default | Description |
|----------|---------|-------------|
| image | bg.jpg | Path to background image |
| algorithm | 2 | 0=static, 1=random, 2=wave |
| threshold | 40 | Brightness threshold (0-255) |
| pixel_size | 1 | Block size for pixelation |
| max_fps | 60 | FPS limit (0=unlimited) |
| profile | 1 | Print FPS info (0=off, 1=on) |
| chaos | 10 | Randomness blend for wave (0-100) |
| --restore, -r | — | Restore xfdesktop settings and exit (X11 only) |

### Examples

```bash
# Default wave animation
./live-dither-wp

# Custom image with random dithering
./live-dither-wp wallpaper.png 1

# Wave animation with high chaos and pixelation
./live-dither-wp bg.jpg 2 40 4 60 0 50

# Restore settings if the program was killed unexpectedly
./live-dither-wp --restore
```

## How It Works

The engine loads an image and scales it to screen resolution, then classifies each pixel as black, orange, or ambiguous based on color distance. Static pixels (clearly black or orange) are cached and never recalculated. Only ambiguous pixels are animated each frame using the selected algorithm, then rendered to a desktop-type window below all other windows.

### Algorithms

Static (0) renders a single dithered frame with no animation. Random (1) flips each ambiguous pixel randomly based on its probability. Wave (2) sweeps a sine wave across the screen with optional chaos parameter for organic movement.

## XFCE Integration

On XFCE, the standard xfdesktop covers the live wallpaper. The [patched xfdesktop](https://github.com/arfelious/xfdesktop-live-wallpaper) uses NORMAL window type instead of DESKTOP for proper stacking, enables RGBA visual for transparency, and sets an identifiable window title for wmctrl. See [xfdesktop-live-wallpaper/README.md](https://github.com/arfelious/xfdesktop-live-wallpaper/blob/live-wallpaper-4.20.1/README.md) for build instructions.

## Stopping

On Linux, press Ctrl+C or send SIGTERM. Settings are automatically restored on clean exit. If the process was killed unexpectedly, use `./live-dither-wp --restore` to restore xfdesktop settings. On Windows, press Ctrl+C or close the window.

## License

MIT
