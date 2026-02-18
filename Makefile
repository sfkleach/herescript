CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
TARGET = _build/runscript
TEST_TARGET = _build/test-runscript
SRC = runscript.c
TEST_SRC = test-runscript.c

.PHONY: all clean install test-runscript

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
	install -m 755 $(TARGET) /usr/local/bin/runscript
