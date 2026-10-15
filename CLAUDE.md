# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`shot` is a screenshot capture program for Linux (X11) written in C, inspired by `scrot`. `shot_cli` freezes the screen, lets the user drag out a rectangle, then saves it to disk or copies it to the clipboard as `image/png`.

## Build

```sh
./compile.sh          # mkdir -p build && cmake .. && make -j$(nproc), run from repo root
./build/shot_cli      # run the CLI
(cd build && make install)  # installs shot_cli to ~/.local/bin (override with -DCMAKE_INSTALL_PREFIX)
```

There is no test suite or lint configuration yet. To test without taking over the real display, run against Xvfb (start it with `-noreset`, or the server wipes the root window when the last client disconnects) and fake input with XTest. `compile_commands.json` is exported to `build/` for clangd.

## Architecture

- **Core library (`shot`)**: a static library built from every `src/**/*.c` file (globbed). Its public headers live in `include/`, included as `<shot/shot.h>`. It links `X11`, `Xrandr`, `jpeg`, `png`, `m`, `pthread` and `atomic`. Capture is meant to go through X11/Xrandr and output through libjpeg/libpng.
  - `capture.c`: grabs the root window into an `XImage` and converts regions to packed RGB (`ShotImage`).
  - `select.c`: the selection overlay. It is a fullscreen override-redirect window showing the frozen capture (dimmed, with the selection lit), double-buffered through a back pixmap and redrawn only in damaged areas. It grabs the pointer and keyboard.
  - `encode.c`: PNG (file or memory) and JPEG (file, picked by `.jpg`/`.jpeg` extension).
  - `clipboard.c`: X11 clipboards only live as long as the owner, so `shot_clipboard_set` forks a detached child that owns `CLIPBOARD`, serves `TARGETS`/`TIMESTAMP`/the mime type (using INCR for large data), and exits once another client takes ownership.
- **Executables (`exe/<name>/`)**: each one is registered in `CMakeLists.txt` with `add_sub_project(<name>)`, which globs `exe/<name>/src/**/*.c`, adds `exe/<name>/include` to its include path, and links against the `shot` library. If the directory doesn't exist yet, `add_sub_project` creates it with a hello-world `main.c`. Right now the only executable is `shot_cli`.

## Build quirks

- Sources are found with `GLOB_RECURSE`, so re-run `cmake` (or `./compile.sh`) after adding or removing `.c` files.
- The build type is hard-coded to `RelWithDebInfo` with `-DNDEBUG` removed, so `assert()` stays active.
- Everything compiles with `-mavx2 -mfma -fno-strict-aliasing -Wall`, so the binaries need an x86-64 CPU that supports AVX2/FMA.
- `CMakeLists.txt` includes `FetchContent` and references `${cglm_SOURCE_DIR}`/`${glfw_SOURCE_DIR}`, but nothing fetches those dependencies. These are leftovers from a template and currently have no effect.
