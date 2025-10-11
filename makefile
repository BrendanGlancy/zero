CC      := gcc
CFLAGS  := -Wall -Wextra -std=c99 -Ilib
LDFLAGS := -lglfw -lGL -lX11
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
