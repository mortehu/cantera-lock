#include "lock.h"

#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <err.h>

#include <X11/extensions/Xinerama.h>

#include "draw.h"
#include "font.h"
#include "glyph.h"

extern char* user_name;
extern char* host_name;
extern char* password_hash;

extern XineramaScreenInfo* screens;
extern int screen_count;

static double now;
static time_t begin_lock;
static time_t hide_hud = 0;
static char pass[256];

static const char* hash_for_password(const char* password, const char* salt) {
  return crypt(password, salt);
}

static void text_draw(float x, float y, const char* fmt, ...) {
  char* string, *ch;
  va_list args;
  va_start(args, fmt);

  if (-1 == vasprintf(&string, fmt, args)) return;

  for (ch = string; *ch; ++ch) {
    struct FONT_Glyph glyph;
    uint16_t u, v;

    GLYPH_Get(*ch, &glyph, &u, &v);

    draw_quad_st(GLYPH_Texture(), x - glyph.x, y - glyph.y, glyph.width,
                 glyph.height, (float) u / GLYPH_ATLAS_SIZE,
                 (float) v / GLYPH_ATLAS_SIZE,
                 (float)(u + glyph.width) / GLYPH_ATLAS_SIZE,
                 (float)(v + glyph.height) / GLYPH_ATLAS_SIZE);

    x += glyph.xOffset;
  }

  free(string);
}

void LOCK_init(void) {
  struct FONT_Data* font;
  int ch;

  FONT_Init();
  GLYPH_Init();

  if (!(font = FONT_Load("Bitstream Vera Sans Mono", 18, 100)))
    errx(EXIT_FAILURE, "Failed to load font \"Arial\"");

  for (ch = ' '; ch <= '~'; ++ch) {
    struct FONT_Glyph* glyph;

    if (!(glyph = FONT_GlyphForCharacter(font, ch)))
      fprintf(stderr, "Failed to get glyph for '%d'", ch);

    GLYPH_Add(ch, glyph);

    free(glyph);
  }

  FONT_Free(font);
  GLYPH_UpdateTexture();

  begin_lock = time(0);

  hide_hud = begin_lock + 60;
}

void LOCK_handle_key(KeySym symbol, const char* text) {
  switch (text[0]) {
    case '\b':

      if (pass[0]) pass[strlen(pass) - 1] = 0;

      break;

    case 'U' & 0x3F:

      pass[0] = 0;

      break;

    default:

      if (strlen(pass) + strlen(text) + 1 < sizeof(pass)) strcat(pass, text);
  }

  if (!strcmp(password_hash, hash_for_password(pass, password_hash))) exit(0);

  hide_hud = time(0) + 20;
}

void LOCK_process_frame(float width, float height, double delta_time) {
  int i;
  float x, y;
  time_t now_tt;

  now_tt = time(0);

  now += delta_time * 0.2;

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glOrtho(0.0, width, height, 0.0, 0.0, -1.0);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  draw_bind_texture(0);

  glLineWidth(8.0);

  glColor4f(0.6, 0.8, 1.0, 0.5);

  glBegin(GL_LINE_STRIP);

  for (i = 0; i <= 200; ++i) {
    x = i * width / 200.0;
    y = 0.1 * sin(i * 0.07 + cos(i * 0.03 + now + sin(i * 0.09 + now) * 0.2));

    y = y * height + height * 0.5;

    glVertex2f(x, y);
  }

  glEnd();

  if (now_tt < hide_hud) {
    for (i = 0; i < screen_count; ++i) {
      char date[32];
      struct tm* lt;
      time_t now;

      lt = localtime(&begin_lock);
      strftime(date, sizeof(date), "%Y-%m-%d %H:%M %Z", lt);

      x = screens[i].x_org;
      y = screens[i].y_org;

      draw_set_color(0xffffffff);

      text_draw(x + 10.0, y + 20.0, "%s at %s", user_name, host_name);
      y += 25.0f;

      draw_set_color(0xffcccccc);
      text_draw(x + 10.0, y + 20.0, "Locked since %s", date);
      y += 25.0f;

      unsigned int diff = (now_tt - begin_lock);

      if (diff < 120)
        text_draw(x + 10.0, y + 20.0, "%u seconds ago", diff);
      else if (diff < 3600)
        text_draw(x + 10.0, y + 20.0, "%u:%02u minutes ago", diff / 60,
                  diff % 60);
      else
        text_draw(x + 10.0, y + 20.0, "%u:%02u hours ago", diff / 3600,
                  (diff / 60) % 60);
      y += 25.0f;

      char pass_obfuscated[sizeof(pass)];
      size_t pass_length = strlen(pass);
      memset(pass_obfuscated, '*', pass_length);
      pass_obfuscated[pass_length] = 0;
      text_draw(x + 10.0, y + 20.0, "Password: %s", pass_obfuscated);
      y += 25.0f;

      now = time(0);
      lt = localtime(&now);
      strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S %Z", lt);
      text_draw(x + 10.0, screens[i].y_org + screens[i].height - 10.0, "%s",
                date);
    }
  }

  draw_flush();

  usleep(20000);
}
