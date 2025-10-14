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

static uint32_t screen[MAX_ROWS][MAX_COLS]; // 2D array of codepoints
static int term_cols = 122, term_rows = 36;
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

void render_terminal(void) {
    int window_width, window_height;
    window_get_size(&window_width, &window_height);

    float char_width = 15.0f, char_height = 20.0f;
    float padding_x = 10.0f;
    float padding_y = 20.0f;

    term_cols = (window_width - padding_x * 2) / char_width;
    term_rows = (window_height - padding_y * 2) / char_height;

    for (int y = 0; y < term_rows; y++) {
        for (int x = 0; x < term_cols; x++) {
            uint32_t cp = screen[y][x];
            if (!cp)
                continue;
            char str[5] = {0};
            if (cp < 128)
                str[0] = (char)cp;
            else
                snprintf(str, sizeof(str), "?");
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
