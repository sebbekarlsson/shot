#include <shot/shot.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BORDER 2     /* selection outline thickness */
#define GAP 6        /* space between the selection and its label/buttons */
#define PAD 6        /* padding inside labels and buttons */
#define MIN_SIZE 3   /* drags smaller than this count as a stray click */
#define NUM_BUTTONS 3

typedef enum { STATE_IDLE, STATE_DRAGGING, STATE_CHOOSING } State;

typedef struct {
  ShotRect rect;
  const char* text;
  ShotAction action;
} Button;

typedef struct {
  Display* dpy;
  Window win;
  Pixmap bright, dim, back;
  GC gc;
  XFontStruct* font;
  Cursor cross, hand;
  int sw, sh;
  unsigned long col_border, col_bg, col_btn, col_btn_hover, col_text;

  State state;
  int ax, ay, cx, cy; /* drag anchor and current pointer position */
  ShotRect sel;
  Button buttons[NUM_BUTTONS];
  int hover; /* index into buttons, or -1 */
  char label[32];
  ShotRect label_rect;
  ShotRect drawn; /* area covered by the decorations currently on screen */
} Ctx;

static int rect_empty(ShotRect r) {
  return r.width <= 0 || r.height <= 0;
}

static ShotRect rect_union(ShotRect a, ShotRect b) {
  if (rect_empty(a)) return b;
  if (rect_empty(b)) return a;
  int x0 = a.x < b.x ? a.x : b.x;
  int y0 = a.y < b.y ? a.y : b.y;
  int x1 = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
  int y1 = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
  return (ShotRect){x0, y0, x1 - x0, y1 - y0};
}

static ShotRect rect_intersect(ShotRect a, ShotRect b) {
  int x0 = a.x > b.x ? a.x : b.x;
  int y0 = a.y > b.y ? a.y : b.y;
  int x1 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
  int y1 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
  if (x1 <= x0 || y1 <= y0) return (ShotRect){0, 0, 0, 0};
  return (ShotRect){x0, y0, x1 - x0, y1 - y0};
}

static ShotRect rect_grow(ShotRect r, int n) {
  return (ShotRect){r.x - n, r.y - n, r.width + 2 * n, r.height + 2 * n};
}

static int rect_contains(ShotRect r, int x, int y) {
  return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}

static int clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static unsigned long alloc_color(Display* dpy, unsigned short r, unsigned short g,
                                 unsigned short b) {
  XColor c = {.red = r * 257, .green = g * 257, .blue = b * 257, .flags = DoRed | DoGreen | DoBlue};
  if (!XAllocColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &c))
    return WhitePixel(dpy, DefaultScreen(dpy));
  return c.pixel;
}

/* Returns a copy of `src` at roughly 40% brightness. */
static XImage* make_dimmed(XImage* src) {
  XImage* dst = XSubImage(src, 0, 0, src->width, src->height);
  if (!dst) return NULL;

  unsigned long rgb = src->red_mask | src->green_mask | src->blue_mask;
  unsigned long masks[3] = {src->red_mask, src->green_mask, src->blue_mask};
  const uint16_t probe = 1;
  int host_lsb = *(const uint8_t*)&probe == 1;
  int fast = src->bits_per_pixel == 32 && (src->byte_order == LSBFirst) == host_lsb;

  for (int y = 0; y < dst->height; y++) {
    uint32_t* row = (uint32_t*)(dst->data + (size_t)y * dst->bytes_per_line);
    for (int x = 0; x < dst->width; x++) {
      unsigned long p = fast ? row[x] : XGetPixel(dst, x, y);
      unsigned long out = p & ~rgb;
      for (int i = 0; i < 3; i++) {
        unsigned long ch = p & masks[i];
        out |= ((ch >> 1) & masks[i]) - ((ch >> 3) & masks[i]);
      }
      if (fast)
        row[x] = (uint32_t)out;
      else
        XPutPixel(dst, x, y, out);
    }
  }
  return dst;
}

static int text_width(Ctx* c, const char* s) {
  return XTextWidth(c->font, s, (int)strlen(s));
}

static int text_height(Ctx* c) {
  return c->font->ascent + c->font->descent;
}

