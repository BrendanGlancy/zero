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

#include "platform.h"
#include "window.h"

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

typedef struct {
  char cmd[2];
  int params[16];
  int nparams;
  char prefix;
} CSISequence;

static CSISequence current_csi;
static uint32_t recent_codepoint = 0;

static Cell screen[MAX_ROWS][MAX_COLS];
static uint8_t current_fg_color = 7;
static uint8_t current_bg_color = 0;
static uint8_t current_bold = 0;

static int term_cols = 128, term_rows = 36;
static int cursor_x = 0, cursor_y = 0;

// Selection state
static bool selecting = false;
static int sel_start_x = 0, sel_start_y = 0;
static int sel_end_x = 0, sel_end_y = 0;
static float cached_char_width = 18.0f;
static float cached_char_height = 35.0f;
static float cached_padding_x = 10.0f;
static float cached_padding_y = 20.0f;

static inline void clearcell(Cell* cell) {
  cell->codepoint = 0;
  cell->fg_color = 7;
  cell->bg_color = 0;
  cell->bold = 0;
}

static inline Cell* cellat(int x, int y) {
  if (x < 0 || x >= term_cols || y < 0 || y >= term_rows) return NULL;
  return &screen[y][x];
}

void moveto(int x, int y) {
  cursor_x = x < 0 ? 0 : (x >= term_cols ? term_cols - 1 : x);
  cursor_y = y < 0 ? 0 : (y >= term_rows ? term_rows - 1 : y);
}

void scrollup(int top, int n) {
  if (n <= 0 || top >= term_rows) return;

  int bottom = term_rows;
  n = n > (bottom - top) ? (bottom - top) : n;

  for (int y = top; y < bottom - n; y++) {
    memcpy(screen[y], screen[y + n], sizeof(Cell) * term_cols);
  }

  for (int y = bottom - n; y < bottom; y++) {
    for (int x = 0; x < term_cols; x++) {
      clearcell(&screen[y][x]);
    }
  }
}

void scrolldown(int top, int n) {
  if (n <= 0 || top >= term_rows) return;

  int bottom = term_rows;
  n = n > (bottom - top) ? (bottom - top) : n;

  for (int y = bottom - 1; y >= top + n; y--) {
    memcpy(screen[y], screen[y - n], sizeof(Cell) * term_cols);
  }

  for (int y = top; y < top + n; y++) {
    for (int x = 0; x < term_cols; x++) {
      clearcell(&screen[y][x]);
    }
  }
}

void insertblankchars(int n) {
  if (n <= 0) return;

  int end = cursor_x + n;
  if (end > term_cols) end = term_cols;

  for (int x = term_cols - 1; x >= end; x--) {
    screen[cursor_y][x] = screen[cursor_y][x - 1];
  }

  for (int x = cursor_x; x < end; x++) {
    clearcell(&screen[cursor_y][x]);
  }
}

void deletecells(int n) {
  if (n <= 0) return;

  for (int x = cursor_x; x < term_cols - n; x++) {
    screen[cursor_y][x] = screen[cursor_y][x + n];
  }

  for (int x = term_cols - n; x < term_cols; x++) {
    clearcell(&screen[cursor_y][x]);
  }
}

static const int8_t utf8_length[256] = {
    [0x00 ... 0x7F] = 1,  // 0xxxxxxx
    [0xC0 ... 0xDF] = 2,  // 110xxxxx
    [0xE0 ... 0xEF] = 3,  // 1110xxxx
    [0xF0 ... 0xF7] = 4,  // 11110xxx

    [0x80 ... 0xBF] = -1,  // Continuation bytes
    [0xF8 ... 0xFF] = -1,  // Invalid
};

