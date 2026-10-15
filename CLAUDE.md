# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`shot` is a screenshot capture program for Linux written in C, inspired by `scrot`. The project is at an early skeleton stage: `src/shot.c` and `include/shot/shot.h` are essentially empty, and `exe/shot_cli/src/main.c` is a hello-world placeholder.

## Build

```sh
./compile.sh          # mkdir -p build && cmake .. && make -j$(nproc), run from repo root
./build/shot_cli      # run the CLI
```

There is no test suite or lint configuration yet. `compile_commands.json` is exported to `build/` for clangd.

## Architecture

- **Core library (`shot`)**: a static library built from every `src/**/*.c` file (globbed). Its public headers live in `include/`, included as `<shot/shot.h>`. It links `X11`, `Xrandr`, `jpeg`, `png`, `m`, `pthread` and `atomic`. Capture is meant to go through X11/Xrandr and output through libjpeg/libpng.
- **Executables (`exe/<name>/`)**: each one is registered in `CMakeLists.txt` with `add_sub_project(<name>)`, which globs `exe/<name>/src/**/*.c`, adds `exe/<name>/include` to its include path, and links against the `shot` library. If the directory doesn't exist yet, `add_sub_project` creates it with a hello-world `main.c`. Right now the only executable is `shot_cli`.

## Build quirks

- Sources are found with `GLOB_RECURSE`, so re-run `cmake` (or `./compile.sh`) after adding or removing `.c` files.
- The build type is hard-coded to `RelWithDebInfo` with `-DNDEBUG` removed, so `assert()` stays active.
- Everything compiles with `-mavx2 -mfma -fno-strict-aliasing -Wall`, so the binaries need an x86-64 CPU that supports AVX2/FMA.
- `CMakeLists.txt` includes `FetchContent` and references `${cglm_SOURCE_DIR}`/`${glfw_SOURCE_DIR}`, but nothing fetches those dependencies. These are leftovers from a template and currently have no effect.
