#include "window.h"

#include <stdint.h>
#include <unistd.h>  // For write() system call

// Platform-specific OpenGL includes
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>
#include <ft2build.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include FT_FREETYPE_H

// Character info for texture atlas
typedef struct {
    float tx0, ty0, tx1, ty1;    // texture coords in atlas
    float width, height;         // size in pixels
    float bearing_x, bearing_y;  // offset from baseline
    float advance;               // horizontal advance
} Character;

static GLFWwindow* g_window = NULL;
static int g_pty_fd = -1;
static FT_Library ft;
static FT_Face face;

static GLuint text_vao, text_vbo;
static GLuint text_shader_program;
static GLuint text_texture;
static GLuint rect_vao, rect_vbo;
static GLuint rect_shader_program;

static Character characters[128];
static int atlas_width = 512;
static int atlas_height = 512;

static void error_callback(int error, const char* desc) {
    fprintf(stderr, "GLFW Error (%d) %s\n", error, desc);
}

// Compile a shader and check for errors
static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, NULL, info_log);
        fprintf(stderr, "Shader compilation failed: %s\n", info_log);
    }
    return shader;
}

// Create and link shader program
static GLuint create_shader_program(const char* vertex_src,
                                    const char* fragment_src) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(program, 512, NULL, info_log);
        fprintf(stderr, "Shader linking failed: %s\n", info_log);
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}

// Create texture atlas with all ASCII characters
static bool create_texture_atlas(void) {
    // Create a buffer to hold the atlas
    unsigned char* atlas_buffer = calloc(atlas_width * atlas_height, 1);
    if (!atlas_buffer) {
        fprintf(stderr, "Failed to allocate atlas buffer\n");
        return false;
    }

    int pen_x = 0, pen_y = 0;
    int row_height = 0;

    // Render all ASCII characters into the atlas
    for (int c = 32; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            fprintf(stderr, "Failed to load character %c\n", c);
            continue;
        }

        FT_GlyphSlot g = face->glyph;

        // Move to next row if needed
        if (pen_x + g->bitmap.width >= atlas_width) {
            pen_x = 0;
            pen_y += row_height;
            row_height = 0;
        }

        // Copy glyph bitmap into atlas
        for (unsigned int row = 0; row < g->bitmap.rows; row++) {
            for (unsigned int col = 0; col < g->bitmap.width; col++) {
                int x = pen_x + col;
                int y = pen_y + row;
                if (x < atlas_width && y < atlas_height) {
                    atlas_buffer[y * atlas_width + x] =
                        g->bitmap.buffer[row * g->bitmap.width + col];
                }
            }
        }

        // Store character info
        characters[c].tx0 = (float)pen_x / atlas_width;
        characters[c].ty0 = (float)pen_y / atlas_height;
        characters[c].tx1 = (float)(pen_x + g->bitmap.width) / atlas_width;
        characters[c].ty1 = (float)(pen_y + g->bitmap.rows) / atlas_height;
        characters[c].width = g->bitmap.width;
        characters[c].height = g->bitmap.rows;
        characters[c].bearing_x = g->bitmap_left;
        characters[c].bearing_y = g->bitmap_top;
        characters[c].advance = g->advance.x >> 6;

        pen_x += g->bitmap.width + 1;  // +1 for padding
        row_height =
            (g->bitmap.rows > row_height) ? g->bitmap.rows : row_height;
    }

    // Create OpenGL texture
    glGenTextures(1, &text_texture);
    glBindTexture(GL_TEXTURE_2D, text_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlas_width, atlas_height, 0, GL_RED,
                 GL_UNSIGNED_BYTE, atlas_buffer);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    free(atlas_buffer);

    return true;
}

void set_pty_fd(int fd) { g_pty_fd = fd; }

void key_callback(GLFWwindow* g_window, int key, int scancode, int action,
                  int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (g_pty_fd < 0) return;

    switch (key) {
        case GLFW_KEY_ENTER:
            write(g_pty_fd, "\n", 1);
            break;
        case GLFW_KEY_BACKSPACE:
            write(g_pty_fd, "\x7f", 1);  // DEL character
            break;
        case GLFW_KEY_TAB:
            write(g_pty_fd, "\t", 1);
            break;
        case GLFW_KEY_ESCAPE:
            write(g_pty_fd, "\x1b", 1);  // ESC character
            break;
        case GLFW_KEY_UP:
            write(g_pty_fd, "\x1b[A", 3);  // ANSI escape sequence for up arrow
            break;
        case GLFW_KEY_DOWN:
            write(g_pty_fd, "\x1b[B", 3);
            break;
        case GLFW_KEY_RIGHT:
            write(g_pty_fd, "\x1b[C", 3);
            break;
        case GLFW_KEY_LEFT:
            write(g_pty_fd, "\x1b[D", 3);
            break;
        // Ctrl key combinations
        case GLFW_KEY_C:
            if (mods & GLFW_MOD_CONTROL) write(g_pty_fd, "\x03", 1);  // Ctrl+C
            break;
        case GLFW_KEY_D:
            if (mods & GLFW_MOD_CONTROL) write(g_pty_fd, "\x04", 1);  // Ctrl+D
            break;
    }
}

