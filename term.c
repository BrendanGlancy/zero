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

#ifdef __APPLE__
    #include <util.h>
#else
    #include <pty.h>
    #include <bits/types/struct_timeval.h>
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static int32_t masterfd;

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
        printf("%i\n", codepoint);
        iter += len;
    }

    if (iter < buflen) {
        memmove(buf, buf + iter, buflen - iter);
    }
    buflen -= iter;
    return nbytes;
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
        window_draw_text(10.0f, 20.0f, "Hello World");
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
