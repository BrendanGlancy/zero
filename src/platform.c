#include "platform.h"
#include <stdio.h>

void platform_set_gl_hints(void) {
#ifdef __APPLE__
  // macOS requires Core Profile (deprecated functions disabled)
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  // Linux: Compat Profile allows deprecated OpenGL (glBegin/glEnd, fixed-function pipeline)
  // NOTE: We don't use any deprecated functions, but compat mode is more permissive
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
#endif
}

bool platform_init_gl(void) {
#ifndef __APPLE__
  // Initialize GLEW on Linux - loads OpenGL function pointers at runtime
  // (macOS links OpenGL framework directly, doesn't need GLEW)
  glewExperimental = GL_TRUE;
  GLenum glew_err = glewInit();
  if (glew_err != GLEW_OK) {
    fprintf(stderr, "Failed to initialize GLEW: %s\n", glewGetErrorString(glew_err));
    return false;
  }
#endif
  return true;
}

const char** platform_get_font_paths(void) {
  static const char* font_paths[] = {
#ifdef __APPLE__
      // macOS font locations
      "/Users/s167452/Library/Fonts/FiraCodeNerdFontMono-Regular.ttf",
      "/System/Library/Fonts/Monaco.ttf",
      "/System/Library/Fonts/Menlo.ttc",
#else
      // Linux font locations (Arch)
      "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
      "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
      "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
      // Debian/Ubuntu fallbacks
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
#endif
      NULL
  };
  return font_paths;
}
