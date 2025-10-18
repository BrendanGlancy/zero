CC      := clang
CFLAGS  := -Wall -Wextra -std=c99 -Ilib $(shell pkg-config --cflags glfw3 freetype2)

# Platform-specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    LDFLAGS := $(shell pkg-config --libs glfw3 freetype2) -framework OpenGL -framework Cocoa -framework IOKit
else
    # Linux
    LDFLAGS := $(shell pkg-config --libs glfw3 freetype2) -lGL -lGLEW -lX11
endif

SRC     := src/term.c src/window.c
OBJ     := $(SRC:.c=.o)
BIN     := term

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)
