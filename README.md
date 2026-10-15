# shot
Screenshot capture program for linux, inspired by `scrot`

## Usage
```sh
./compile.sh
./build/shot_cli [-o PATH]
cd build && make install   # installs shot_cli to ~/.local/bin
```
Drag to select a region, then press `s`/Enter (or click **Save**) to save it, or `c` (or click **Copy**) to copy it to the clipboard as PNG. `Esc`, `q` or right click cancels. Clicking outside the buttons starts a new selection.

Press `d` (or click **Draw**) to draw on the selection with a pencil. In draw mode, `1`-`4` pick the color (red, green, blue, yellow) and `u` undoes the last stroke. `d` again or `Esc` leaves draw mode and keeps the drawing.

Files go to `~/Pictures/shot_<timestamp>.png` by default (the current directory if `~/Pictures` doesn't exist); `-o` picks the path, and a `.jpg`/`.jpeg` extension writes JPEG.


## Project Structure
- `./src` and `./include` is the core `shot` library
- `./exe` contains source code for the executable
