#include "window.h"

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
#include <string.h>
#include <stdlib.h>
#include FT_FREETYPE_H

// Character info for texture atlas
typedef struct {
    float tx0, ty0, tx1, ty1;  // texture coords in atlas
    float width, height;        // size in pixels
    float bearing_x, bearing_y; // offset from baseline
    float advance;              // horizontal advance
} Character;

static GLFWwindow *g_window = NULL;
static FT_Library ft;
static FT_Face face;

// Modern OpenGL text rendering state
static GLuint text_vao, text_vbo;
static GLuint text_shader_program;
static GLuint text_texture;
static Character characters[128];
static int atlas_width = 512;
static int atlas_height = 512;

static void error_callback(int error, const char *desc) {
    fprintf(stderr, "GLFW Error (%d) %s\n", error, desc);
}

// Compile a shader and check for errors
static GLuint compile_shader(GLenum type, const char *source) {
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
static GLuint create_shader_program(const char *vertex_src, const char *fragment_src) {
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
    unsigned char *atlas_buffer = calloc(atlas_width * atlas_height, 1);
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
                    atlas_buffer[y * atlas_width + x] = g->bitmap.buffer[row * g->bitmap.width + col];
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

        pen_x += g->bitmap.width + 1; // +1 for padding
        row_height = (g->bitmap.rows > row_height) ? g->bitmap.rows : row_height;
    }

    // Create OpenGL texture
    glGenTextures(1, &text_texture);
    glBindTexture(GL_TEXTURE_2D, text_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlas_width, atlas_height, 0, GL_RED, GL_UNSIGNED_BYTE, atlas_buffer);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    free(atlas_buffer);

    return true;
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

    // Initialize GLEW after creating OpenGL context
    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        fprintf(stderr, "Failed to initialize GLEW: %s\n", glewGetErrorString(glew_err));
        return false;
    }

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
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, 18);

    // Create texture atlas
    if (!create_texture_atlas()) {
        fprintf(stderr, "Failed to create texture atlas\n");
        return false;
    }

    // Create shader program
    const char *vertex_shader_src =
        "#version 330 core\n"
        "layout (location = 0) in vec4 vertex;\n"
        "out vec2 TexCoords;\n"
        "uniform mat4 projection;\n"
        "void main() {\n"
        "    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
        "    TexCoords = vertex.zw;\n"
        "}\n";

    const char *fragment_shader_src =
        "#version 330 core\n"
        "in vec2 TexCoords;\n"
        "out vec4 color;\n"
        "uniform sampler2D text;\n"
        "uniform vec3 textColor;\n"
        "void main() {\n"
        "    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);\n"
        "    color = vec4(textColor, 1.0) * sampled;\n"
        "}\n";

    text_shader_program = create_shader_program(vertex_shader_src, fragment_shader_src);

    // Set up projection matrix
    glUseProgram(text_shader_program);
    GLint projection_loc = glGetUniformLocation(text_shader_program, "projection");

    // Create orthographic projection matrix (same as glOrtho)
    float projection[16] = {
        2.0f / fb_width, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / fb_height, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
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
    // Clean up OpenGL resources
    glDeleteVertexArrays(1, &text_vao);
    glDeleteBuffers(1, &text_vbo);
    glDeleteProgram(text_shader_program);
    glDeleteTextures(1, &text_texture);

    // Clean up FreeType
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    if (g_window)
        glfwDestroyWindow(g_window);
    glfwTerminate();
}

void window_draw_text(float x, float y, const char *text) {
    // Use the shader program
    glUseProgram(text_shader_program);

    // Set text color to white
    GLint text_color_loc = glGetUniformLocation(text_shader_program, "textColor");
    glUniform3f(text_color_loc, 1.0f, 1.0f, 1.0f);

    // Bind the texture atlas
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, text_texture);

    // Bind VAO
    glBindVertexArray(text_vao);

    // Iterate through all characters
    for (const char *p = text; *p; p++) {
        unsigned char c = *p;
        if (c < 32 || c >= 128)
            continue;

        Character ch = characters[c];

        float xpos = x + ch.bearing_x;
        float ypos = y - (ch.height - ch.bearing_y);

        float w = ch.width;
        float h = ch.height;

        // Update VBO for each character (6 vertices = 2 triangles)
        float vertices[6][4] = {
            { xpos,     ypos + h,   ch.tx0, ch.ty0 },  // top-left
            { xpos,     ypos,       ch.tx0, ch.ty1 },  // bottom-left
            { xpos + w, ypos,       ch.tx1, ch.ty1 },  // bottom-right

            { xpos,     ypos + h,   ch.tx0, ch.ty0 },  // top-left
            { xpos + w, ypos,       ch.tx1, ch.ty1 },  // bottom-right
            { xpos + w, ypos + h,   ch.tx1, ch.ty0 }   // top-right
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
