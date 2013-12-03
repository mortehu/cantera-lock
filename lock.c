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

static void lock_draw_line(float width, float height) {
  size_t i, j;
  size_t x_count;
  float* ys;

  x_count = (size_t) 200 * width / 1366;
  if (x_count < 50) x_count = 50;

  ys = calloc(x_count, sizeof(*ys));

  for (i = 0; i < x_count; ++i)
    ys[i] =
        0.07 * sin(i * 0.035 + cos(i * 0.021 + now + sin(i * 0.043 + now) * 0.22));

  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  draw_bind_texture(0);

  glLineWidth(3.0);

  for (j = 0; j < 5; ++j) {
    glBegin(GL_LINE_STRIP);

    for (i = 0; i < x_count; ++i) {
      float x, y, theta, twist, scale, a, r, g, b;
      x = i * width / (x_count - 1);
      twist = sin(-0.7 * now + i * 0.021 + cos(1.1 * now + i * 0.031));
      theta = j + i * 0.1 + 2.0 * now + 3.2 * twist;
      a = 0.6 + 0.6 * cos(theta);
      scale = 0.05 - 0.03 * (1.0 - fabs(twist));
      y = ys[i] + scale * sin(theta);

      r = 0.6 * a;
      g = 0.8 * a;
      b = a;

      if (r > 1.0f) r = 1.0f;
      if (g > 1.0f) g = 1.0f;
      if (b > 1.0f) b = 1.0f;

      glColor4f(r, g, b, 0.5);

      y = y * height + height * 0.5;

      glVertex2f(x, y);
    }
    glEnd();
  }

  free(ys);
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
  glDepthMask(0);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glOrtho(0.0, width, height, 0.0, 0.0, -1.0);

  lock_draw_line(width, height);

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
