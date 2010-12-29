#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <sched.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>
#include <wchar.h>

#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>

#include "array.h"
#include "common.h"
#include "font.h"
#include "globals.h"
#include "menu.h"
#include "tree.h"

#define PARTIAL_REPAINT 1

extern char** environ;

struct tree* config;
int inotify_fd = -1;

int xskips[] = { 1, 1 };
int yskips[] = { 1, 1 };
int font_sizes[] = { 12, 36 };

unsigned int palette[] =
{
  /* ANSI colors */
  0xff000000, 0xff1818c2, 0xff18c218, 0xff18c2c2,
  0xffc21818, 0xffc218c2, 0xffc2c218, 0xffc2c2c2,
  0xff686868, 0xff7474ff, 0xff54ff54, 0xff54ffff,
  0xffff5454, 0xffff54ff, 0xffffff54, 0xffffffff,

  0xffdddddd, /* 16 = lighter gray */
  0xff9090ff, /* 17 = lighter blue */
  0xff484848, /* 18 = darker gray */
  0x3f000000, /* 19 = semitransparent black */
};

Cursor menu_cursor;

XRenderColor xrpalette[sizeof(palette) / sizeof(palette[0])];
Picture picpalette[sizeof(palette) / sizeof(palette[0])];

struct
{
  int width, height;
  Window window;
} terminal_list;

struct screen *screens;
int screen_count = 0;

void run_command(int fd, const char* command, const char* arg);
void init_ximage(XImage* image, int width, int height, void* data);

void
destroy_notify(Window xwindow);

Window window;

struct window_list windows;

struct window*
find_window(Window window)
{
  size_t i;

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      if(ARRAY_GET(&windows, i).xwindow == window)
        return &ARRAY_GET(&windows, i);
    }

  return 0;
}

Window
find_xwindow(terminal* t)
{
  const struct window* w;
  size_t i;

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      w = &ARRAY_GET(&windows, i);

      if(w->desktop == t)
        return w->xwindow;
    }

  return 0;
}

Display* display;
int screenidx;
Screen* screen;
Visual* visual;
XVisualInfo visual_template;
XVisualInfo* visual_info;
Window root_window;
Picture root_picture;
XWindowAttributes root_window_attr;

int damage_eventbase;
int damage_errorbase;

Atom xa_utf8_string;
Atom xa_compound_text;
Atom xa_targets;
Atom xa_net_wm_icon;
Atom xa_net_wm_pid;
Atom xa_net_wm_user_time;
Atom xa_net_wm_user_time_window;
Atom xa_wm_state;
Atom xa_wm_transient_for;
Atom xa_wm_protocols;
Atom xa_wm_delete_window;

XRenderPictFormat* xrenderpictformat;
XRenderPictFormat* argb32pictformat;
XRenderPictFormat* a8pictformat;

GlyphSet alpha_glyphs[2];
Picture blend_90;

XIM xim = 0;
XIC xic;
int xfd;
int ctrl_pressed = 0;
int mod1_pressed = 0;
int super_pressed = 0;
int shift_pressed = 0;
int button1_pressed = 0;

int desktops_visible = 0;

struct screen* current_screen;

#define my_isprint(c) (isprint((c)) || ((c) >= 0x80))

struct window *
create_notify (XCreateWindowEvent* cwe);

void
clear()
{
  unsigned int i;

  for(i = 0; i < screen_count; ++i)
    {
      if(screens[i].at->mode == mode_menu)
        {
          XClearArea(display, screens[i].window, 0, 0,
                     screens[i].width, screens[i].height, True);
        }
    }
}

static void
swap_terminals(unsigned int a, unsigned int b)
{
  terminal* term_a;
  terminal* term_b;
  terminal tmp;
  unsigned int i;

  term_a = &current_screen->terminals[a];
  term_b = &current_screen->terminals[b];

  tmp = *term_a;
  *term_a = *term_b;
  *term_b = tmp;

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      struct window* w;

      w = &ARRAY_GET(&windows, i);

      if(w->screen != current_screen)
        continue;

      if(w->desktop == term_a)
        w->desktop = term_b;
      else if(w->desktop == term_b)
        w->desktop = term_a;
    }
}

static void
history_reset(struct screen *s, int terminal)
{
  s->history_size = 1;
  s->history[0] = terminal;
}

static void
history_add(struct screen *s, int terminal)
{
  if (s->history_size == TERMINAL_COUNT)
    {
      --s->history_size;
      memmove (s->history, s->history + 1, s->history_size * sizeof(*s->history));
    }

  s->history[s->history_size++] = terminal;
}

void
composite_init_window(struct window *w)
{
  XWindowAttributes attr;
  XRenderPictFormat *format;
  XRenderPictureAttributes pa;

  if (w->xpicture)
    return;

  XGetWindowAttributes (display, w->xwindow, &attr);

  format = XRenderFindVisualFormat(display, attr.visual);

  if (!format)
    return;

  memset(&pa, 0, sizeof(pa));
  pa.subwindow_mode = IncludeInferiors;

  w->xpicture = XRenderCreatePicture(display, w->xwindow, format, CPSubwindowMode, &pa);
  w->xdamage = XDamageCreate(display, w->xwindow, XDamageReportNonEmpty);
}

void
composite_destroy_window(struct window *w)
{
  if (!w->xpicture)
    return;

  XDamageDestroy(display, w->xdamage);
  XRenderFreePicture(display, w->xpicture);

  w->xdamage = 0;
  w->xpicture = 0;
}

static void paint(Window window, int x, int y, int width, int height)
{
  struct window *w;

  w = find_window (window);

  if (!w || w->type != wm_window_type_wm)
    return;

  if (w->screen->at->mode == mode_menu)
    menu_draw(w->screen);

  XRenderComposite(display, PictOpSrc,
                   w->screen->root_buffer,
                   None,
                   w->screen->root_picture,
                   x, y,
                   0, 0,
                   x, y, width, height);
}

int get_int_property(Window window, Atom property, int* result)
{
  Atom type;
  int format;
  unsigned long nitems;
  unsigned long bytes_after;
  uint32_t* prop;

  if(Success != XGetWindowProperty(display, window, property, 0, sizeof (*result) / 4, False,
                                   AnyPropertyType, &type, &format, &nitems,
                                   &bytes_after, (unsigned char**) &prop))
    return -1;

  if(!prop)
    return -1;

  *result = *prop;

  XFree(prop);

  return 0;
}

