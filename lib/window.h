#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>

#include "GLFW/glfw3.h"

bool window_init(const char* title, int width, int height);
bool window_should_close(void);
void window_poll(void);
void window_clear(float r, float g, float b);
void window_swap(void);
void window_shutdown(void);
void window_draw_text(float x, float y, const char* text);
void window_get_size(int* window_width, int* window_height);
void window_set_text_color(float r, float g, float b);
void window_draw_rect(float x, float y, float w, float h, float r, float g, float b);
void set_pty_fd(int fd);
GLFWwindow* window_get_glfw_window(void);
void set_copy_handler(void (*handler)(GLFWwindow*));

#endif