void char_callback(GLFWwindow* g_window, uint32_t codepoint) {
    if (g_pty_fd < 0) return;

    char buf[4];
    int len = 0;

    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        len = 1;
    } else if (codepoint < 0x800) {
        buf[0] = 0xC0 | (codepoint >> 6);
        buf[1] = 0x80 | (codepoint & 0x3F);
        len = 2;
    } else if (codepoint < 0x10000) {
        buf[0] = 0xE0 | (codepoint >> 12);
        buf[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        buf[2] = 0x80 | (codepoint & 0x3F);
        len = 3;
    } else {
        buf[0] = 0xF0 | (codepoint >> 18);
        buf[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        buf[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        buf[3] = 0x80 | (codepoint & 0x3F);
        len = 4;
    }

    write(g_pty_fd, buf, len);
}

static bool init_rect_rendering(int fb_width, int fb_height) {
    const char* rect_vertex_src =
        "#version 330 core\n"
        "layout (location = 0) in vec2 position;\n"
        "uniform mat4 projection;\n"
        "void main() {\n"
        "    gl_Position = projection * vec4(position, 0.0, 1.0);\n"
        "}\n";

    const char* rect_fragment_src =
        "#version 330 core\n"
        "out vec4 color;\n"
        "uniform vec4 rectColor;\n"
        "void main() {\n"
        "    color = rectColor;\n"
        "}\n";

    rect_shader_program =
        create_shader_program(rect_vertex_src, rect_fragment_src);

    // Set up projection matrix (same as text rendering)
    glUseProgram(rect_shader_program);
    GLint projection_loc =
        glGetUniformLocation(rect_shader_program, "projection");

    float projection[16] = {2.0f / fb_width,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            -2.0f / fb_height,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            -1.0f,
                            0.0f,
                            -1.0f,
                            1.0f,
                            0.0f,
                            1.0f};
    glUniformMatrix4fv(projection_loc, 1, GL_FALSE, projection);
    glUseProgram(0);

    // Create VAO and VBO
    glGenVertexArrays(1, &rect_vao);
    glGenBuffers(1, &rect_vbo);

    glBindVertexArray(rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, rect_vbo);

    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 2, NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return true;
}

bool window_init(const char* title, int width, int height) {
    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
#endif
    // no window decoration
    // glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    g_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!g_window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);  // enable vsync

#ifndef __APPLE__
    // Initialize GLEW on Linux (macOS has native OpenGL support)
    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        fprintf(stderr, "Failed to initialize GLEW: %s\n",
                glewGetErrorString(glew_err));
        return false;
    }
#endif

    int fb_width, fb_height;
    glfwGetFramebufferSize(g_window, &fb_width, &fb_height);
    glViewport(0, 0, fb_width, fb_height);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "Could not init FreetType\n");
        return false;
    }

    // Try platform-specific font paths
    const char* font_paths[] = {
#ifdef __APPLE__
        "/Users/s167452/Library/Fonts/FiraCodeNerdFontMono-Regular.ttf",
        "/System/Library/Fonts/Monaco.ttf", "/System/Library/Fonts/Menlo.ttc",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
#endif
        NULL};

    bool font_loaded = false;
    for (int i = 0; font_paths[i] != NULL; i++) {
        if (FT_New_Face(ft, font_paths[i], 0, &face) == 0) {
            font_loaded = true;
            break;
        }
    }

    if (!font_loaded) {
        fprintf(stderr, "Could not open any font\n");
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, 24);

    // Create texture atlas
    if (!create_texture_atlas()) {
        fprintf(stderr, "Failed to create texture atlas\n");
        return false;
    }

    // Create shader program
    const char* vertex_shader_src =
        "#version 330 core\n"
        "layout (location = 0) in vec4 vertex;\n"
        "out vec2 TexCoords;\n"
        "uniform mat4 projection;\n"
        "void main() {\n"
        "    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
        "    TexCoords = vertex.zw;\n"
        "}\n";

    const char* fragment_shader_src =
        "#version 330 core\n"
        "in vec2 TexCoords;\n"
        "out vec4 color;\n"
        "uniform sampler2D text;\n"
        "uniform vec3 textColor;\n"
        "void main() {\n"
        "    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);\n"
        "    color = vec4(textColor, 1.0) * sampled;\n"
        "}\n";

    text_shader_program =
        create_shader_program(vertex_shader_src, fragment_shader_src);

    // Set up projection matrix
    glUseProgram(text_shader_program);
    GLint projection_loc =
        glGetUniformLocation(text_shader_program, "projection");

    // Create orthographic projection matrix for top-down coordinates
    // Equivalent to glOrtho(0, fb_width, fb_height, 0, -1, 1)
    float projection[16] = {2.0f / fb_width,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            -2.0f / fb_height,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            -1.0f,
                            0.0f,
                            -1.0f,
                            1.0f,
                            0.0f,
                            1.0f};
    glUniformMatrix4fv(projection_loc, 1, GL_FALSE, projection);
    glUseProgram(0);

    // Set up VAO and VBO for dynamic rendering
    glGenVertexArrays(1, &text_vao);
    glGenBuffers(1, &text_vbo);

    glBindVertexArray(text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (!init_rect_rendering(fb_width, fb_height)) {
        fprintf(stderr, "Failed to initialize rectangle rendering\n");
        return false;
    }
    glfwSetCharCallback(g_window, char_callback);
    glfwSetKeyCallback(g_window, key_callback);

    return true;
}

