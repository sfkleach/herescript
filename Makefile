CC = gcc
CFLAGS = -Wall -Wextra -Wconversion -std=gnu11 -O2 -Werror
TARGET = _build/herescript
TEST_TARGET = _build/test-herescript
UNIT_TEST_TARGET = _build/test_unit
SRC = herescript.c
TEST_SRC = test-herescript.c
UNIT_TEST_SRC = tests/test_unit.c
UNITY_SRC = tests/unity/unity.c

.PHONY: all clean install test-herescript unittest

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(SRC)
	mkdir -p _build
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

$(TEST_TARGET): $(TEST_SRC)
	mkdir -p _build
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_SRC)

$(UNIT_TEST_TARGET): $(UNIT_TEST_SRC) $(UNITY_SRC) $(SRC)
	mkdir -p _build
	$(CC) $(CFLAGS) -Wno-unused-function -DHERESCRIPT_UNIT_TEST -I tests/unity -o $(UNIT_TEST_TARGET) $(UNIT_TEST_SRC) $(UNITY_SRC)

unittest: $(UNIT_TEST_TARGET)
	$(UNIT_TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(UNIT_TEST_TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/herescript