// takes a point to a UTF-8 byte sequence and decodes a single Unicode codepoint from it.
int32_t utf8decode(const char* s, uint32_t* out_cp) {
  unsigned char c = (unsigned char)s[0];
  int32_t len = utf8_length[c];

  if (len <= 0) return -1;

  switch (len) {
    case 1:
      *out_cp = c;
      return 1;

    case 2:
      *out_cp = ((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
      return 2;

    case 3:
      *out_cp = ((c & 0x0F) << 12) | ((unsigned char)s[1] & 0x3F << 6) | ((unsigned char)s[2] & 0x3F);
      return 3;

    case 4:
      *out_cp = ((c & 0x07) << 18) | ((unsigned char)s[1] & 0x3F << 12) | ((unsigned char)s[2] & 0x3F << 6) |
                ((unsigned char)s[3] & 0x3F);
      return 4;
  }

  return -1;
}

int utf8encode(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }

  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }

  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }

  if (cp < 0x10FFFF) {
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }

  return -1;
}

void parse_csi(void) {
  uint32_t dp = current_csi.nparams > 0 ? current_csi.params[0] : 1;

  switch (current_csi.cmd[0]) {
    case 'm': {
      for (int p = 0; p < current_csi.nparams; p++) {
        int param = current_csi.params[p];
        if (param == 0) {
          current_fg_color = 7;
          current_bg_color = 0;
          current_bold = 0;
        } else if (param == 1) {
          current_bold = 1;
        } else if (param >= 30 && param <= 37) {
          current_fg_color = param - 30;
        } else if (param >= 40 && param <= 47) {
          current_bg_color = param - 40;
        } else if (param >= 90 && param <= 97) {
          current_fg_color = param - 90 + 8;
        } else if (param >= 100 && param <= 107) {
          current_bg_color = param - 100 + 8;
        }
      }

      break;
    }

    case 'A':
      moveto(cursor_x, cursor_y - dp);
      break;

    case 'B':
    case 'e':
      moveto(cursor_x, cursor_y + dp);
      break;

    case 'C':
    case 'a':
      moveto(cursor_x + dp, cursor_y);
      break;

    case 'D':
      moveto(cursor_x - dp, cursor_y);
      break;

    case 'E':
      moveto(0, cursor_y + dp);
      break;

    case 'F':
      moveto(0, cursor_y - dp);
      break;

    case 'G':
    case '`':
      moveto(dp - 1, cursor_y);
      break;

    case 'H':
    case 'f': {
      uint32_t y = current_csi.nparams > 0 ? current_csi.params[0] : 1;
      uint32_t x = current_csi.nparams > 1 ? current_csi.params[1] : 1;
      moveto(x - 1, y - 1);
    }

    case 'J': {
      int32_t op = current_csi.params[0];
      if (op == 0) {
        for (int x = cursor_x; x < term_cols; x++) {
          clearcell(&screen[cursor_y][x]);
        }
        for (int y = cursor_y + 1; y < term_rows; y++) {
          for (int x = 0; x < term_cols; x++) {
            clearcell(&screen[y][x]);
          }
        }
      } else if (op == 1) {
        for (int y = 0; y < cursor_y; y++) {
          for (int x = 0; x < term_cols; x++) {
            clearcell(&screen[y][x]);
          }
        }
        for (int x = 0; x <= cursor_x; x++) {
          clearcell(&screen[cursor_y][x]);
        }
      } else if (op == 2) {
        for (int y = 0; y < term_rows; y++) {
          for (int x = 0; x <= term_cols; x++) {
            clearcell(&screen[y][x]);
          }
        }
      }
      break;
    }

    case 'K': {
      int32_t op = current_csi.params[0];
      if (op == 0) {
        // clear line right of cursor
        for (int x = cursor_x; x < term_cols; x++) {
          clearcell(&screen[cursor_y][x]);
        }
      } else if (op == 1) {
        // clear line left
        for (int x = 0; x <= cursor_x; x++) {
          clearcell(&screen[cursor_y][x]);
        }
      } else if (op == 2) {
        // Entire line
        for (int x = 0; x < term_cols; x++) {
          clearcell(&screen[cursor_y][x]);
        }
      }
      break;
    }

    case 'L':  // Insert n Lines
      scrolldown(cursor_y, dp);
      break;

    case 'M':
      scrollup(cursor_y, dp);
      break;

    case 'P':
      deletecells(dp);
      break;

    case 'S':
      if (current_csi.prefix != '?') {
        scrollup(0, dp);
      }
      break;

    case 'X':
      for (int x = cursor_x; x < cursor_x + (int)dp && x < term_cols; x++) {
        clearcell(&screen[cursor_y][x]);
      }
      break;

    case '@':
      insertblankchars(dp);
      break;

    case 'b':
      for (uint32_t i = 0; i < dp && i < SHRT_MAX; i++) {
        if (recent_codepoint) {
          screen[cursor_y][cursor_x].codepoint = recent_codepoint;
          screen[cursor_y][cursor_x].fg_color = current_fg_color;
          screen[cursor_y][cursor_x].bg_color = current_bg_color;
          screen[cursor_y][cursor_x].bold = current_bold;
          cursor_x++;
          if (cursor_x >= term_cols) {
            cursor_x = 0;
            cursor_y++;
          }
        }
      }
      break;

    case 'd':
      moveto(cursor_x, dp - 1);
      break;

    case 'n':  // device status report
      // would need to write back to PTY
      break;

    default:
      break;
  }
}

