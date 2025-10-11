#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>

bool window_init(const char *title, int width, int height);
bool window_should_close(void);
void window_poll(void);
void window_clear(float r, float g, float b);
void window_swap(void);
void window_shutdown(void);
void window_draw_text(float x, float y, const char *text);

#endif