int get_time_property(Window window, Atom property, Time* result)
{
  Atom type;
  int format;
  unsigned long nitems;
  unsigned long bytes_after;
  uint32_t* prop;

  if(Success != XGetWindowProperty(display, window, property, 0, sizeof (*result) / 4, False,
                                   AnyPropertyType, &type, &format, &nitems,
                                   &bytes_after, (unsigned char**) &prop))
    return -1;

  if(!prop)
    return -1;

  *result = *prop;

  XFree(prop);

  return 0;
}

static int first_available_terminal(struct screen* screen)
{
  int i;

  if(screen->at->mode == mode_menu)
    return screen->active_terminal;

  for(i = 0; i < TERMINAL_COUNT; ++i)
  {
    if(screen->terminals[i].mode == mode_menu)
      return i;
  }

  return -1;
}

static void set_map_state(Window window, int state)
{
  unsigned long data[2];
  data[0] = state;
  data[1] = None;

  XChangeProperty(display, window, xa_wm_state, xa_wm_state, 32,
                  PropModeReplace, (unsigned char*) data, 2);
}

static void grab_thumbnail(struct window* w)
{
  Atom type;
  int format;
  int result;
  unsigned long nitems;
  unsigned long bytes_after;
  unsigned char* prop;

  int thumb_width, thumb_height;

  if (!w->desktop)
    return;

  menu_thumbnail_dimensions (w->screen, &thumb_width, &thumb_height, 0);

  if(w->desktop->thumbnail)
    return;

  result = XGetWindowProperty(display, w->xwindow, xa_net_wm_icon, 0, 0, False,
                              AnyPropertyType, &type, &format, &nitems, &bytes_after,
                              &prop);

  if(result != Success)
    {
      fprintf(stderr, "XGetWindowProperty failed\n");

      return;
    }

  if(prop)
    XFree(prop);

  result = XGetWindowProperty(display, w->xwindow, xa_net_wm_icon, 0, bytes_after, False,
                              AnyPropertyType, &type, &format, &nitems, &bytes_after,
                              &prop);

  if(prop && format == 32)
    {
      XImage temp_image;
      Pixmap temp_pixmap;
      GC temp_gc;

      unsigned long *buf, *end;
      unsigned int width, height;
      unsigned int i;

      unsigned long *icon_start, *best;
      unsigned int best_area = 0;

      union
        {
          uint32_t rgba;
          struct
            {
              unsigned char r, g, b, a;
            } c;
        } *colors;

      buf = (unsigned long*) prop;
      end = buf + nitems;

      while (buf < end)
      {
        icon_start = buf;

        width = *buf++;
        height = *buf++;
        buf += width * height;

        if (width >= thumb_width || height >= thumb_height)
          continue;

        if (width * height > best_area)
        {
          best_area = width * height;
          best = icon_start;
        }
      }

      buf = best ? best : (unsigned long *) prop;

      width = *buf++;
      height = *buf++;

      colors = malloc (sizeof(*colors) * width * height);

      for (i = 0; i < width * height; ++i)
        {
          colors[i].rgba = *buf++;

          colors[i].c.r = (colors[i].c.r * colors[i].c.a) / 255;
          colors[i].c.g = (colors[i].c.g * colors[i].c.a) / 255;
          colors[i].c.b = (colors[i].c.b * colors[i].c.a) / 255;
        }

      init_ximage(&temp_image, width, height, colors);

      temp_pixmap = XCreatePixmap(display, w->screen->window, thumb_width, thumb_height, format);

      temp_gc = XCreateGC(display, temp_pixmap, 0, 0);
      XFillRectangle(display, temp_pixmap, temp_gc, 0, 0, thumb_width, thumb_height);
      XPutImage(display, temp_pixmap, temp_gc, &temp_image, 0, 0,
                thumb_width / 2 - width / 2, thumb_height / 2 - height / 2, width, height);
      XFreeGC(display, temp_gc);

      w->desktop->thumbnail
        = XRenderCreatePicture(display, temp_pixmap,
                               XRenderFindStandardFormat(display, PictStandardARGB32),
                               0, 0);

      XFreePixmap(display, temp_pixmap);

      XFree(prop);

      free(colors);
    }
}

static int
terminal_list_height(struct screen *screen)
{
  int thumb_height, thumb_margin;

  menu_thumbnail_dimensions(&screens[0], 0, &thumb_height, &thumb_margin);

  return 2 * thumb_height + 3 * thumb_margin + yskips[SMALL];
}

static void
map_window (struct window *w)
{
  set_map_state(w->xwindow, 1);
  XMapRaised(display, w->xwindow);
  XMoveWindow(display, w->xwindow, w->target.x, w->target.y);

  w->flags &= ~WINDOW_WANT_UNMAPPED;
  w->flags |= WINDOW_IS_MAPPED;
}

static void
unmap_window (struct window *w)
{
  XMoveWindow(display, w->xwindow, root_window_attr.width, 0);
  set_map_state(w->xwindow, 0);

  w->flags &= ~(WINDOW_WANT_UNMAPPED | WINDOW_IS_MAPPED);
}

static void
set_focus(struct screen* screen, terminal* t, Time when)
{
  Window focus;
  struct window* w;
  size_t i;

  focus = screen->window;

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      w = &ARRAY_GET(&windows, i);

      if (w->screen != screen || !(w->flags & WINDOW_IS_MAPPED))
        continue;

      w->flags |= WINDOW_WANT_UNMAPPED;
    }

  switch (t->mode)
    {
    case mode_menu:

      map_window (screen->background);

      for(i = 0; i < ARRAY_COUNT(&windows); ++i)
        {
          w = &ARRAY_GET(&windows, i);

          if(w->screen == screen && w->type == wm_window_type_desktop)
            {
              map_window (w);

              break;
            }
        }

      break;

    case mode_x11:

      screen->background->flags |= WINDOW_WANT_UNMAPPED;

      for(i = 0; i < ARRAY_COUNT(&windows); ++i)
        {
          w = &ARRAY_GET(&windows, i);

          if(w->type == wm_window_type_normal && w->desktop == t)
            {
              focus = w->xwindow;
              map_window (w);

              break;
            }
        }

      for(i = 0; i < ARRAY_COUNT(&windows); ++i)
        {
          w = &ARRAY_GET(&windows, i);

          if(w->type != wm_window_type_normal && w->desktop == t)
            {
              focus = w->xwindow;
              map_window (w);
            }
        }

      break;

    default:;

            fprintf (stderr, "Some other mode\n");
    }

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      w = &ARRAY_GET(&windows, i);

      if(w->type == wm_window_type_dock)
        map_window (w);
    }

  if(screen == current_screen && focus)
    XSetInputFocus(display, focus, RevertToPointerRoot, when);
}

