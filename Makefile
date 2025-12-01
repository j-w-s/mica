CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g
LDFLAGS = -lm

SRC = main.c lexer.c parser.c compiler.c vm.c value.c \
      array.c string.c iterator.c upvalue.c builtins.c

OBJ = $(SRC:.c=.o)

TARGET = mica

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c mica.h mica_internal.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJ) $(TARGET)

test: $(TARGET)
	./$(TARGET) tests/basic.mica
	./$(TARGET) tests/functions.mica
	./$(TARGET) tests/arrays.mica
	./$(TARGET) tests/lang.mica

.PHONY: all clean test