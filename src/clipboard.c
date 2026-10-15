#include <shot/shot.h>
#include <X11/Xatom.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_TRANSFERS 16

/* An in-progress INCR transfer to one requestor (ICCCM 2.7.2). */
typedef struct {
  Window requestor;
  Atom property;
  size_t offset;
} Transfer;

typedef struct {
  Display* dpy;
  Window win;
  Time owned_at;
  Atom clipboard, targets, timestamp, incr, mime;
  const uint8_t* data;
  size_t size;
  size_t chunk;
  Transfer transfers[MAX_TRANSFERS];
  int ntransfers;
} Server;

/* Requestors can vanish mid-transfer; that must not kill the server. */
static int ignore_x_errors(Display* dpy, XErrorEvent* ev) {
  (void)dpy;
  (void)ev;
  return 0;
}

static int start_incr(Server* s, Window requestor, Atom property) {
  if (s->ntransfers == MAX_TRANSFERS) return -1;
  XSelectInput(s->dpy, requestor, PropertyChangeMask);
  long size = (long)s->size;
  XChangeProperty(s->dpy, requestor, property, s->incr, 32, PropModeReplace,
                  (unsigned char*)&size, 1);
  s->transfers[s->ntransfers++] = (Transfer){requestor, property, 0};
  return 0;
}

static void continue_incr(Server* s, const XPropertyEvent* ev) {
  if (ev->state != PropertyDelete) return;
  for (int i = 0; i < s->ntransfers; i++) {
    Transfer* t = &s->transfers[i];
    if (t->requestor != ev->window || t->property != ev->atom) continue;

    size_t n = s->size - t->offset;
    if (n > s->chunk) n = s->chunk;
    XChangeProperty(s->dpy, t->requestor, t->property, s->mime, 8, PropModeReplace,
                    s->data + t->offset, (int)n);
    t->offset += n;
    /* A zero-length write tells the requestor the transfer is complete. */
    if (n == 0) {
      XSelectInput(s->dpy, t->requestor, NoEventMask);
      *t = s->transfers[--s->ntransfers];
    }
    XFlush(s->dpy);
    return;
  }
}

static void handle_request(Server* s, const XSelectionRequestEvent* req) {
  XSelectionEvent resp = {
    .type = SelectionNotify,
    .display = req->display,
    .requestor = req->requestor,
    .selection = req->selection,
    .target = req->target,
    .property = None,
    .time = req->time,
  };
  /* Obsolete clients pass None and expect the target name to be used. */
  Atom prop = req->property != None ? req->property : req->target;

  if (req->selection == s->clipboard) {
    if (req->target == s->targets) {
      Atom list[] = {s->targets, s->timestamp, s->mime};
      XChangeProperty(s->dpy, req->requestor, prop, XA_ATOM, 32, PropModeReplace,
                      (unsigned char*)list, sizeof(list) / sizeof(list[0]));
      resp.property = prop;
    } else if (req->target == s->timestamp) {
      long t = (long)s->owned_at;
      XChangeProperty(s->dpy, req->requestor, prop, XA_INTEGER, 32, PropModeReplace,
                      (unsigned char*)&t, 1);
      resp.property = prop;
    } else if (req->target == s->mime) {
      if (s->size <= s->chunk) {
        XChangeProperty(s->dpy, req->requestor, prop, s->mime, 8, PropModeReplace, s->data,
                        (int)s->size);
        resp.property = prop;
      } else if (start_incr(s, req->requestor, prop) == 0) {
        resp.property = prop;
      }
    }
  }

  XSendEvent(s->dpy, req->requestor, False, NoEventMask, (XEvent*)&resp);
  XFlush(s->dpy);
}

/* Runs in the forked child. Reports ownership success through `status_fd`,
 * then serves requests until another client takes the clipboard. */
static int serve(const char* mime, const uint8_t* data, size_t size, int status_fd) {
  char ok = 0;
  Server s = {.data = data, .size = size};
  s.dpy = XOpenDisplay(NULL);
  if (!s.dpy) {
    write(status_fd, &ok, 1);
    return 1;
  }
  XSetErrorHandler(ignore_x_errors);

  s.clipboard = XInternAtom(s.dpy, "CLIPBOARD", False);
  s.targets = XInternAtom(s.dpy, "TARGETS", False);
  s.timestamp = XInternAtom(s.dpy, "TIMESTAMP", False);
  s.incr = XInternAtom(s.dpy, "INCR", False);
  s.mime = XInternAtom(s.dpy, mime, False);

  long max_req = XExtendedMaxRequestSize(s.dpy);
  if (max_req == 0) max_req = XMaxRequestSize(s.dpy);
  s.chunk = (size_t)max_req * 4 - 1024;
  if (s.chunk > 1024 * 1024) s.chunk = 1024 * 1024;

  s.win = XCreateSimpleWindow(s.dpy, DefaultRootWindow(s.dpy), 0, 0, 1, 1, 0, 0, 0);
  XSelectInput(s.dpy, s.win, PropertyChangeMask);

  /* ICCCM forbids CurrentTime for ownership; get a real server timestamp. */
  Atom stamp = XInternAtom(s.dpy, "SHOT_TIMESTAMP", False);
  XChangeProperty(s.dpy, s.win, stamp, XA_STRING, 8, PropModeAppend, NULL, 0);
  XEvent ev;
  XWindowEvent(s.dpy, s.win, PropertyChangeMask, &ev);
  s.owned_at = ev.xproperty.time;

  XSetSelectionOwner(s.dpy, s.clipboard, s.win, s.owned_at);
  ok = XGetSelectionOwner(s.dpy, s.clipboard) == s.win;
  write(status_fd, &ok, 1);
  close(status_fd);
  if (!ok) return 1;

  int lost = 0;
  while (!lost || s.ntransfers > 0) {
    XNextEvent(s.dpy, &ev);
    switch (ev.type) {
      case SelectionRequest:
        handle_request(&s, &ev.xselectionrequest);
        break;
      case SelectionClear:
        if (ev.xselectionclear.selection == s.clipboard) lost = 1;
        break;
      case PropertyNotify:
        continue_incr(&s, &ev.xproperty);
        break;
    }
  }
  XCloseDisplay(s.dpy);
  return 0;
}

int shot_clipboard_set(const char* mime, const uint8_t* data, size_t size) {
  int fds[2];
  if (pipe(fds) != 0) {
    perror("shot: pipe");
    return -1;
  }
  fflush(NULL);

  pid_t pid = fork();
  if (pid < 0) {
    perror("shot: fork");
    close(fds[0]);
    close(fds[1]);
    return -1;
  }

  if (pid == 0) {
    close(fds[0]);
    setsid();
    /* Detach from the terminal and any pipes the caller's shell is waiting on. */
    int null = open("/dev/null", O_RDWR);
    if (null >= 0) {
      dup2(null, STDIN_FILENO);
      dup2(null, STDOUT_FILENO);
      dup2(null, STDERR_FILENO);
      if (null > STDERR_FILENO) close(null);
    }
    _exit(serve(mime, data, size, fds[1]));
  }

  close(fds[1]);
  char ok = 0;
  ssize_t n = read(fds[0], &ok, 1);
  close(fds[0]);
  if (n != 1 || !ok) {
    fprintf(stderr, "shot: failed to take ownership of the clipboard\n");
    return -1;
  }
  return 0;
}