/* Recomputes the selection rect, label and button placement from the state. */
static void layout(Ctx* c) {
  if (c->state == STATE_IDLE) return;

  if (c->state == STATE_DRAGGING) {
    int x0 = c->ax < c->cx ? c->ax : c->cx;
    int y0 = c->ay < c->cy ? c->ay : c->cy;
    c->sel = (ShotRect){x0, y0, abs(c->cx - c->ax) + 1, abs(c->cy - c->ay) + 1};
  }

  snprintf(c->label, sizeof(c->label), "%d x %d", c->sel.width, c->sel.height);
  int lw = text_width(c, c->label) + 2 * PAD;
  int lh = text_height(c) + 2 * PAD;
  int ly = c->sel.y - GAP - lh;
  if (ly < 0) ly = c->sel.y + GAP;
  c->label_rect = (ShotRect){clamp(c->sel.x, 0, c->sw - lw), ly, lw, lh};

  if (c->state != STATE_CHOOSING) return;

  int bh = text_height(c) + 2 * PAD;
  int total = -GAP;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    c->buttons[i].rect.width = text_width(c, c->buttons[i].text) + 2 * PAD;
    c->buttons[i].rect.height = bh;
    total += c->buttons[i].rect.width + GAP;
  }
  int by = c->sel.y + c->sel.height + GAP;
  if (by + bh > c->sh) by = c->sel.y + c->sel.height - GAP - bh;
  if (by < 0) by = 0;
  int bx = clamp(c->sel.x + c->sel.width - total, 0, c->sw - total);
  for (int i = 0; i < NUM_BUTTONS; i++) {
    c->buttons[i].rect.x = bx;
    c->buttons[i].rect.y = by;
    bx += c->buttons[i].rect.width + GAP;
  }
}

static ShotRect decorations_bbox(Ctx* c) {
  ShotRect r = {0, 0, 0, 0};
  if (c->state == STATE_IDLE) return r;
  r = rect_union(rect_grow(c->sel, BORDER), c->label_rect);
  if (c->state == STATE_CHOOSING)
    for (int i = 0; i < NUM_BUTTONS; i++) r = rect_union(r, c->buttons[i].rect);
  return r;
}

static void draw_text_box(Ctx* c, ShotRect r, const char* text, unsigned long bg) {
  XSetForeground(c->dpy, c->gc, bg);
  XFillRectangle(c->dpy, c->back, c->gc, r.x, r.y, r.width, r.height);
  XSetForeground(c->dpy, c->gc, c->col_text);
  XDrawString(c->dpy, c->back, c->gc, r.x + PAD, r.y + PAD + c->font->ascent, text,
              (int)strlen(text));
}

/* Repaints `damage` into the back buffer and copies it to the window. */
static void redraw(Ctx* c, ShotRect damage) {
  damage = rect_intersect(damage, (ShotRect){0, 0, c->sw, c->sh});
  if (rect_empty(damage)) return;

  XCopyArea(c->dpy, c->dim, c->back, c->gc, damage.x, damage.y, damage.width, damage.height,
            damage.x, damage.y);

  if (c->state != STATE_IDLE) {
    ShotRect lit = rect_intersect(c->sel, damage);
    if (!rect_empty(lit))
      XCopyArea(c->dpy, c->bright, c->back, c->gc, lit.x, lit.y, lit.width, lit.height, lit.x,
                lit.y);

    ShotRect s = c->sel;
    XSetForeground(c->dpy, c->gc, c->col_border);
    XFillRectangle(c->dpy, c->back, c->gc, s.x - BORDER, s.y - BORDER, s.width + 2 * BORDER, BORDER);
    XFillRectangle(c->dpy, c->back, c->gc, s.x - BORDER, s.y + s.height, s.width + 2 * BORDER, BORDER);
    XFillRectangle(c->dpy, c->back, c->gc, s.x - BORDER, s.y, BORDER, s.height);
    XFillRectangle(c->dpy, c->back, c->gc, s.x + s.width, s.y, BORDER, s.height);

    draw_text_box(c, c->label_rect, c->label, c->col_bg);

    if (c->state == STATE_CHOOSING)
      for (int i = 0; i < NUM_BUTTONS; i++)
        draw_text_box(c, c->buttons[i].rect, c->buttons[i].text,
                      i == c->hover ? c->col_btn_hover : c->col_btn);
  }

  XCopyArea(c->dpy, c->back, c->win, c->gc, damage.x, damage.y, damage.width, damage.height,
            damage.x, damage.y);
}

