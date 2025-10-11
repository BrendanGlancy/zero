#include "window.h"
#include <GLFW/glfw3.h>
#include <stdio.h>

static GLFWwindow *g_window = NULL;

static void error_callback(int error, const char *desc) {
    fprintf(stderr, "GLFW Error (%d) %s\n", error, desc);
}

bool window_init(const char *title, int width, int height) {
    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    g_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!g_window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1); // enable vsync

    return true;
}

void window_clear(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// Swap the front and back buffers (show frame)
void window_swap(void) { glfwSwapBuffers(g_window); }

// Poll for input and window events
void window_poll(void) { glfwPollEvents(); }

// Check if the user wants to close the window
bool window_should_close(void) { return glfwWindowShouldClose(g_window); }

// Cleanup and shutdown GLFW
void window_shutdown(void) {
    if (g_window)
        glfwDestroyWindow(g_window);
    glfwTerminate();
}
