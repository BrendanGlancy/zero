#ifndef PLATFORM_H
#define PLATFORM_H

#define _POSIX_C_SOURCE 200809L

#ifdef __APPLE__
#include <util.h>
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <pty.h>
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>

#endif // PLATFORM_H
