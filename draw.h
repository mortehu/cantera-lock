#ifndef DRAW_H_
#define DRAW_H_ 1

void draw_bind_texture(int texture);

void draw_set_color(unsigned int color);

void draw_quad(int texture, float x, float y, float width, float height);

void draw_quad_st(int texture, float x, float y, float width, float height,
                  float s0, float t0, float s1, float t1);

void draw_flush();

#endif /* !DRAW_H_ */
