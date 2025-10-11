#include "window.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <ft2build.h>
#include <stdio.h>
#include FT_FREETYPE_H

static GLFWwindow *g_window = NULL;
static FT_Library ft;
static FT_Face face;

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
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    g_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!g_window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1); // enable vsync
    int fb_width, fb_height;
    glfwGetFramebufferSize(g_window, &fb_width, &fb_height);
    glViewport(0, 0, fb_width, fb_height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, fb_width, fb_height, 0, -1, 1); // left, right, bottom, top, near, far (top-down Y)
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "Could not init FreetType\n");
        return false;
    }

    if (FT_New_Face(ft, "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 0, &face)) {
        fprintf(stderr, "Could not open font\n");
    }
    FT_Set_Pixel_Sizes(face, 0, 18);

    return true;
}

void window_clear(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void window_swap(void) { glfwSwapBuffers(g_window); }

void window_poll(void) { glfwPollEvents(); }

bool window_should_close(void) { return glfwWindowShouldClose(g_window); }

void window_shutdown(void) {
    if (g_window)
        glfwDestroyWindow(g_window);
    glfwTerminate();
}

void window_draw_text(float x, float y, const char *text) {
    // PROBLEM: This function uses deprecated OpenGL functions (glRasterPos2f, glDrawPixels)
    // that don't work with Core Profile (OpenGL 3.3+).
    //
    // TO FIX: Implement modern OpenGL text rendering:
    //
    // 1. CREATE TEXTURE ATLAS (do this once in window_init):
    //    - Pre-render all ASCII characters (32-127) using FreeType
    //    - Pack glyph bitmaps into a single OpenGL texture (e.g., 512x512)
    //    - Store each glyph's texture coordinates and metrics in a struct array:
    //      struct Character {
    //          float tx0, ty0, tx1, ty1;  // texture coords in atlas
    //          float width, height;        // size in pixels
    //          float bearing_x, bearing_y; // offset from baseline
    //          float advance;              // horizontal advance
    //      } characters[128];
    //    - Create texture: glGenTextures, glBindTexture, glTexImage2D with GL_RED format
    //    - Set texture parameters: GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_WRAP_*
    //
    // 2. CREATE SHADERS (do this once in window_init):
    //    Vertex Shader:
    //      #version 330 core
    //      layout (location = 0) in vec4 vertex; // <vec2 pos, vec2 tex>
    //      out vec2 TexCoords;
    //      uniform mat4 projection;
    //      void main() {
    //          gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
    //          TexCoords = vertex.zw;
    //      }
    //
    //    Fragment Shader:
    //      #version 330 core
    //      in vec2 TexCoords;
    //      out vec4 color;
    //      uniform sampler2D text;
    //      uniform vec3 textColor;
    //      void main() {
    //          vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);
    //          color = vec4(textColor, 1.0) * sampled;
    //      }
    //
    //    - Compile shaders: glCreateShader, glShaderSource, glCompileShader
    //    - Link program: glCreateProgram, glAttachShader, glLinkProgram
    //    - Get uniform locations: glGetUniformLocation for "projection", "textColor"
    //
    // 3. CREATE VBO/VAO (do this once in window_init):
    //    - Generate VAO: glGenVertexArrays, glBindVertexArray
    //    - Generate VBO: glGenBuffers, glBindBuffer(GL_ARRAY_BUFFER, ...)
    //    - Allocate dynamic buffer: glBufferData with NULL and GL_DYNAMIC_DRAW
    //    - Configure vertex attributes:
    //      glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
    //      glEnableVertexAttribArray(0);
    //
    // 4. IN THIS FUNCTION (window_draw_text):
    //    - Bind shader program: glUseProgram
    //    - Set uniforms: glUniform3f for text color, glUniformMatrix4fv for projection
    //    - Bind texture atlas: glActiveTexture(GL_TEXTURE0), glBindTexture
    //    - Bind VAO: glBindVertexArray
    //    - For each character in text:
    //      a. Look up character metrics from the atlas
    //      b. Calculate quad vertices (6 vertices = 2 triangles):
    //         float xpos = x + bearing_x;
    //         float ypos = y - (height - bearing_y);  // adjust for baseline
    //         float vertices[6][4] = {
    //             { xpos,         ypos + height, tx0, ty0 },  // top-left
    //             { xpos,         ypos,          tx0, ty1 },  // bottom-left
    //             { xpos + width, ypos,          tx1, ty1 },  // bottom-right
    //             { xpos,         ypos + height, tx0, ty0 },  // top-left
    //             { xpos + width, ypos,          tx1, ty1 },  // bottom-right
    //             { xpos + width, ypos + height, tx1, ty0 }   // top-right
    //         };
    //      c. Upload vertices: glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices)
    //      d. Draw: glDrawArrays(GL_TRIANGLES, 0, 6)
    //      e. Advance x position: x += advance
    //    - Unbind: glBindVertexArray(0), glBindTexture(GL_TEXTURE_2D, 0)
    //
    // 5. CLEANUP in window_shutdown:
    //    - glDeleteVertexArrays, glDeleteBuffers, glDeleteProgram, glDeleteTextures
    //
    // NOTE: You'll also need to fix the projection matrix to flip Y-axis or adjust
    // glyph positioning, since FreeType uses top-down coordinates but OpenGL uses bottom-up.

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (const char *p = text; *p; p++) {
        if (FT_Load_Char(face, *p, FT_LOAD_RENDER))
            continue;

        FT_GlyphSlot g = face->glyph;

        float xpos = x + g->bitmap_left;
        float ypos = y - g->bitmap_top + g->bitmap.rows;  // Adjust for top-down Y and bitmap flip

        glRasterPos2f(xpos, ypos);  // DEPRECATED: doesn't work in Core Profile
        glPixelZoom(1.0f, -1.0f);  // Flip vertically to fix upside-down text
        glDrawPixels(g->bitmap.width, g->bitmap.rows, GL_ALPHA, GL_UNSIGNED_BYTE, g->bitmap.buffer);  // DEPRECATED

        x += g->advance.x >> 6;
    }
}
