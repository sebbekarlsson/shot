#include <shot/shot.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

XImage* shot_grab_screen(Display* dpy) {
  Window root = DefaultRootWindow(dpy);
  XWindowAttributes attr;
  if (!XGetWindowAttributes(dpy, root, &attr)) {
    fprintf(stderr, "shot: failed to query root window\n");
    return NULL;
  }
  XImage* img = XGetImage(dpy, root, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
  if (!img) fprintf(stderr, "shot: failed to grab the screen\n");
  return img;
}

typedef struct {
  int shift;
  int bits;
} Channel;

static Channel channel_from_mask(unsigned long mask) {
  Channel c = {0, 0};
  if (!mask) return c;
  c.shift = __builtin_ctzl(mask);
  c.bits = __builtin_popcountl(mask);
  return c;
}

static uint8_t channel_value(unsigned long pixel, unsigned long mask, Channel c) {
  unsigned long v = (pixel & mask) >> c.shift;
  if (c.bits == 8) return (uint8_t)v;
  if (c.bits > 8) return (uint8_t)(v >> (c.bits - 8));
  if (c.bits == 0) return 0;
  return (uint8_t)(v * 255 / ((1ul << c.bits) - 1));
}

int shot_image_from_ximage(const XImage* ximg, ShotRect rect, ShotImage* out) {
  if (rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0 ||
      rect.x + rect.width > ximg->width || rect.y + rect.height > ximg->height) {
    fprintf(stderr, "shot: capture rectangle out of bounds\n");
    return -1;
  }

  out->width = rect.width;
  out->height = rect.height;
  out->pixels = malloc((size_t)rect.width * rect.height * 3);
  if (!out->pixels) {
    fprintf(stderr, "shot: out of memory\n");
    return -1;
  }

  Channel r = channel_from_mask(ximg->red_mask);
  Channel g = channel_from_mask(ximg->green_mask);
  Channel b = channel_from_mask(ximg->blue_mask);

  const uint16_t probe = 1;
  int host_lsb = *(const uint8_t*)&probe == 1;
  int fast = ximg->bits_per_pixel == 32 && (ximg->byte_order == LSBFirst) == host_lsb;

  uint8_t* dst = out->pixels;
  for (int y = 0; y < rect.height; y++) {
    const uint32_t* row =
      (const uint32_t*)(ximg->data + (size_t)(rect.y + y) * ximg->bytes_per_line) + rect.x;
    for (int x = 0; x < rect.width; x++) {
      unsigned long p = fast ? row[x] : XGetPixel((XImage*)ximg, rect.x + x, rect.y + y);
      *dst++ = channel_value(p, ximg->red_mask, r);
      *dst++ = channel_value(p, ximg->green_mask, g);
      *dst++ = channel_value(p, ximg->blue_mask, b);
    }
  }
  return 0;
}

void shot_image_free(ShotImage* img) {
  free(img->pixels);
  img->pixels = NULL;
  img->width = img->height = 0;
}