/* Call after any state change: repaints both the old and new decorations. */
static void update(Ctx* c) {
  layout(c);
  ShotRect bbox = decorations_bbox(c);
  redraw(c, rect_union(c->drawn, bbox));
  c->drawn = bbox;
}

static int button_at(Ctx* c, int x, int y) {
  if (c->state != STATE_CHOOSING) return -1;
  for (int i = 0; i < NUM_BUTTONS; i++)
    if (rect_contains(c->buttons[i].rect, x, y)) return i;
  return -1;
}

static void set_hover(Ctx* c, int hover) {
  if (hover == c->hover) return;
  c->hover = hover;
  XChangeActivePointerGrab(c->dpy, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                           hover >= 0 ? c->hand : c->cross, CurrentTime);
  update(c);
}

static int grab_input(Ctx* c) {
  struct timespec ts = {0, 1000 * 1000};
  int pointer = 0, keyboard = 0;
  /* Another client (e.g. the WM handling a hotkey) may briefly hold a grab. */
  for (int i = 0; i < 500 && !(pointer && keyboard); i++) {
    if (!pointer)
      pointer = XGrabPointer(c->dpy, c->win, False,
                             ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                             GrabModeAsync, GrabModeAsync, None, c->cross,
                             CurrentTime) == GrabSuccess;
    if (!keyboard)
      keyboard = XGrabKeyboard(c->dpy, c->win, False, GrabModeAsync, GrabModeAsync,
                               CurrentTime) == GrabSuccess;
    if (!(pointer && keyboard)) nanosleep(&ts, NULL);
  }
  if (!pointer) fprintf(stderr, "shot: could not grab the pointer\n");
  if (!keyboard) fprintf(stderr, "shot: could not grab the keyboard\n");
  return pointer && keyboard ? 0 : -1;
}

static int setup(Ctx* c, Display* dpy, XImage* frozen) {
  int scr = DefaultScreen(dpy);
  Window root = RootWindow(dpy, scr);
  int depth = DefaultDepth(dpy, scr);
  c->dpy = dpy;
  c->sw = frozen->width;
  c->sh = frozen->height;
  c->hover = -1;

  XImage* dimmed = make_dimmed(frozen);
  if (!dimmed) {
    fprintf(stderr, "shot: out of memory\n");
    return -1;
  }

  c->gc = XCreateGC(dpy, root, 0, NULL);
  c->bright = XCreatePixmap(dpy, root, c->sw, c->sh, depth);
  c->dim = XCreatePixmap(dpy, root, c->sw, c->sh, depth);
  c->back = XCreatePixmap(dpy, root, c->sw, c->sh, depth);
  XPutImage(dpy, c->bright, c->gc, frozen, 0, 0, 0, 0, c->sw, c->sh);
  XPutImage(dpy, c->dim, c->gc, dimmed, 0, 0, 0, 0, c->sw, c->sh);
  XCopyArea(dpy, c->dim, c->back, c->gc, 0, 0, c->sw, c->sh, 0, 0);
  XDestroyImage(dimmed);

  c->font = XLoadQueryFont(dpy, "-misc-fixed-bold-r-normal--13-*-*-*-*-*-iso10646-1");
  if (!c->font) c->font = XLoadQueryFont(dpy, "fixed");
  if (!c->font) {
    fprintf(stderr, "shot: could not load a font\n");
    return -1;
  }
  XSetFont(dpy, c->gc, c->font->fid);

  c->col_border = alloc_color(dpy, 0x4a, 0x9e, 0xff);
  c->col_bg = alloc_color(dpy, 0x20, 0x20, 0x20);
  c->col_btn = alloc_color(dpy, 0x30, 0x30, 0x30);
  c->col_btn_hover = alloc_color(dpy, 0x2a, 0x6e, 0xc8);
  c->col_text = alloc_color(dpy, 0xf0, 0xf0, 0xf0);

  c->cross = XCreateFontCursor(dpy, XC_crosshair);
  c->hand = XCreateFontCursor(dpy, XC_hand2);

  c->buttons[0] = (Button){{0}, "Save [s]", SHOT_ACTION_SAVE};
  c->buttons[1] = (Button){{0}, "Copy [c]", SHOT_ACTION_COPY};
  c->buttons[2] = (Button){{0}, "Cancel [Esc]", SHOT_ACTION_CANCEL};

  XSetWindowAttributes wa = {
    .override_redirect = True,
    .background_pixmap = None,
    .cursor = c->cross,
    .event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask |
                  ButtonReleaseMask | PointerMotionMask,
  };
  c->win = XCreateWindow(dpy, root, 0, 0, c->sw, c->sh, 0, CopyFromParent, InputOutput,
                         CopyFromParent, CWOverrideRedirect | CWBackPixmap | CWCursor | CWEventMask,
                         &wa);
  XStoreName(dpy, c->win, "shot");
  XMapRaised(dpy, c->win);

  XEvent ev;
  do XNextEvent(dpy, &ev);
  while (ev.type != MapNotify || ev.xmap.window != c->win);

  return grab_input(c);
}

