#ifdef __APPLE__
#include <util.h>
#else
#include <bits/types/struct_timeval.h>
#include <pty.h>
#endif
#include "lib/window.h"
#include <GLFW/glfw3.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define TERM_COLS 80
#define TERM_ROWS 24

#define MAX(a, b) ((a) > (b) ? (a) : (b))

// master file descriptor
static int32_t masterfd;

static uint32_t screen[TERM_ROWS][TERM_COLS]; // 2D array of codepoints
static int cursor_x = 0;
static int cursor_y = 0;

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
        *out_cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                  ((s[2] & 0x3f) << 6) | (s[3] & 0x3F);
        return 4;
    }
    return -1;
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
        uint32_t codepoint;
        int32_t len = utf8decode(&buf[iter], &codepoint);

        if (len == -1 || len > buflen)
            break;

        if (codepoint == 10) {
            cursor_x = 0;
            cursor_y++;
        } else {
            screen[cursor_y][cursor_x] = codepoint;
            cursor_x++;

            if (cursor_x >= TERM_COLS) { // wrap to next line
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

void render_terminal(void) {
    float char_width = 10.0f;
    float char_height = 20.0f;

    for (int y = 0; y < TERM_ROWS; y++) {
        for (int x = 0; x < TERM_COLS; x++) { 
            uint32_t cp = screen[x][y];
            if (cp == 0) {
                continue;
            }
            // calculate pixel position
            float pixel_x = x * char_width;
            float pixel_y = y * char_height;

            // convert codepoint to string
            char str[2] = {(char)cp, '\0'};
            window_draw_text(pixel_x, pixel_y, str);
        }
    }
}

// creates a pty and fork + event loop
int main(void) {
    // forkpty() = openpty + fork()
    // parent gets master file descriptor
    if (forkpty(&masterfd, NULL, NULL, NULL) == 0) {
        // child replaces itself with zsh
        execlp("/usr/bin/zsh", "zsh", NULL);
        perror("execlp");
        exit(1);
    }

    if (!window_init("myterm", 1280, 720)) {
        fprintf(stderr, "Failed to init window");
        return 1;
    }

    bool running = true;

    // event loop
    fd_set fdset;
    while (running) {
        FD_ZERO(&fdset);
        FD_SET(masterfd, &fdset);
        struct timeval tv = {0, 0}; // non-block select

        // uses select to block until data is avaliable to read from masterfd
        select(masterfd + 1, &fdset, NULL, NULL, NULL);

        if (FD_ISSET(masterfd, &fdset)) {
            readfrompty();
        }

        window_clear(0.05f, 0.05f, 0.06f);
        render_terminal();
        window_swap();

        glfwWaitEventsTimeout(0.01);
        window_poll();

        if (window_should_close()) {
            running = false;
        }
    }

    window_shutdown();
    return 0;
}
