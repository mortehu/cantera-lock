#ifndef GLOBALS_H_
#define GLOBALS_H_ 1

#include <X11/extensions/Xinerama.h>

#include <stdint.h>

#include <sys/time.h>
#include <pty.h>

enum mode
{
  mode_menu,
  mode_x11
};

#define MAX_QUERIES 16

struct terminal
{
  Picture thumbnail;

  enum mode mode;
  enum mode return_mode;

  pid_t pid;
  int fd;

  struct timeval next_update;
};

typedef struct terminal terminal;

#define TERMINAL_COUNT 24

struct screen
{
  wchar_t query[256];

  unsigned int x_org, y_org;
  unsigned int width, height;
  Window window;
  Picture root_buffer;
  Picture root_picture;
  terminal terminals[TERMINAL_COUNT];

  int history[TERMINAL_COUNT];
  int history_size;

  int active_terminal;
  terminal* at;
};

extern struct screen* current_screen;

extern int xskips[];
extern int yskips[];
extern int font_sizes[];

extern GlyphSet alpha_glyphs[2];
extern XRenderPictFormat* xrenderpictformat;
extern XRenderPictFormat* a8pictformat;
extern XWindowAttributes root_window_attr;
extern Display* display;

extern XRenderColor xrpalette[];
extern Picture picpalette[];

void init_session(terminal* t, char* const* args);

int launch(const char* command, Time time);

size_t utf8_to_ucs(wchar_t* result, const char* input, size_t avail);

#endif /* GLOBALS_H_ */