void window_draw_rect(float x, float y, float w, float h, float r, float g,
                      float b) {
    glUseProgram(rect_shader_program);

    // Set the color uniform
    GLint color_loc = glGetUniformLocation(rect_shader_program, "rectColor");
    glUniform4f(color_loc, r, g, b, 1.0f);  // RGB + alpha

    // Define the 6 vertices for 2 triangles
    float vertices[12] = {
        // Triangle 1
        x, y,          // Top-left
        x, y + h,      // Bottom-left
        x + w, y + h,  // Bottom-right

        // Triangle 2
        x, y,          // Top-left
        x + w, y + h,  // Bottom-right
        x + w, y       // Top-right
    };

    // Upload vertices to GPU
    glBindVertexArray(rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, rect_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    // Draw the 6 vertices as triangles
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Cleanup
    glBindVertexArray(0);
    glUseProgram(0);
}

void window_clear(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void window_swap(void) { glfwSwapBuffers(g_window); }

void window_poll(void) { glfwPollEvents(); }

bool window_should_close(void) { return glfwWindowShouldClose(g_window); }

void window_get_size(int* window_width, int* window_height) {
    glfwGetFramebufferSize(g_window, window_width, window_height);
}

void window_shutdown(void) {
    // Clean up OpenGL resources
    glDeleteVertexArrays(1, &text_vao);
    glDeleteBuffers(1, &text_vbo);
    glDeleteProgram(text_shader_program);
    glDeleteVertexArrays(1, &rect_vao);
    glDeleteBuffers(1, &rect_vbo);
    glDeleteProgram(rect_shader_program);
    glDeleteTextures(1, &text_texture);

    // Clean up FreeType
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    if (g_window) glfwDestroyWindow(g_window);
    glfwTerminate();
}

void window_set_text_color(float r, float g, float b) {
    glUseProgram(text_shader_program);
    GLint text_color_loc =
        glGetUniformLocation(text_shader_program, "textColor");
    glUniform3f(text_color_loc, r, g, b);
    glUseProgram(0);
}

void window_draw_text(float x, float y, const char* text) {
    // Use the shader program
    glUseProgram(text_shader_program);

    // Bind the texture atlas
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, text_texture);

    // Bind VAO
    glBindVertexArray(text_vao);

    // Iterate through all characters
    for (const char* p = text; *p; p++) {
        unsigned char c = *p;
        if (c < 32 || c >= 128) continue;

        Character ch = characters[c];

        float xpos = x + ch.bearing_x;
        float ypos = y - ch.bearing_y;

        float w = ch.width;
        float h = ch.height;

        // Update VBO for each character (6 vertices = 2 triangles)
        // Swap ty0/ty1 to flip glyphs right-side up
        float vertices[6][4] = {
            {xpos, ypos + h, ch.tx0, ch.ty1},  // top-left
            {xpos, ypos, ch.tx0, ch.ty0},      // bottom-left
            {xpos + w, ypos, ch.tx1, ch.ty0},  // bottom-right

            {xpos, ypos + h, ch.tx0, ch.ty1},     // top-left
            {xpos + w, ypos, ch.tx1, ch.ty0},     // bottom-right
            {xpos + w, ypos + h, ch.tx1, ch.ty1}  // top-right
        };

        // Update content of VBO memory
        glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        // Render quad
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Advance cursor for next glyph
        x += ch.advance;
    }

    // Unbind everything
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}