static void teardown(Ctx* c) {
  Display* dpy = c->dpy;
  XUngrabPointer(dpy, CurrentTime);
  XUngrabKeyboard(dpy, CurrentTime);
  if (c->win) XDestroyWindow(dpy, c->win);
  if (c->cross) XFreeCursor(dpy, c->cross);
  if (c->hand) XFreeCursor(dpy, c->hand);
  if (c->font) XFreeFont(dpy, c->font);
  if (c->bright) XFreePixmap(dpy, c->bright);
  if (c->dim) XFreePixmap(dpy, c->dim);
  if (c->back) XFreePixmap(dpy, c->back);
  if (c->gc) XFreeGC(dpy, c->gc);
  XSync(dpy, False);
}

static ShotAction run(Ctx* c) {
  XEvent ev;
  for (;;) {
    XNextEvent(c->dpy, &ev);
    switch (ev.type) {
      case Expose:
        if (ev.xexpose.count == 0) redraw(c, (ShotRect){0, 0, c->sw, c->sh});
        break;

      case KeyPress: {
        KeySym sym = XLookupKeysym(&ev.xkey, 0);
        if (sym == XK_Escape || sym == XK_q) return SHOT_ACTION_CANCEL;
        if (c->state != STATE_CHOOSING) break;
        if (sym == XK_s || sym == XK_Return || sym == XK_KP_Enter) return SHOT_ACTION_SAVE;
        if (sym == XK_c || sym == XK_y) return SHOT_ACTION_COPY;
        break;
      }

      case ButtonPress: {
        int x = ev.xbutton.x_root, y = ev.xbutton.y_root;
        if (ev.xbutton.button == Button3) return SHOT_ACTION_CANCEL;
        if (ev.xbutton.button != Button1) break;
        int b = button_at(c, x, y);
        if (b >= 0) return c->buttons[b].action;
        /* Clicking anywhere else starts a fresh selection. */
        set_hover(c, -1);
        c->state = STATE_DRAGGING;
        c->ax = c->cx = clamp(x, 0, c->sw - 1);
        c->ay = c->cy = clamp(y, 0, c->sh - 1);
        update(c);
        break;
      }

      case MotionNotify: {
        while (XCheckTypedWindowEvent(c->dpy, c->win, MotionNotify, &ev)) {}
        int x = ev.xmotion.x_root, y = ev.xmotion.y_root;
        if (c->state == STATE_DRAGGING) {
          c->cx = clamp(x, 0, c->sw - 1);
          c->cy = clamp(y, 0, c->sh - 1);
          update(c);
        } else if (c->state == STATE_CHOOSING) {
          set_hover(c, button_at(c, x, y));
        }
        break;
      }

      case ButtonRelease:
        if (ev.xbutton.button != Button1 || c->state != STATE_DRAGGING) break;
        c->state = c->sel.width >= MIN_SIZE && c->sel.height >= MIN_SIZE ? STATE_CHOOSING
                                                                          : STATE_IDLE;
        update(c);
        set_hover(c, button_at(c, ev.xbutton.x_root, ev.xbutton.y_root));
        break;
    }
  }
}

ShotAction shot_select(Display* dpy, XImage* frozen, ShotRect* rect_out) {
  Ctx c;
  memset(&c, 0, sizeof(c));
  ShotAction action = SHOT_ACTION_CANCEL;
  if (setup(&c, dpy, frozen) == 0) {
    action = run(&c);
    *rect_out = c.sel;
  }
  teardown(&c);
  return action;
}
