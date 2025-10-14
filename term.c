#ifdef __APPLE__
#include <util.h>
#else
#include <bits/types/struct_timeval.h>
#include <pty.h>
#endif
#include "lib/window.h"
#include <GLFW/glfw3.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define MAX_COLS 192
#define MAX_ROWS 108

#define MAX(a, b) ((a) > (b) ? (a) : (b))

// master file descriptor
static int32_t masterfd;

typedef struct {
    uint32_t codepoint;
    uint8_t fg_color;
    uint8_t bg_color;
    uint8_t bold;
} Cell;
static Cell screen[MAX_ROWS][MAX_COLS]; // 2D array of codepoints
static uint8_t current_fg_color = 7;
static uint8_t current_bg_color = 0;
static uint8_t current_bold = 0;

static int term_cols = 128, term_rows = 36;
static int cursor_x = 0, cursor_y = 0;

// takes a point to a UTF-8 byte sequence and decodes a single Unicode codepoint
// from it.
int32_t utf8decode(const char *s, uint32_t *out_cp) {
    unsigned char c = s[0];
    if (c < 0x80) {
        *out_cp = c;
        return 1;
    } else if ((c >> 5) == 0x6) {
        *out_cp = ((c & 0x1f) << 6) | (s[1] & 0x3F);
        return 2;
    } else if ((c >> 4) == 0xE) {
        *out_cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    } else if ((c >> 3) == 0x1E) {
        *out_cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3F);
        return 4;
    }
    return -1;
}

int parse_ansii_escape(const char *buf, uint32_t buflen) {
    if (buflen < 2 || buf[0] != '\x1b')
        return 0;

    // checks for CSI sequence: ESC
    if (buf[1] != '[')
        return 2;

    uint32_t i = 2;
    while (i < buflen && (buf[i] == ';' || (buf[i] >= '0' && buf[i] <= '9')))
        i++;

    if (i >= buflen)
        return 0;

    char cmd = buf[i];

    if (cmd != 'm')
        return i + 1;

    int params[16];
    int param_count = 0;
    int num = 0;
    bool has_num = false;

    for (uint32_t j = 2; j < i; j++) {
        if (buf[j] >= '0' && buf[j] <= '9') {
            num = num * 10 + (buf[j] - '0');
            has_num = true;
        } else if (buf[j] == ';') {
            params[param_count++] = has_num ? num : 0;
            num = 0;
            has_num = false;
        }
    }

    if (has_num || i == 2)
        params[param_count++] = has_num ? num : 0;

    for (int p = 0; p < param_count; p++) {
        if (params[p] == 0) {
            current_fg_color = 7;
            current_bg_color = 0;
            current_bold = 0;
        } else if (params[p] == 1) {
            current_bold = 1;
        } else if (params[p] >= 30 && params[p] <= 37) {
            current_fg_color = params[p] - 30;
        } else if (params[p] >= 40 && params[p] <= 47) {
            current_bg_color = params[p] - 40;
        } else if (params[p] >= 90 && params[p] <= 97) {
            current_fg_color = params[p] - 90 + 8;
        } else if (params[p] >= 100 && params[p] <= 107) {
            current_bg_color = params[p] - 100 + 8;
        }
    }

    return i + 1;
}

// reads whatever byte is currently avaliable form the PTY decodes them prints
// their Unicode codepoint to the console
size_t readfrompty(void) {
    static char buf[SHRT_MAX];
    static uint32_t buflen = 0;

    int nbytes = read(masterfd, buf + buflen, sizeof(buf) - buflen);
    buflen += nbytes;

    uint32_t iter = 0;
    while (iter < buflen) {
        if (buf[iter] == '\x1b') {
            int consumed = parse_ansii_escape(&buf[iter], buflen - iter);
            if (consumed == 0)
                break;
            iter += consumed;
            continue;
        }

        uint32_t codepoint;
        int32_t len = utf8decode(&buf[iter], &codepoint);

        if (len == -1 || len > buflen)
            break;

        if (codepoint == 10) {
            cursor_x = 0;
            cursor_y++;
        } else if (codepoint == 8 || codepoint == 127) {
            // backspace
            if (cursor_x > 0)
                cursor_x--;
        } else if (codepoint == 13) {
            // return
            cursor_x = 0;
        } else {
            screen[cursor_y][cursor_x].codepoint = codepoint;
            screen[cursor_y][cursor_x].fg_color = current_fg_color;
            screen[cursor_y][cursor_x].bg_color = current_bg_color;
            screen[cursor_y][cursor_x].bold = current_bold;
            cursor_x++;

            if (cursor_x >= term_cols) { // wrap to next line
                cursor_x = 0;
                cursor_y++;
            }
        }

        iter += len;
    }

    if (iter < buflen) {
        memmove(buf, buf + iter, buflen - iter);
    }
    buflen -= iter;
    return nbytes;
}