static void
set_active_terminal(struct screen* screen, unsigned int terminal_index, Time when)
{
  struct window* w;
  int i;

  if(terminal_index == screen->active_terminal)
    return;

  set_focus(screen, &screen->terminals[terminal_index], when);

  screen->active_terminal = terminal_index;
  screen->at = &screen->terminals[terminal_index];

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      w = &ARRAY_GET(&windows, i);

      if (w->flags & WINDOW_WANT_UNMAPPED)
        unmap_window(w);
    }
}

pid_t launch(const char* command, Time when)
{
  pid_t pid = fork();

  if(pid == -1)
    return 0;

  if(!pid)
  {
    char* args[4];
    char buf[32];

    setsid();

    sprintf(buf, "%llu", (unsigned long long int) when);
    setenv("DESKTOP_STARTUP_ID", buf, 1);

    sprintf(buf, ".cantera/bash-history-%02d", current_screen->active_terminal);
    setenv("HISTFILE", buf, 1);

    sprintf(buf, ".cantera/session-%02d", current_screen->active_terminal);
    setenv("SESSION_PATH", buf, 1);

    args[0] = "/bin/sh";
    args[1] = "-c";
    asprintf(&args[2], "%s", command);
    args[3] = 0;

    execve(args[0], args, environ);

    exit(EXIT_FAILURE);
  }

  if(current_screen->at->mode == mode_menu)
    {
      current_screen->at->pid = pid;
      current_screen->at->startup = when;
    }

  return pid;
}

