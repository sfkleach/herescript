CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -D_POSIX_C_SOURCE=200809L -Werror
TARGET = _build/herescript
TEST_TARGET = _build/test-herescript
SRC = herescript.c
TEST_SRC = test-herescript.c

.PHONY: all clean install test-herescript

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(SRC)
	mkdir -p _build
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

$(TEST_TARGET): $(TEST_SRC)
	mkdir -p _build
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_SRC)

clean:
	rm -f $(TARGET) $(TEST_TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/herescript
