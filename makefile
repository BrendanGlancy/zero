CC      := clang
CFLAGS  := -Wall -Wextra -std=c99 -Ilib $(shell pkg-config --cflags glfw3 freetype2)
LDFLAGS := $(shell pkg-config --libs glfw3 freetype2) -lGL -lX11
SRC     := term.c lib/window.c
OBJ     := $(SRC:.c=.o)
BIN     := term

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)