static void grab_keys()
{
  static const int global_modifiers[] = { 0, LockMask, LockMask | Mod2Mask, Mod2Mask };
  int i, f, gmod;

  XGrabKey(display, 129, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* music */
  XGrabKey(display, 160, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* mute */
  XGrabKey(display, 161, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* calculator */
  XGrabKey(display, 174, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* volume down */
  XGrabKey(display, 176, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* volume up */

  XGrabKey(display, XKeysymToKeycode(display, XK_Alt_L), Mod4Mask, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Alt_R), Mod4Mask, root_window, False, GrabModeAsync, GrabModeAsync);

  XGrabKey(display, XKeysymToKeycode(display, XK_Super_L), AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Super_R), AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync);

  for(i = 0; i < sizeof(global_modifiers) / sizeof(global_modifiers[0]); ++i)
  {
    gmod = global_modifiers[i];

    for(f = 0; f < 9; ++f)
      XGrabKey(display, XKeysymToKeycode(display, XK_1 + f), Mod4Mask, root_window, False, GrabModeAsync, GrabModeAsync);

    XGrabKey(display, XKeysymToKeycode(display, XK_Left), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Right), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Up), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Down), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);

    for(f = 0; f < 12; ++f)
    {
      XGrabKey(display, XKeysymToKeycode(display, XK_F1 + f), ControlMask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
      XGrabKey(display, XKeysymToKeycode(display, XK_F1 + f), Mod4Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    }

    XGrabKey(display, XKeysymToKeycode(display, XK_Escape), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_F4), Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
  }
}

static int xerror_handler(Display* display, XErrorEvent* error)
{
  char errorbuf[512];

  XGetErrorText(display, error->error_code, errorbuf, sizeof(errorbuf));

  if(error->error_code == BadWindow)
    destroy_notify(error->resourceid);

  fprintf(stderr, "X error: %s (request: %d, minor: %d)  ID: %08X\n", errorbuf,
          error->request_code, error->minor_code, (unsigned int) error->resourceid);

  return 0;
}

static int xerror_discarder(Display* display, XErrorEvent* error)
{
  return 0;
}

static int x11_connected = 0;

static void
create_menu_cursor()
{
  Pixmap mask = XCreatePixmap(display, XRootWindow(display, 0), 1, 1, 1);

  XGCValues xgc;

  xgc.function = GXclear;

  GC gc = XCreateGC(display, mask, GCFunction, &xgc);

  XFillRectangle(display, mask, gc, 0, 0, 1, 1);

  XColor color;

  color.pixel = 0;
  color.red = 0;
  color.flags = 4;

  menu_cursor = XCreatePixmapCursor(display, mask, mask, &color, &color, 0, 0);

  XFreePixmap(display, mask);

  XFreeGC(display, gc);
}

static void composite_init()
{
  XWindowAttributes attr;
  XRenderPictFormat *format;
  XRenderPictureAttributes pa;
  int major = 0, minor = 2;
  int i;

  if(!XCompositeQueryExtension(display, &i, &i))
    {
      fprintf (stderr, "No XComposite\n");

      return;
    }

  if(!XDamageQueryExtension(display, &damage_eventbase, &damage_errorbase))
    {
      fprintf (stderr, "No XDamage\n");

      return;
    }

  XCompositeQueryVersion(display, &major, &minor);

  if(!(major > 0 || minor >= 2))
    return;

  XCompositeRedirectSubwindows(display, root_window, CompositeRedirectManual);

  XGetWindowAttributes (display, root_window, &attr);

  format = XRenderFindVisualFormat(display, attr.visual);

  memset(&pa, 0, sizeof(pa));
  pa.subwindow_mode = IncludeInferiors;

  root_picture = XRenderCreatePicture(display, root_window, xrenderpictformat, CPSubwindowMode, &pa);
}

static void x11_connect(const char* display_name)
{
  XSetWindowAttributes window_attr;
  XineramaScreenInfo* xinerama_screens = 0;
  XRenderColor color;
  int i;
  int nitems;
  char* c;

  fprintf(stderr, "Connecting to %s\n", display_name);

  if(x11_connected)
    return;

  display = XOpenDisplay(display_name);

  if(!display)
  {
    fprintf(stderr, "Failed to open display %s\n", display_name);

    return;
  }

  XSynchronize(display, True);

  root_window = RootWindow(display, screenidx);

  window_attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | EnterWindowMask;
  XChangeWindowAttributes(display, root_window, CWEventMask, &window_attr);

  XGetWindowAttributes(display, root_window, &root_window_attr);

  XSetErrorHandler(xerror_handler);
  //XSetIOErrorHandler(xioerror_handler);

  screenidx = DefaultScreen(display);
  screen = DefaultScreenOfDisplay(display);
  visual = DefaultVisual(display, screenidx);
  visual_info = XGetVisualInfo(display, VisualNoMask, &visual_template, &nitems);

  xrenderpictformat = XRenderFindVisualFormat(display, visual);

  if(!xrenderpictformat)
  {
    fprintf(stderr, "XRenderFindVisualFormat failed.\n");

    return;
  }

  create_menu_cursor();

  if(XineramaQueryExtension(display, &i, &i))
  {
    if(XineramaIsActive(display))
      xinerama_screens = XineramaQueryScreens(display, &screen_count);
  }

  if(!screen_count)
  {
    screen_count = 1;
    xinerama_screens = malloc(sizeof(*screens) * 1);
    xinerama_screens[0].x_org = 0;
    xinerama_screens[0].y_org = 0;
    xinerama_screens[0].width = root_window_attr.width;
    xinerama_screens[0].height = root_window_attr.height;
  }

  screens = calloc(sizeof(*screens), screen_count);
  current_screen = screens;

  for(i = 0; i < screen_count; ++i)
    {
      XRenderPictureAttributes pa;
      Pixmap pmap;
      XCreateWindowEvent cwe;
      struct window *w;

      memset(&pa, 0, sizeof(pa));
      pa.subwindow_mode = IncludeInferiors;

      screens[i].width = xinerama_screens[i].width;
      screens[i].height = xinerama_screens[i].height;
      screens[i].x_org = xinerama_screens[i].x_org;
      screens[i].y_org = xinerama_screens[i].y_org;

      memset(&window_attr, 0, sizeof(window_attr));

      window_attr.colormap = DefaultColormap(display, 0);
      window_attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask;
      window_attr.override_redirect = True;

      window_attr.cursor = menu_cursor;

      screens[i].window
        = XCreateWindow(display, root_window,
                        screens[i].x_org, screens[i].y_org,
                        screens[i].width, screens[i].height, 0,
                        visual_info->depth, InputOutput, visual,
                        CWOverrideRedirect | CWColormap | CWEventMask |
                        CWCursor, &window_attr);

      XMapWindow(display, screens[i].window);

      screens[i].root_picture = XRenderCreatePicture(display, screens[i].window, xrenderpictformat, CPSubwindowMode, &pa);

      pmap = XCreatePixmap(display, screens[i].window, screens[i].width, screens[i].height, visual_info->depth);
      screens[i].root_buffer = XRenderCreatePicture(display, pmap, xrenderpictformat, 0, 0);

      if(screens[i].root_buffer == None)
        {
          fprintf(stderr, "Failed to create root buffer\n");

          return;
        }

      font_init(pmap, visual, DefaultColormap(display, 0));

      XFreePixmap(display, pmap);

      screens[i].active_terminal = 0;
      screens[i].at = &screens[i].terminals[0];

      memset (&cwe, 0, sizeof (cwe));
      cwe.window = screens[i].window;
      cwe.width = screens[i].width;
      cwe.height = screens[i].height;
      cwe.x = screens[i].x_org;
      cwe.y = screens[i].y_org;
      cwe.override_redirect = 1;

      w = create_notify (&cwe);
      w->screen = &screens[i];
      w->flags |= WINDOW_IS_MAPPED;
      w->type = wm_window_type_wm;

      screens[i].background = w;
    }

  if (xinerama_screens)
    XFree (xinerama_screens);

  window = screens[0].window;

  grab_keys();

  xa_utf8_string = XInternAtom(display, "UTF8_STRING", False);
  xa_compound_text = XInternAtom(display, "COMPOUND_TEXT", False);
  xa_targets = XInternAtom(display, "TARGETS", False);
  xa_wm_state = XInternAtom(display, "WM_STATE", False);
  xa_net_wm_icon = XInternAtom(display, "_NET_WM_ICON", False);
  xa_net_wm_pid = XInternAtom(display, "_NET_WM_PID", False);
  xa_net_wm_user_time = XInternAtom(display, "_NET_WM_USER_TIME", False);
  xa_net_wm_user_time_window = XInternAtom(display, "_NET_WM_USER_TIME_WINDOW", False);
  xa_wm_transient_for = XInternAtom(display, "WM_TRANSIENT_FOR", False);
  xa_wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
  xa_wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);

  xim = 0;

  if((c = XSetLocaleModifiers("")) && *c)
    xim = XOpenIM(display, 0, 0, 0);

  if(!xim && (c = XSetLocaleModifiers("@im=none")) && *c)
    xim = XOpenIM(display, 0, 0, 0);

  if(!xim)
  {
    fprintf(stderr, "Failed to open X Input Method\n");

    return;
  }

  /* XXX: Used to be `window'  Root okay? */
  xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                  XNClientWindow, root_window, XNFocusWindow, root_window, NULL);

  if(!xic)
  {
    fprintf(stderr, "Failed to create X Input Context\n");

    return;
  }

  a8pictformat = XRenderFindStandardFormat(display, PictStandardA8);

  for(i = 0; i < sizeof(palette) / sizeof(palette[0]); ++i)
  {
    xrpalette[i].alpha = ((palette[i] & 0xff000000) >> 24) * 0x0101;
    xrpalette[i].red = ((palette[i] & 0xff0000) >> 16) * 0x0101;
    xrpalette[i].green = ((palette[i] & 0x00ff00) >> 8) * 0x0101;
    xrpalette[i].blue = (palette[i] & 0x0000ff) * 0x0101;

    picpalette[i] = XRenderCreateSolidFill(display, &xrpalette[i]);
  }

  alpha_glyphs[0] = XRenderCreateGlyphSet(display, a8pictformat);
  alpha_glyphs[1] = XRenderCreateGlyphSet(display, a8pictformat);

  if(!alpha_glyphs[0] || !alpha_glyphs[1])
  {
    fprintf(stderr, "XRenderCreateGlyphSet failed.\n");

    return;
  }

  color.red = 0xffff;
  color.green = 0xffff;
  color.blue = 0xffff;
  color.alpha = 90 * 0xffff / 100;
  blend_90 = XRenderCreateSolidFill(display, &color);

  composite_init();

  menu_init();

  wm_window_type_init (display);

  xfd = ConnectionNumber(display);

  XSynchronize(display, False);

  x11_connected = 1;
}

static int done;

static void sighandler(int signal)
{
  fprintf(stderr, "Got signal %d\n", signal);

  exit(EXIT_SUCCESS);
}

static void
enter_menu_mode(struct screen* screen, terminal* t)
{
  /* XXX: Free first (2010-10-15: what does this mean?) */

  t->mode = mode_menu;

  if(t->thumbnail)
  {
    XRenderFreePicture(display, t->thumbnail);
    t->thumbnail = 0;
  }
}

