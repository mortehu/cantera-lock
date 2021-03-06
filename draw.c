/**
 * 2D drawing routines
 * Copyright (C) 2008  Morten Hustveit
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
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include "draw.h"

#define MAX_BATCH_SIZE 1024

struct DRAW_quad {
  int texture;
  uint32_t color;
  float x, y;
  float width, height;
  float u0, v0;
  float u1, v1;
};

static struct DRAW_quad DRAW_quads[MAX_BATCH_SIZE];
static size_t DRAW_quad_count;

static int DRAW_current_texture;
static unsigned int DRAW_current_color = 0xffffffff;

void draw_bind_texture(int texture) {
  if (texture != DRAW_current_texture) {
    glBindTexture(GL_TEXTURE_2D, texture);

    DRAW_current_texture = texture;
  }
}

void draw_set_color(unsigned int color) { DRAW_current_color = color; }

void draw_quad_st(int texture, float x, float y, float width, float height,
                  float s0, float t0, float s1, float t1) {
  size_t i;

  if (DRAW_quad_count == MAX_BATCH_SIZE) draw_flush();

  i = DRAW_quad_count++;
  DRAW_quads[i].texture = texture;
  DRAW_quads[i].color = DRAW_current_color;
  DRAW_quads[i].x = x;
  DRAW_quads[i].y = y;
  DRAW_quads[i].width = width;
  DRAW_quads[i].height = height;
  DRAW_quads[i].u0 = s0;
  DRAW_quads[i].v0 = t0;
  DRAW_quads[i].u1 = s1;
  DRAW_quads[i].v1 = t1;
}

void draw_flush() {
  size_t i;

  for (i = 0; i < DRAW_quad_count; ++i) {
    struct DRAW_quad* quad = &DRAW_quads[i];

    if (DRAW_current_texture != quad->texture) {
      if (i) {
        glEnd();
        glBindTexture(GL_TEXTURE_2D, quad->texture);
        glBegin(GL_QUADS);
      } else
        glBindTexture(GL_TEXTURE_2D, quad->texture);

      DRAW_current_texture = quad->texture;
    }

    if (!i) glBegin(GL_QUADS);

    glColor4ub(quad->color >> 16, quad->color >> 8, quad->color,
               quad->color >> 24);

    glTexCoord2f(quad->u0, quad->v0);
    glVertex2f(quad->x, quad->y);
    glTexCoord2f(quad->u0, quad->v1);
    glVertex2f(quad->x, quad->y + quad->height);
    glTexCoord2f(quad->u1, quad->v1);
    glVertex2f(quad->x + quad->width, quad->y + quad->height);
    glTexCoord2f(quad->u1, quad->v0);
    glVertex2f(quad->x + quad->width, quad->y);
  }

  glEnd();

  DRAW_quad_count = 0;
}
