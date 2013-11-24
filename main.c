/**
 * X11 Input Device Detection and Window Creation
 * Copyright (C) 2006  Morten Hustveit
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <fcntl.h>
#include <pwd.h>
#include <shadow.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xinerama.h>
#include <X11/keysym.h>

#include <GL/glx.h>

#include "lock.h"

extern char** environ;

static Display* display = 0;
static Window window;
static XVisualInfo* visual;
static XIM xim;
static XIC xic;

XineramaScreenInfo* screens;
int screen_count = 0;

static char* get_user_name();
static char* get_host_name();
static Bool wait_for_map_notify(Display*, XEvent* event, char* arg);

char* user_name;
char* host_name;
char* password_hash;

void get_password_hash(void) {
  struct passwd* p;
  char* passkey_path;
  FILE* passkey_file;

  p = getpwnam(user_name);

  if (!p) errx(EXIT_FAILURE, "getpwnam failed for '%s'", user_name);

  if (-1 == asprintf(&passkey_path, "%s/.cantera/lock-passkey", p->pw_dir))
    err(EXIT_FAILURE, "asprintf failed");

  if (NULL != (passkey_file = fopen(passkey_path, "r"))) {
    size_t i, password_hash_alloc = 4096;

    password_hash = malloc(password_hash_alloc);

    if (!fgets(password_hash, password_hash_alloc, passkey_file)) {
      if (ferror(passkey_file))
        errx(EXIT_FAILURE, "Error reading '%s': %s", passkey_path,
             strerror(errno));
      else
        errx(EXIT_FAILURE, "Passkey file '%s' appears to be empty",
             passkey_path);
    }

    i = strlen(password_hash);

    // Remove trailing whitespace, usually one LF.
    while (i && isspace(password_hash[i - 1]))
      password_hash[--i] = 0;

    return;
  } else if (errno != ENOENT)
    err(EXIT_FAILURE, "fopen failed");

  password_hash = p->pw_passwd;

  if (strcmp(password_hash, "x")) return;

}

static void attempt_grab(void) {
  static int pointer_grabbed, keyboard_grabbed;

  // Grabs may fail if keys are being pressed while the program is starting, so
  // we keep retrying until success.

  if (!pointer_grabbed &&
      GrabSuccess ==
          XGrabPointer(display, window, True,
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                       GrabModeAsync, GrabModeAsync, window, None, CurrentTime))
    pointer_grabbed = 1;

  if (!keyboard_grabbed &&
      GrabSuccess == XGrabKeyboard(display, DefaultRootWindow(display), True,
                                   GrabModeSync, GrabModeAsync, CurrentTime))
    keyboard_grabbed = 1;
}

int main(int argc, char** argv) {
  int width, height;
  int i;
  int fd;
  const char* display_name;

  display_name = getenv("DISPLAY");

  display = XOpenDisplay(display_name);

  // This program may be installed as setuid root, so we don't want the
  // environment to affect the behavior of this program in unexpected,
  // potentially insecure, ways.
  environ = 0;

  // Move out of the way so that this process never holds up umount.
  if (-1 == chdir("/"))
    err(EXIT_FAILURE, "Failed to switch directory to the root directory");

  // SysRq+F and runaway processes can activate the OOM killer, which may very
  // well kill this process.  This is, of course, very bad for a screen locking
  // program, so we try to tell the OOM killer to kill us last.
  if (-1 != (fd = open("/proc/self/oom_adj", O_WRONLY))) {
    write(fd, "-17", 3);
    close(fd);
  }

  user_name = get_user_name();
  host_name = get_host_name();

  get_password_hash();

  // Drop super-user privileges if we have to.
  if (0 == geteuid()) {
    setgid(getuid());
    setuid(getuid());
  }

  if (!display) errx(EXIT_FAILURE, "Failed to open display %s", display_name);

  if (!glXQueryExtension(display, 0, 0))
    errx(EXIT_FAILURE, "No GLX extension present");

  int attributes[] = { GLX_RGBA, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1,
                       GLX_BLUE_SIZE, 1, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16,
                       None };

  visual = glXChooseVisual(display, DefaultScreen(display), attributes);

  if (!visual) errx(EXIT_FAILURE, "glXChooseVisual failed");

  XWindowAttributes root_window_attr;

  XGetWindowAttributes(display, RootWindow(display, DefaultScreen(display)),
                       &root_window_attr);

  GLXContext glx_context = glXCreateContext(display, visual, 0, GL_TRUE);

  if (!glx_context) errx(EXIT_FAILURE, "Failed creating OpenGL context");

  Colormap color_map = XCreateColormap(
      display, RootWindow(display, visual->screen), visual->visual, AllocNone);

  Pixmap mask = XCreatePixmap(display, XRootWindow(display, 0), 1, 1, 1);

  XGCValues xgc;

  xgc.function = GXclear;

  GC gc = XCreateGC(display, mask, GCFunction, &xgc);

  XFillRectangle(display, mask, gc, 0, 0, 1, 1);

  XColor color;

  color.pixel = 0;
  color.red = 0;
  color.flags = 4;

  Cursor cursor =
      XCreatePixmapCursor(display, mask, mask, &color, &color, 0, 0);

  XFreePixmap(display, mask);

  XFreeGC(display, gc);

  XSetWindowAttributes attr;

  attr.colormap = color_map;
  attr.border_pixel = 0;
  attr.event_mask = KeyPressMask | VisibilityChangeMask | ExposureMask |
                    StructureNotifyMask | FocusChangeMask;
  attr.cursor = cursor;

  attr.override_redirect = True;

  width = root_window_attr.width;
  height = root_window_attr.height;

  window = XCreateWindow(
      display, RootWindow(display, visual->screen), 0, 0, width, height, 0,
      visual->depth, InputOutput, visual->visual,
      CWOverrideRedirect | CWCursor | CWColormap | CWEventMask, &attr);
  XMapRaised(display, window);

  attempt_grab();

  XSetInputFocus(display, window, RevertToParent, CurrentTime);

  XFlush(display);

  char* p;

  if ((p = XSetLocaleModifiers("")) && *p) xim = XOpenIM(display, 0, 0, 0);

  if (!xim && (p = XSetLocaleModifiers("@im=none")) && *p)
    xim = XOpenIM(display, 0, 0, 0);

  if (!xim) errx(EXIT_FAILURE, "Failed to open X Input Method");

  xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                  XNClientWindow, window, XNFocusWindow, window, NULL);

  if (!xic) errx(EXIT_FAILURE, "Failed to create X Input Context");

  XEvent event;

  XIfEvent(display, &event, wait_for_map_notify, (char*)window);

  if (!glXMakeCurrent(display, window, glx_context))
    errx(EXIT_FAILURE, "glXMakeCurrent returned false");

  if (XineramaQueryExtension(display, &i, &i) && XineramaIsActive(display))
    screens = XineramaQueryScreens(display, &screen_count);

  if (!screen_count) {
    screen_count = 1;
    screens = malloc(sizeof(XineramaScreenInfo) * 1);
    screens[0].x_org = 0;
    screens[0].y_org = 0;
    screens[0].width = root_window_attr.width;
    screens[0].height = root_window_attr.height;
  }

  LOCK_init();

  int done = 0;

  struct timeval start;
  gettimeofday(&start, 0);

  while (!done) {
    struct timeval now;
    double delta_time;

    gettimeofday(&now, 0);

    while (now.tv_sec < start.tv_sec)
      now.tv_sec += 24 * 60 * 60;

    while (XPending(display)) {
      XNextEvent(display, &event);

      switch (event.type) {
        case KeyPress: {
          char text[32];
          Status status;

          KeySym key_sym;
          int len = Xutf8LookupString(xic, &event.xkey, text, sizeof(text) - 1,
                                      &key_sym, &status);
          text[len] = 0;

          LOCK_handle_key(key_sym, text);
        } break;

        case ConfigureNotify:
          width = event.xconfigure.width;
          height = event.xconfigure.height;

          glViewport(0, 0, event.xconfigure.width, event.xconfigure.height);

          break;

        case FocusOut:
          // If keyboard grabs have been unsuccessful so far, a FocusOut event
          // may occur.  If so, we change the focus right back.
          XSetInputFocus(display, window, RevertToParent, CurrentTime);

          break;

        case VisibilityNotify:
          if (event.xvisibility.state != VisibilityUnobscured)
            XRaiseWindow(display, window);

          break;
      }
    }

    delta_time =
        (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) * 1.0e-6;

    start = now;

    LOCK_process_frame(width, height, delta_time);

    glXSwapBuffers(display, window);

    attempt_grab();
  }

  return EXIT_SUCCESS;
}

static char* get_user_name() {
  char* result = 0;
  uid_t euid;
  struct passwd* pwent;

  euid = getuid();

  while (0 != (pwent = getpwent())) {
    if (pwent->pw_uid == euid) {
      result = strdup(pwent->pw_name);

      break;
    }
  }

  endpwent();

  return result;
}

static char* get_host_name() {
  static char host_name[32];

  gethostname(host_name, sizeof(host_name));
  host_name[sizeof(host_name) - 1] = 0;

  return host_name;
}

static Bool wait_for_map_notify(Display* display, XEvent* event, char* arg) {
  return (event->type == MapNotify) && (event->xmap.window == (Window) arg);
}