int
find_pid(pid_t pid, struct terminal** term, struct screen** screen)
{
  unsigned int screen_index;
  unsigned int term_index;

  for(screen_index = 0; screen_index < screen_count; ++screen_index)
    {
      for(term_index = 0; term_index < TERMINAL_COUNT; ++term_index)
        {
          if(screens[screen_index].terminals[term_index].pid == pid)
            {
              *term = &screens[screen_index].terminals[term_index];
              *screen = &screens[screen_index];

              return 0;
            }
        }
    }

  return -1;
}

void
get_window_hints (struct window *w)
{
  if (w->type != wm_window_type_unknown)
    return;

  XSync(display, False);
  XSetErrorHandler(xerror_discarder);

  if (w->type == wm_window_type_unknown)
    w->type = wm_window_type_get (display, w->xwindow);

  XGetTransientForHint(display, w->xwindow, &w->transient_for);

  XSync(display, False);
  XSetErrorHandler(xerror_handler);

  if(w->transient_for)
    {
      struct window* parent;

      if (w->type == wm_window_type_normal)
	w->type = wm_window_type_dialog;

      parent = find_window(w->transient_for);

      if(parent)
        {
          w->screen = parent->screen;
          w->desktop = parent->desktop;
        }
    }
}

void
destroy_notify(Window xwindow)
{
  struct screen* screen;
  terminal* desktop;
  struct window* w;
  size_t i, j;

  w = find_window(xwindow);

  if(!w)
    return;

  screen = w->screen;
  desktop = w->desktop;

  ARRAY_REMOVE_PTR(&windows, w);

  if (screen && screen->history_size > 1 && w->type == wm_window_type_normal)
    {
      i = desktop - screen->terminals;

      assert (i >= 0 && i < TERMINAL_COUNT);

      if (i == screen->history[screen->history_size - 1])
        {
          --screen->history_size;
          set_active_terminal(screen, screen->history[screen->history_size - 1], CurrentTime);
        }

      /* If desktop is inside the history stack, remove it */

      for (j = 0; screen->history_size > 1 && j < screen->history_size; )
        {
          if (screen->history[j] == i)
            {
              --screen->history_size;
              memmove(screen->history + j, screen->history + j + 1, sizeof(*screen->history) * (screen->history_size - j));
            }
          else
            ++j;
        }
    }

  if(desktop)
    {
      for(i = 0; i < ARRAY_COUNT(&windows); ++i)
        if(ARRAY_GET(&windows, i).desktop == desktop)
          return;

      if(i == ARRAY_COUNT(&windows))
        enter_menu_mode(screen, desktop);

      if(screen->at == desktop)
        {
          set_focus(screen, desktop, CurrentTime);
          clear();
        }
    }

  /*
  composite_destroy_window (w);
  */
}

struct window *
create_notify (XCreateWindowEvent* cwe)
{
  struct window new_window;
  struct window *w;

  if (0 != (w = find_window (cwe->window)))
    return w;

  memset(&new_window, 0, sizeof(new_window));

  if (cwe->override_redirect)
    new_window.flags |= WINDOW_UNMANAGED;

  new_window.xwindow = cwe->window;
  new_window.position.x = cwe->x;
  new_window.position.y = cwe->y;
  new_window.position.width = cwe->width;
  new_window.position.height = cwe->height;

  new_window.target = new_window.position;

  ARRAY_ADD(&windows, new_window);

  return &ARRAY_GET(&windows, ARRAY_COUNT(&windows) - 1);
}

int window_size(XWindowChanges *wc, const struct window *w)
{
  int x, y, width, height;

  if (!w->screen || (w->flags & WINDOW_UNMANAGED))
    return 0;

  x = wc->x;
  y = wc->y;
  width = wc->width;
  height = wc->height;

  switch (w->type)
    {
    case wm_window_type_normal:

      x = w->screen->x_org;
      y = w->screen->y_org;
      width = w->screen->width;
      height = w->screen->height;

      break;

    case wm_window_type_desktop:

      x = w->screen->x_org;
      y = w->screen->y_org;
      width = w->screen->width;
      height = w->screen->height - terminal_list_height(w->screen);

      break;

    default:

      if (x < w->screen->x_org)
        x = w->screen->x_org;

      if (y < w->screen->y_org)
        y = w->screen->y_org;

      if (x + width > w->screen->width)
        width = w->screen->width - x;

      if (y + height > w->screen->height)
        height = w->screen->height - y;

      break;
    }

  if (wc->x != x || wc->y != y
      || wc->width != w->screen->width
      || wc->height != w->screen->height)
    {
      wc->x = x;
      wc->y = y;
      wc->width = width;
      wc->height = height;

      return 1;
    }

  return 0;
}

extern struct tree* config;

void
paint_dirty_windows()
{
  size_t i, j;
  struct window *w;

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      w = &ARRAY_GET(&windows, i);

      if (w->flags & WINDOW_DIRTY)
        break;
    }

  for(; i < ARRAY_COUNT(&windows); ++i)
    {
      w = &ARRAY_GET(&windows, i);

      if (w->type == wm_window_type_wm)
        continue;

      if (!w->xpicture)
        continue;

      if (w->screen && w->screen->at->mode == mode_menu)
        XClearArea(display, w->screen->window, 0, 0, w->screen->width, w->screen->height, True);
      else
        {
          if (!(w->flags & WINDOW_IS_MAPPED))
            continue;

          if (w->flags & WINDOW_UNMANAGED)
            {
              for (j = 0; j < screen_count; ++j)
                {
                  XRenderComposite(display, PictOpOver,
                                   w->xpicture,
                                   blend_90,
                                   screens[j].root_buffer,
                                   0, 0,
                                   0, 0,
                                   w->position.x - screens[j].x_org, w->position.y - screens[j].y_org,
                                   w->position.width, w->position.height);
                }
            }
          else if (w->type != wm_window_type_wm)
            {
              XRenderComposite(display, PictOpSrc,
                               w->xpicture,
                               None,
                               w->screen->root_buffer,
                               0, 0,
                               0, 0,
                               w->position.x - w->screen->x_org, w->position.y - w->screen->y_org,
                               w->position.width, w->position.height);
            }
        }

      w->flags &= ~WINDOW_DIRTY;
    }

  if (current_screen->at->mode == mode_x11 && desktops_visible)
    menu_draw_desktops(current_screen);

  for(i = 0; i < screen_count; ++i)
    {
      if (screens[i].at->mode == mode_menu)
        menu_draw(&screens[i]);

      XRenderComposite(display, PictOpSrc,
                       screens[i].root_buffer,
                       None,
                       root_picture,
                       0, 0,
                       0, 0,
                       screens[i].x_org, screens[i].y_org, screens[i].width, screens[i].height);
    }
}

