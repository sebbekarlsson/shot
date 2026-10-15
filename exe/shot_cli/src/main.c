#include <shot/shot.h>
#include <X11/Xutil.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void usage(const char* prog) {
  fprintf(stderr,
          "usage: %s [-o PATH]\n"
          "\n"
          "Drag to select a region, then choose:\n"
          "  s / Enter   save to disk\n"
          "  c           copy to clipboard (image/png)\n"
          "  Esc / q     cancel (right click also cancels)\n"
          "\n"
          "  -o PATH     where to save (.png, .jpg or .jpeg)\n"
          "              default: ~/Pictures/shot_<timestamp>.png, or the current\n"
          "              directory if ~/Pictures does not exist\n",
          prog);
}

static int is_dir(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void default_path(char* out, size_t size) {
  char dir[PATH_MAX] = ".";
  const char* home = getenv("HOME");
  if (home) {
    snprintf(dir, sizeof(dir), "%s/Pictures", home);
    if (!is_dir(dir)) snprintf(dir, sizeof(dir), ".");
  }

  char stamp[64];
  time_t now = time(NULL);
  strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", localtime(&now));

  snprintf(out, size, "%s/shot_%s.png", dir, stamp);
  for (int i = 1; access(out, F_OK) == 0; i++)
    snprintf(out, size, "%s/shot_%s_%d.png", dir, stamp, i);
}

int main(int argc, char* argv[]) {
  const char* out_path = NULL;
  int opt;
  while ((opt = getopt(argc, argv, "o:h")) != -1) {
    switch (opt) {
      case 'o': out_path = optarg; break;
      case 'h': usage(argv[0]); return 0;
      default: usage(argv[0]); return 2;
    }
  }

  Display* dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fprintf(stderr, "shot: cannot open display\n");
    return 1;
  }

  XImage* frozen = shot_grab_screen(dpy);
  if (!frozen) {
    XCloseDisplay(dpy);
    return 1;
  }

  ShotRect rect;
  ShotAction action = shot_select(dpy, frozen, &rect);

  ShotImage img = {0};
  int rc = action == SHOT_ACTION_CANCEL ? 1 : 0;
  if (action != SHOT_ACTION_CANCEL && shot_image_from_ximage(frozen, rect, &img) != 0) rc = 1;
  XDestroyImage(frozen);
  XCloseDisplay(dpy);
  if (rc != 0) return rc;

  if (action == SHOT_ACTION_SAVE) {
    char path[PATH_MAX];
    if (!out_path) {
      default_path(path, sizeof(path));
      out_path = path;
    }
    if (shot_image_save(&img, out_path) == 0)
      printf("%s\n", out_path);
    else
      rc = 1;
  } else {
    uint8_t* png = NULL;
    size_t size = 0;
    if (shot_image_encode_png(&img, &png, &size) == 0 &&
        shot_clipboard_set("image/png", png, size) == 0)
      fprintf(stderr, "shot: copied %dx%d to clipboard\n", img.width, img.height);
    else
      rc = 1;
    free(png);
  }

  shot_image_free(&img);
  return rc;
}
