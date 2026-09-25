CC ?= cc

BUILD_DIR := build
TEST_BINARY := $(BUILD_DIR)/test_xfsm_native
SOURCES := test_xfsm_native.c ../../xfsm_native.c

CFLAGS := -std=c99 -O1 -g3 -Wall -Wextra -Werror -Wconversion \
  -Wsign-conversion -Wshadow -Wstrict-prototypes -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS := -I../..
LDFLAGS := -fsanitize=address,undefined

.PHONY: all clean test

all: test

$(TEST_BINARY): $(SOURCES) ../../xfsm_native.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(BUILD_DIR):
	mkdir -p $@

test: $(TEST_BINARY)
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1 $(TEST_BINARY)

clean:
	rm -rf $(BUILD_DIR)