int main(int argc, char** argv)
{
  int i, j;
  int result;

  setlocale(LC_ALL, "en_US.UTF-8");

  if(!getenv("DISPLAY"))
  {
    fprintf(stderr, "DISPLAY variable not set.\n");

    return EXIT_FAILURE;
  }

  chdir(getenv("HOME"));

  config = tree_load_cfg(".cantera/config");

  inotify_fd = inotify_init1(IN_CLOEXEC);

  if(inotify_fd != -1)
    {
      if(-1 == inotify_add_watch(inotify_fd, ".cantera", IN_ALL_EVENTS | IN_ONLYDIR))
        fprintf(stderr, "inotify_add_watch failed: %s\n", strerror(errno));
    }

  mkdir(".cantera", 0777);
  mkdir(".cantera/commands", 0777);
  mkdir(".cantera/file-commands", 0777);
  mkdir(".cantera/filemanager", 0777);

  signal(SIGTERM, sighandler);
  signal(SIGIO, sighandler);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

  setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin:/usr/games:~/bin", 1);
  setenv("TERM", "xterm", 1);

  x11_connect(getenv("DISPLAY"));

  if(!x11_connected)
    return EXIT_FAILURE;

  for(j = 0; j < screen_count; ++j)
    {
      for(i = 0; i < TERMINAL_COUNT; ++i)
        {
          char buf[32];
          const char* command;

          screens[j].active_terminal = i;
          screens[j].at = &screens[j].terminals[i];

          if(i < 24)
            {
              if(!j)
                {
                  sprintf(buf,
                          "auto-launch.%s-f%d", (i < 12) ? "ctrl" : "super",
                          (i % 12) + 1);
                }
              else
                {
                  sprintf(buf,
                          "auto-launch.%s-f%d.%u", (i < 12) ? "ctrl" : "super",
                          (i % 12) + 1, j);
                }

              if(0 != (command = tree_get_string_default(config, buf, 0)))
                launch(command, 0);
            }

          screens[j].at->mode = mode_menu;
          screens[j].at->return_mode = mode_menu;
        }

      screens[j].active_terminal = 0;
      screens[j].at = &screens[j].terminals[0];

      history_reset(&screens[j], 0);
    }

  set_focus(current_screen, current_screen->at, CurrentTime);

  while(!done)
  {
    pid_t pid;
    int status;
    int maxfd = xfd;
    fd_set readset;
    fd_set writeset;
    struct timeval timeout;

    while(0 < (pid = waitpid(-1, &status, WNOHANG)))
      ;

    if(x11_connected && XPending(display))
      goto process_events;

    FD_ZERO(&readset);
    FD_ZERO(&writeset);

    if(x11_connected)
    {
      FD_SET(xfd, &readset);

      if(xfd > maxfd)
        maxfd = xfd;
    }

    if(inotify_fd != -1)
      {
        FD_SET(inotify_fd, &readset);

        if(inotify_fd > maxfd)
          maxfd = inotify_fd;
      }

    gettimeofday(&timeout, 0);
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000000 - timeout.tv_usec;

    result = select(maxfd + 1, &readset, &writeset, 0, &timeout);

    if(result < 0)
    {
      FD_ZERO(&writeset);
      FD_ZERO(&readset);
    }

    if(result == 0)
      clear();

    if(inotify_fd != -1 && FD_ISSET(inotify_fd, &readset))
      {
        struct inotify_event* ev;
        char* buf;
        size_t size;
        int available = 0;

        result = ioctl(inotify_fd, FIONREAD, &available);

        if(available > 0)
          {
            buf = malloc(available);

            result = read(inotify_fd, buf, available);

            if(result < 0)
              {
                fprintf(stderr, "Read error from inotify file descriptor: %s\n",
                        strerror(errno));

                close(inotify_fd);

                inotify_fd = -1;
              }
            else if(result < sizeof(struct inotify_event))
              {
                fprintf(stderr, "Short read from inotify\n");

                close(inotify_fd);

                inotify_fd =-1;
              }
            else
              {
                ev = (struct inotify_event*) buf;

                while(available)
                  {
                    size = sizeof(struct inotify_event) + ev->len;

                    if(size > available)
                      {
                        fprintf(stderr, "Corrupt data in inotify stream\n");

                        close(inotify_fd);

                        inotify_fd = -1;

                        break;
                      }

                    if(!strcmp(ev->name, "config")
                       && (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)))
                      {
                        tree_destroy(config);
                        config = tree_load_cfg(".cantera/config");
                      }

                    available -= size;
                    ev = (struct inotify_event*) ((char*) ev + size);
                  }
              }

            free(buf);
          }
      }

    if(x11_connected && FD_ISSET(xfd, &readset))
    {
      XEvent event;

process_events:

      while(XPending(display))
      {
        XNextEvent(display, &event);

        if(event.type == damage_eventbase + XDamageNotify)
          {
            XDamageNotifyEvent* e = (XDamageNotifyEvent*) &event;
            struct window *w;

            w = find_window (e->drawable);

            if (w)
              {
                if(!w->damage_region)
                  w->damage_region = XFixesCreateRegion(display, 0, 0);

                XDamageSubtract(display, e->damage, None, w->damage_region);

                w->flags |= WINDOW_DIRTY;
              }
            else
              XDamageSubtract(display, e->damage, None, None);

            continue;
          }

        switch(event.type)
        {
        case KeyPress:

          if(!XFilterEvent(&event, event.xkey.window))
          {
            char text[32];
            Status status;
            KeySym key_sym;
            int len;

            ctrl_pressed = (event.xkey.state & ControlMask);
            mod1_pressed = (event.xkey.state & Mod1Mask);
            super_pressed = (event.xkey.state & Mod4Mask);
            shift_pressed = (event.xkey.state & ShiftMask);

            len = Xutf8LookupString(xic, &event.xkey, text, sizeof(text) - 1, &key_sym, &status);

            if(!text[0])
              len = 0;

            if(key_sym == XK_Control_L || key_sym == XK_Control_R)
              ctrl_pressed = 1;

            if(key_sym == XK_Super_L || key_sym == XK_Super_R)
              super_pressed = 1;

            if(key_sym == XK_Alt_L || key_sym == XK_Alt_R)
              mod1_pressed = 1;

            /* Logitech cordless keyboard (S510) */

            /* right side keys */
            if(event.xkey.keycode == 129)
              run_command(-1, "music", 0);
            else if(event.xkey.keycode == 162)
              run_command(-1, "play_pause", 0);
            else if(event.xkey.keycode == 164)
              run_command(-1, "stop", 0);
            else if(event.xkey.keycode == 153)
              run_command(-1, "next", 0);
            else if(event.xkey.keycode == 144)
              run_command(-1, "previous", 0);
            else if(event.xkey.keycode == 176)
              run_command(-1, "increase-sound-volume", 0);
            else if(event.xkey.keycode == 174)
              run_command(-1, "decrease-sound-volume", 0);
            else if(event.xkey.keycode == 160)
              run_command(-1, "toggle-mute", 0);
            /* left side keys */
            else if(event.xkey.keycode == 223)
              run_command(-1, "standby", 0);
            else if(event.xkey.keycode == 130)
              run_command(-1, "home", 0);
            /* other */
            else if(ctrl_pressed && event.xkey.keycode == 110)
              run_command(-1, "coffee", 0);
            else if(key_sym >= 'a' && key_sym <= 'z' && super_pressed)
              {
                char key[10];
                const char* command;

                sprintf(key, "hotkey.%c", (int) key_sym);

                command = tree_get_string_default(config, key, 0);

                if(command)
                  launch(command, event.xkey.time);
              }
            else if((super_pressed ^ ctrl_pressed) && key_sym >= XK_F1 && key_sym <= XK_F12)
            {
              unsigned int new_terminal;

              new_terminal = key_sym - XK_F1;

              if(super_pressed)
                new_terminal += 12;

              if(new_terminal != current_screen->active_terminal)
              {
                  history_reset(current_screen, new_terminal);
                  set_active_terminal(current_screen, new_terminal, event.xkey.time);
              }
            }
            else if((key_sym == XK_q || key_sym == XK_Q) && (ctrl_pressed && mod1_pressed))
            {
              exit(EXIT_SUCCESS);
            }
            else if(super_pressed && (mod1_pressed ^ ctrl_pressed))
            {
              int new_terminal;
              int direction = 0;

              if(key_sym == XK_Right)
                direction = 1;
              else if(key_sym == XK_Left)
                direction = -1;
              else if(key_sym == XK_Down)
                direction = TERMINAL_COUNT / 2;
              else if(key_sym == XK_Up)
                direction = -TERMINAL_COUNT / 2;

              if(direction)
              {
                new_terminal = (TERMINAL_COUNT + current_screen->active_terminal + direction) % TERMINAL_COUNT;

                if(ctrl_pressed)
                {
                  swap_terminals(new_terminal, current_screen->active_terminal);

                  current_screen->history[current_screen->history_size - 1] = new_terminal;

                  current_screen->active_terminal = new_terminal;
                  current_screen->at = &current_screen->terminals[current_screen->active_terminal];
                }
                else
                {
                    history_reset(current_screen, new_terminal);
                    set_active_terminal(current_screen, new_terminal, event.xkey.time);
                }
              }

              desktops_visible = 1;
            }
            else if(super_pressed && key_sym >= XK_1 && key_sym <= XK_9)
            {
              unsigned int screen = key_sym - XK_1;

              if(screen >= screen_count)
                break;

              current_screen = &screens[screen];

              set_focus(current_screen, current_screen->at, event.xkey.time);
            }
            else if(ctrl_pressed && mod1_pressed && (key_sym == XK_Escape))
            {
              launch("xkill", event.xkey.time);
            }
            else if(mod1_pressed && key_sym == XK_F4)
            {
              switch(current_screen->at->mode)
              {
              case mode_x11:

                {
                  XClientMessageEvent cme;
                  Window temp_window;

                  if(0 != (temp_window = find_xwindow(current_screen->at)))
                    {
                      cme.type = ClientMessage;
                      cme.send_event = True;
                      cme.display = display;
                      cme.window = temp_window;
                      cme.message_type = xa_wm_protocols;
                      cme.format = 32;
                      cme.data.l[0] = xa_wm_delete_window;
                      cme.data.l[1] = event.xkey.time;

                      XSendEvent(display, temp_window, False, 0, (XEvent*) &cme);
                    }
                }

                break;

              default:;
              }
            }
            else if(current_screen->at->mode == mode_x11)
            {
            }
            else if(current_screen->at->mode == mode_menu)
            {
              if(0 != menu_handle_char(current_screen, text[0]))
                menu_keypress(current_screen, key_sym, text, len, event.xkey.time);
            }
          }

          clear();

          break;

        case KeyRelease:

          {
            KeySym key_sym;

            key_sym = XLookupKeysym(&event.xkey, 0);

            ctrl_pressed = (event.xkey.state & ControlMask);
            mod1_pressed = (event.xkey.state & Mod1Mask);
            super_pressed = (event.xkey.state & Mod4Mask);
            shift_pressed = (event.xkey.state & ShiftMask);

            if(key_sym == XK_Control_L || key_sym == XK_Control_R)
              ctrl_pressed = 0;

            if(key_sym == XK_Super_L || key_sym == XK_Super_R)
              super_pressed = 0;

            if(key_sym == XK_Alt_L || key_sym == XK_Alt_R)
              mod1_pressed = 0;

            if(!super_pressed || !(mod1_pressed ^ ctrl_pressed))
              desktops_visible = 0;
          }

          break;

        case DestroyNotify:

          destroy_notify(event.xdestroywindow.window);

          break;

        case UnmapNotify:

            {
              struct window* w;
              XEvent destroy_event;

              /* Window is probably destroyed, so we check that first */
              while(XCheckTypedWindowEvent(display, root_window, DestroyNotify,
                                           &destroy_event))
                {
                  destroy_notify(destroy_event.xdestroywindow.window);
                }

              w = find_window(event.xunmap.window);

              if(!w)
                break;

              composite_destroy_window (w);

              if(w->screen && w->desktop == w->screen->at)
                set_focus(w->screen, w->screen->at, CurrentTime);
            }

          break;

        case NoExpose:

          break;

        case Expose:

          {
            int minx = event.xexpose.x;
            int miny = event.xexpose.y;
            int maxx = minx + event.xexpose.width;
            int maxy = miny + event.xexpose.height;

            while(XCheckTypedWindowEvent(display, event.xexpose.window, Expose, &event))
            {
              if(event.xexpose.x < minx) minx = event.xexpose.x;
              if(event.xexpose.y < miny) miny = event.xexpose.y;
              if(event.xexpose.x + event.xexpose.width > maxx) maxx = event.xexpose.x + event.xexpose.width;
              if(event.xexpose.y + event.xexpose.height > maxy) maxy = event.xexpose.y + event.xexpose.height;
            }

            paint(event.xexpose.window, minx, miny, maxx - minx, maxy - miny);
          }

          break;

        case CreateNotify:

          create_notify (&event.xcreatewindow);

          break;

        case ConfigureRequest:

            {
              XWindowAttributes attr;
              XWindowChanges wc;
              XConfigureRequestEvent* request = &event.xconfigurerequest;
              int mask;
              struct window* w;

              mask = request->value_mask;

              memset(&wc, 0, sizeof(wc));

              w = find_window(request->window);

              if(!w)
                break;

              XGetWindowAttributes(display, w->xwindow, &attr);

              get_window_hints (w);

              mask = request->value_mask;
              wc.sibling = request->above;
              wc.stack_mode = request->detail;
              wc.x = request->x;
              wc.y = request->y;
              wc.width = request->width;
              wc.height = request->height;

              if(!(mask & CWX))
                wc.x = w->target.x;

              if(!(mask & CWY))
                wc.y = w->target.y;

              if(!(mask & CWWidth))
                wc.width = w->target.width;

              if(!(mask & CWHeight))
                wc.height = w->target.height;

              window_size(&wc, w);

              mask |= CWX | CWY | CWWidth | CWHeight;

              w->target.x = wc.x;
              w->target.y = wc.y;
              w->target.width = wc.width;
              w->target.height = wc.height;

              XConfigureWindow(display, request->window, mask, &wc);
            }

          break;

        case ConfigureNotify:

            {
              struct window* w;

              w = find_window(event.xconfigure.window);

              if(!w)
                break;

              w->position.x = event.xconfigure.x;
              w->position.y = event.xconfigure.y;
              w->position.width = event.xconfigure.width;
              w->position.height = event.xconfigure.height;
            }

          break;


        case MapNotify:

            {
              struct window *w;

              if (0 == (w = find_window (event.xmap.window)))
                break;

              composite_init_window (w);
              w->flags |= WINDOW_DIRTY | WINDOW_IS_MAPPED;
            }

          break;

        case MapRequest:

          {
            XWindowChanges wc;
            struct window* w;
            int pid;

            w = find_window(event.xmaprequest.window);

            if(!w)
              break;

            get_window_hints (w);

            if (0)
              {
                int x = 0, ww = 0;
                unsigned int screen_index;
                unsigned int term_index;

                get_int_property(w->xwindow, xa_net_wm_user_time_window, &ww);
                get_int_property(ww ? (Window) ww : w->xwindow, xa_net_wm_user_time, &x);

                printf ("New: user time: %d  user time window: %d\n", x, ww);

                if (x)
                  {
                    for(screen_index = 0; screen_index < screen_count; ++screen_index)
                      {
                        for(term_index = 0; term_index < TERMINAL_COUNT; ++term_index)
                          {
                            printf ("  Screen: %d\n", (int) screens[screen_index].terminals[term_index].startup);
                          }
                      }
                  }
              }

            if(!w->desktop
               && w->type == wm_window_type_normal
               && 0 == get_int_property(w->xwindow, xa_net_wm_pid, &pid))
              {
                pid = getsid(pid);

                if(0 == find_pid(pid, &w->desktop, &w->screen))
                  {
                    if(w->desktop->mode != mode_menu)
                      {
                        w->screen = 0;
                        w->desktop = 0;
                      }
                    else
                      {
                        w->desktop->mode = mode_x11;
                        clear();
                      }
                  }
              }

            if(!w->desktop && w->type == wm_window_type_normal)
              {
                unsigned int new_terminal;

                new_terminal = first_available_terminal(current_screen);

                w->screen = current_screen;
                w->desktop = &current_screen->terminals[new_terminal];

                memset(w->desktop, 0, sizeof(*w->desktop));
                w->desktop->mode = mode_x11;

                history_add(current_screen, new_terminal);
                set_active_terminal(current_screen, new_terminal, CurrentTime);
              }
            else if (w->type == wm_window_type_desktop)
              {
                struct window *w;

                for (i = 0; i < screen_count; ++i)
                  {
                    for(j = 0; j < ARRAY_COUNT(&windows); ++j)
                      {
                        w = &ARRAY_GET(&windows, j);

                        if (w->type == wm_window_type_desktop && w->screen == &screens[i])
                          break;
                      }

                    if (j == ARRAY_COUNT(&windows))
                      w->screen = &screens[i];
                  }

                if (i == screen_count)
                  w->screen = &screens[0];
              }

            wc.x = w->target.x;
            wc.y = w->target.y;
            wc.width = w->target.width;
            wc.height = w->target.height;

            if (window_size(&wc, w))
              {
                w->target.x = wc.x;
                w->target.y = wc.y;
                w->target.width = wc.width;
                w->target.height = wc.height;

                XConfigureWindow(display, w->xwindow, CWX | CWY | CWWidth | CWHeight, &wc);
              }

            if (w->type == wm_window_type_normal)
              grab_thumbnail(w);

            if(w->desktop == w->screen->at || w->type == wm_window_type_dock)
              set_focus (w->screen, w->screen->at, CurrentTime);
          }

          break;
        }
      }
    }

    paint_dirty_windows ();
  }

  return EXIT_SUCCESS;
}

void run_command(int fd, const char* command, const char* arg)
{
  char path[4096];
  sprintf(path, ".cantera/commands/%s", command);

  if(-1 == access(path, X_OK))
    sprintf(path, PKGDATADIR "/commands/%s", command);

  if(-1 == access(path, X_OK))
    return;

  if(!fork())
  {
    char* args[3];

    if(fd != -1)
      dup2(fd, 1);

    args[0] = path;
    args[1] = (char*) arg;
    args[2] = 0;

    execve(args[0], args, environ);

    exit(EXIT_FAILURE);
  }
}

void init_ximage(XImage* image, int width, int height, void* data)
{
  memset(image, 0, sizeof(XImage));
  image->width = width;
  image->height = height;
  image->format = ZPixmap;
  image->data = (char*) data;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  image->byte_order = LSBFirst;
  image->bitmap_bit_order = LSBFirst;
#else
  image->byte_order = MSBFirst;
  image->bitmap_bit_order = MSBFirst;
#endif
  image->bitmap_unit = 32;
  image->bitmap_pad = 32;
  image->depth = 32;
  image->bytes_per_line = width * 4;
  image->bits_per_pixel = 32;
}

// vim: ts=2 sw=2 et sts=2