void get_ansi_color(uint8_t color, uint8_t bold, float *r, float *g, float *b) {
    // Basic 16 colors (0-7 normal, 8-15 bright)
    static const float colors[16][3] = {
        {0.0f, 0.0f, 0.0f}, // 0: Black
        {0.8f, 0.0f, 0.0f}, // 1: Red
        {0.0f, 0.8f, 0.0f}, // 2: Green
        {0.8f, 0.8f, 0.0f}, // 3: Yellow
        {0.0f, 0.0f, 0.8f}, // 4: Blue
        {0.8f, 0.0f, 0.8f}, // 5: Magenta
        {0.0f, 0.8f, 0.8f}, // 6: Cyan
        {0.8f, 0.8f, 0.8f}, // 7: White
        {0.5f, 0.5f, 0.5f}, // 8: Bright Black (Gray)
        {1.0f, 0.0f, 0.0f}, // 9: Bright Red
        {0.0f, 1.0f, 0.0f}, // 10: Bright Green
        {1.0f, 1.0f, 0.0f}, // 11: Bright Yellow
        {0.0f, 0.0f, 1.0f}, // 12: Bright Blue
        {1.0f, 0.0f, 1.0f}, // 13: Bright Magenta
        {0.0f, 1.0f, 1.0f}, // 14: Bright Cyan
        {1.0f, 1.0f, 1.0f}, // 15: Bright White
    };

    int idx = (color % 16);
    if (bold && idx < 8)
        idx += 8; // Use bright version for bold

    *r = colors[idx][0];
    *g = colors[idx][1];
    *b = colors[idx][2];
}

void render_terminal(void) {
    int window_width, window_height;
    window_get_size(&window_width, &window_height);

    float char_width = 15.0f, char_height = 25.0f;
    float padding_x = 10.0f;
    float padding_y = 20.0f;

    term_cols = (window_width - padding_x * 2) / char_width;
    term_rows = (window_height - padding_y * 2) / char_height;

    for (int y = 0; y < term_rows; y++) {
        for (int x = 0; x < term_cols; x++) {
            Cell cell = screen[y][x];
            if (!cell.codepoint)
                continue;
            char str[5] = {0};
            if (cell.codepoint < 128)
                str[0] = (char)cell.codepoint;
            else
                snprintf(str, sizeof(str), "?");
            float r, g, b;
            get_ansi_color(cell.fg_color, cell.bold, &r, &g, &b);
            window_set_text_color(r, g, b);

            window_draw_text(padding_x + x * char_width, padding_y + y * char_height, str);
        }
    }
}

// creates a pty and fork + event loop
int main(void) {
    // forkpty() = openpty + fork()
    // parent gets master file descriptor
    if (forkpty(&masterfd, NULL, NULL, NULL) == 0) {
        // child replaces itself with zsh
        execlp("/opt/homebrew/bin/zsh", "zsh", NULL);
        perror("execlp");
        exit(1);
    }
    set_pty_fd(masterfd);

    if (!window_init("myterm", 1280, 720)) {
        fprintf(stderr, "Failed to init window");
        return 1;
    }

    struct pollfd fds[1];
    fds[0].fd = masterfd;
    fds[0].events = POLLIN;

    bool running = true;
    bool dirty = true;

    while (running) {
        int ret = poll(fds, 1, 20); // block up to 20ms
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            readfrompty();
            dirty = true;
        }

        if (dirty) {
            window_clear(0.05f, 0.05f, 0.06f);
            render_terminal();
            window_swap();
            dirty = false;
        }

        glfwPollEvents();
        if (window_should_close())
            running = false;
    }

    window_shutdown();
    return 0;
}
