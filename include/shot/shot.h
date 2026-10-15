#ifndef SHOT_H
#define SHOT_H

#include <X11/Xlib.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  int x, y;
  int width, height;
} ShotRect;

/* Tightly packed 8-bit RGB pixels, row-major, stride = width * 3. */
typedef struct {
  int width, height;
  uint8_t* pixels;
} ShotImage;

typedef enum {
  SHOT_ACTION_CANCEL = 0,
  SHOT_ACTION_SAVE,
  SHOT_ACTION_COPY,
} ShotAction;

/* capture.c */
XImage* shot_grab_screen(Display* dpy);
int shot_image_from_ximage(const XImage* ximg, ShotRect rect, ShotImage* out);
void shot_image_free(ShotImage* img);

/* select.c
 * Shows `frozen` fullscreen and lets the user drag out a rectangle, then pick
 * what to do with it. Blocks until the user decides. */
ShotAction shot_select(Display* dpy, XImage* frozen, ShotRect* rect_out);

/* encode.c */
int shot_image_encode_png(const ShotImage* img, uint8_t** data_out, size_t* size_out);
/* Writes JPEG for .jpg/.jpeg paths, PNG otherwise. */
int shot_image_save(const ShotImage* img, const char* path);

/* clipboard.c
 * Takes ownership of the CLIPBOARD selection and serves `data` as `mime` from a
 * forked background process, so the caller is free to exit. */
int shot_clipboard_set(const char* mime, const uint8_t* data, size_t size);

#endif