int parse_ansii_escape(const char* buf, uint32_t buflen) {
  if (buflen < 2 || buf[0] != '\x1b') return 0;

  if (buf[1] != '[') return 2;  // Unknown escape, skip ESC + next char

  uint32_t i = 2;

  memset(&current_csi, 0, sizeof(current_csi));

  // Skip optional '?' (used in private mode sequences like bracketed paste)
  if (i < buflen && buf[i] == '?') {
    current_csi.prefix = '?';
    i++;
  }

  int num = 0;
  bool has_num = false;

  while (i < buflen && (buf[i] == ';' || (buf[i] >= '0' && buf[i] <= '9'))) {
    if (buf[i] >= '0' && buf[i] <= '9') {
      num = num * 10 + (buf[i] - '0');
      has_num = true;
    } else if (buf[i] == ';') {
      current_csi.params[current_csi.nparams++] = has_num ? num : 0;
      num = 0;
      if (current_csi.nparams >= 16) break;
    }
    i++;
  }

  if (has_num && current_csi.nparams < 16) {
    current_csi.params[current_csi.nparams++] = num;
  }

  if (i >= buflen) return 0;

  current_csi.cmd[0] = buf[i];
  current_csi.cmd[1] = '\0';

  parse_csi();

  return i + 1;
}

