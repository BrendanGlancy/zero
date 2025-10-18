#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

#ifdef __APPLE__
#include <util.h>
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <pty.h>
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>

// Platform abstraction - hides all #ifdef __APPLE__ from main code
void platform_set_gl_hints(void);
bool platform_init_gl(void);
const char** platform_get_font_paths(void);

#endif // PLATFORM_H