// reads byte currently avaliable form the PTY decodes them prints their codepoint to the console
size_t readfrompty(void) {
  static char buf[SHRT_MAX];
  static uint32_t buflen = 0;

  int nbytes = read(masterfd, buf + buflen, sizeof(buf) - buflen);
  buflen += nbytes;

  uint32_t iter = 0;
  while (iter < buflen) {
    if (buf[iter] == '\x1b') {
      int consumed = parse_ansii_escape(&buf[iter], buflen - iter);
      if (consumed == 0) break;
      iter += consumed;
      continue;
    }

    uint32_t codepoint;
    int32_t len = utf8decode(&buf[iter], &codepoint);

    if (len == -1 || len > buflen) break;

    if (codepoint == 10) {
      cursor_x = 0;
      cursor_y++;
    } else if (codepoint == 8 || codepoint == 127) {
      // backspace
      if (cursor_x > 0) cursor_x--;
    } else if (codepoint == 13) {
      // return
      cursor_x = 0;
    } else {
      screen[cursor_y][cursor_x].codepoint = codepoint;
      screen[cursor_y][cursor_x].fg_color = current_fg_color;
      screen[cursor_y][cursor_x].bg_color = current_bg_color;
      screen[cursor_y][cursor_x].bold = current_bold;

      recent_codepoint = codepoint;

      cursor_x++;

      if (cursor_x >= term_cols) {  // wrap to next line
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

void copy_selection_to_clipboard(GLFWwindow* window) {
  int min_y = sel_start_y < sel_end_y ? sel_start_y : sel_end_y;
  int max_y = sel_start_y > sel_end_y ? sel_start_y : sel_end_y;
  int min_x = sel_start_y < sel_end_y
                  ? sel_start_x
                  : (sel_start_y == sel_end_y ? (sel_start_x < sel_end_x ? sel_start_x : sel_end_x) : sel_end_x);
  int max_x = sel_start_y > sel_end_y
                  ? sel_start_x
                  : (sel_start_y == sel_end_y ? (sel_start_x > sel_end_x ? sel_start_x : sel_end_x) : sel_end_x);

  char buffer[MAX_ROWS * MAX_COLS * 4];
  int pos = 0;

  for (int y = min_y; y <= max_y && y < MAX_ROWS; y++) {
    int start_x = (y == min_y) ? min_x : 0;
    int end_x = (y == max_y) ? max_x : term_cols - 1;

    for (int x = start_x; x <= end_x && x < term_cols; x++) {
      if (screen[y][x].codepoint) {
        pos += utf8encode(screen[y][x].codepoint, &buffer[pos]);
      }
    }
    if (y < max_y) buffer[pos++] = '\n';
  }
  buffer[pos] = '\0';

  glfwSetClipboardString(window, buffer);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    int grid_x = (int)((xpos - cached_padding_x) / cached_char_width);
    int grid_y = (int)((ypos - cached_padding_y) / cached_char_height);

    if (grid_x < 0) grid_x = 0;
    if (grid_y < 0) grid_y = 0;
    if (grid_x >= term_cols) grid_x = term_cols - 1;
    if (grid_y >= term_rows) grid_y = term_rows - 1;

    if (action == GLFW_PRESS) {
      selecting = true;
      sel_start_x = sel_end_x = grid_x;
      sel_start_y = sel_end_y = grid_y;
    } else if (action == GLFW_RELEASE) {
      // Keep selection but stop dragging
    }
  }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
  if (selecting && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
    int grid_x = (int)((xpos - cached_padding_x) / cached_char_width);
    int grid_y = (int)((ypos - cached_padding_y) / cached_char_height);

    if (grid_x < 0) grid_x = 0;
    if (grid_y < 0) grid_y = 0;
    if (grid_x >= term_cols) grid_x = term_cols - 1;
    if (grid_y >= term_rows) grid_y = term_rows - 1;

    sel_end_x = grid_x;
    sel_end_y = grid_y;
  }
}

void get_ansi_color(uint8_t color, uint8_t bold, float* r, float* g, float* b) {
  // Basic 16 colors (0-7 normal, 8-15 bright)
  static const float colors[16][3] = {
      {0.0f, 0.0f, 0.0f},  // 0: Black
      {0.8f, 0.0f, 0.0f},  // 1: Red
      {0.0f, 0.8f, 0.0f},  // 2: Green
      {0.8f, 0.8f, 0.0f},  // 3: Yellow
      {0.0f, 0.0f, 0.8f},  // 4: Blue
      {0.8f, 0.0f, 0.8f},  // 5: Magenta
      {0.0f, 0.8f, 0.8f},  // 6: Cyan
      {0.8f, 0.8f, 0.8f},  // 7: White
      {0.5f, 0.5f, 0.5f},  // 8: Bright Black (Gray)
      {1.0f, 0.0f, 0.0f},  // 9: Bright Red
      {0.0f, 1.0f, 0.0f},  // 10: Bright Green
      {1.0f, 1.0f, 0.0f},  // 11: Bright Yellow
      {0.0f, 0.0f, 1.0f},  // 12: Bright Blue
      {1.0f, 0.0f, 1.0f},  // 13: Bright Magenta
      {0.0f, 1.0f, 1.0f},  // 14: Bright Cyan
      {1.0f, 1.0f, 1.0f},  // 15: Bright White
  };

  int idx = (color % 16);
  if (bold && idx < 8) idx += 8;  // Use bright version for bold

  *r = colors[idx][0];
  *g = colors[idx][1];
  *b = colors[idx][2];
}

void render_terminal(void) {
  int window_width, window_height;
  window_get_size(&window_width, &window_height);

  float padding_x = 10.0f;
  float padding_y = 20.0f;

  float avaliable_width = window_width - (padding_x * 2);
  float avaliable_height = window_height - (padding_y * 2);

  const int target_cols = 120;
  const int target_rows = 40;

  float char_width = avaliable_width / target_cols;
  float char_height = avaliable_height / target_rows;

  const float aspect_ratio = 1.9f;  // for monospace char width is usually ~2x char_width

  if (char_height / char_width > aspect_ratio) {
    char_height = char_width * aspect_ratio;  // too tall
  } else {
    char_width = char_height / aspect_ratio;  // too wide
  }

  term_cols = (int)(avaliable_width / char_width);
  term_rows = (int)(avaliable_height / char_height);

  if (term_cols > MAX_COLS) term_cols = MAX_COLS;
  if (term_rows > MAX_ROWS) term_rows = MAX_ROWS;

  // Cache for mouse callbacks
  cached_char_width = char_width;
  cached_char_height = char_height;
  cached_padding_x = padding_x;
  cached_padding_y = padding_y;

  float cursor_x_px = padding_x + cursor_x * char_width;
  float cursor_y_px = padding_y + cursor_y * char_height;

  // Calculate selection bounds
  int min_y = sel_start_y < sel_end_y ? sel_start_y : sel_end_y;
  int max_y = sel_start_y > sel_end_y ? sel_start_y : sel_end_y;
  int min_x = sel_start_y < sel_end_y
                  ? sel_start_x
                  : (sel_start_y == sel_end_y ? (sel_start_x < sel_end_x ? sel_start_x : sel_end_x) : sel_end_x);
  int max_x = sel_start_y > sel_end_y
                  ? sel_start_x
                  : (sel_start_y == sel_end_y ? (sel_start_x > sel_end_x ? sel_start_x : sel_end_x) : sel_end_x);

  // Draw selection highlight
  if (selecting) {
    for (int y = min_y; y <= max_y && y < term_rows; y++) {
      int start_x = (y == min_y) ? min_x : 0;
      int end_x = (y == max_y) ? max_x : term_cols - 1;

      for (int x = start_x; x <= end_x && x < term_cols; x++) {
        window_draw_rect(padding_x + x * char_width, padding_y + y * char_height, char_width, char_height, 0.3f, 0.5f,
                         0.8f);
      }
    }
  }

  for (int y = 0; y < term_rows; y++) {
    for (int x = 0; x < term_cols; x++) {
      Cell cell = screen[y][x];
      if (!cell.codepoint) continue;

      char str[5] = {0};
      if (cell.codepoint < 128) {
        str[0] = (char)cell.codepoint;
      } else {
        utf8encode(cell.codepoint, str);
      }

      float r, g, b;
      get_ansi_color(cell.fg_color, cell.bold, &r, &g, &b);
      window_set_text_color(r, g, b);

      window_draw_text(padding_x + x * char_width, padding_y + y * char_height, str);
    }
  }

  window_draw_rect(cursor_x_px, cursor_y_px, char_width, char_height, 0.8f, 0.8f, 0.8f);
}

int main(void) {
  // forkpty() = openpty + fork() parent gets master file descriptor
  if (forkpty(&masterfd, NULL, NULL, NULL) == 0) {
    // child replaces itself with zsh
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    execlp("/bin/zsh", "zsh", NULL);
    perror("execlp");
    exit(1);
  }
  set_pty_fd(masterfd);

  if (!window_init("myterm", 1280, 720)) {
    fprintf(stderr, "Failed to init window\n");
    return 1;
  }

  GLFWwindow* window = window_get_glfw_window();
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetCursorPosCallback(window, cursor_position_callback);
  set_copy_handler(copy_selection_to_clipboard);

  struct pollfd fds[1];
  fds[0].fd = masterfd;
  fds[0].events = POLLIN;

  bool running = true;
  bool dirty = true;
  int frame_count = 0;

  fprintf(stderr, "Entering main loop...\n");

  while (running) {
    int ret = poll(fds, 1, 2);  // 2ms 144hz

    if (ret > 0 && (fds[0].revents & POLLIN)) {
      do {
        readfrompty();
        dirty = true;
      } while (poll(fds, 1, 0) > 0 && (fds[0].revents & POLLIN));
    }

    if (dirty) {
      window_clear(0.05f, 0.05f, 0.06f);
      render_terminal();
      window_swap();  // blocks until VSync
      dirty = false;

      // Debug: show we're rendering
      if (frame_count++ < 5) {
        fprintf(stderr, "Frame %d rendered\n", frame_count);
      }
    }

    glfwPollEvents();
    if (window_should_close()) running = false;
  }

  window_shutdown();
  return 0;
}
