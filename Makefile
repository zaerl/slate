# The Slate library
#
# This file is distributed under the MIT License. See LICENSE for details.

CC = gcc
WARN_FLAGS = -Wall -Wextra -Wno-unused-parameter -Wshadow -Wpointer-arith -Wvla -Wwrite-strings
CFLAGS = $(WARN_FLAGS) -std=c11
CXX = g++
CXXFLAGS = $(WARN_FLAGS) -std=c++17

# clang-tidy from PATH, else Homebrew's keg-only LLVM. Override with `make tidy CLANG_TIDY=...`.
CLANG_TIDY ?= $(firstword $(shell command -v clang-tidy 2>/dev/null) \
	$(wildcard /opt/homebrew/opt/llvm/bin/clang-tidy /usr/local/opt/llvm/bin/clang-tidy) clang-tidy)

# Test builds add debug info and frame pointers for readable stack traces.
TEST_FLAGS = -g -O1 -fno-omit-frame-pointer

# Generate .d files so objects are rebuilt when included headers change
DEPFLAGS = -MMD -MP

TEST_DIR = tests
BIN_DIR = bin

TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c, $(BIN_DIR)/%.o, $(TEST_SRCS))
TEST_OBJS_CPP = $(patsubst $(TEST_DIR)/%.c, $(BIN_DIR)/%.cpp.o, $(TEST_SRCS))

# Executable names
TEST_EXECUTABLE = $(BIN_DIR)/test
TEST_EXECUTABLE_CPP = $(BIN_DIR)/test-cpp
CLI_EXECUTABLE = $(BIN_DIR)/cli-test

# Header dependency files
DEPS = $(TEST_OBJS:.o=.d) $(TEST_OBJS_CPP:.o=.d)

all: $(TEST_EXECUTABLE)

# C compilation of test files
$(BIN_DIR)/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_FLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

# C++ compilation of test files
$(BIN_DIR)/%.cpp.o: $(TEST_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(TEST_FLAGS) -x c++ $(DEPFLAGS) $(INCLUDES) -c $< -o $@

# Link C test executable
$(TEST_EXECUTABLE): $(TEST_OBJS)
	$(CC) $(CFLAGS) $(TEST_FLAGS) $(INCLUDES) $^ -o $@

# Link C++ test executable
$(TEST_EXECUTABLE_CPP): $(TEST_OBJS_CPP)
	$(CXX) $(CXXFLAGS) $(TEST_FLAGS) $(INCLUDES) $^ -o $@

test: $(TEST_EXECUTABLE)
	$(TEST_EXECUTABLE)

test-cpp: $(TEST_EXECUTABLE_CPP)
	$(TEST_EXECUTABLE_CPP)

# Build the Docker image and run the tests inside it.
test-docker:
	docker build -t slate-test .
	docker run --rm slate-test

# Interactive CLI demo.
$(CLI_EXECUTABLE): cli/cli-test.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

cli: $(CLI_EXECUTABLE)

# Static analysis with the checks in .clang-tidy. slate.h is analyzed through the files that
# define SLATE_IMPLEMENTATION (tests/test.c and the CLI).
tidy:
	$(CLANG_TIDY) --quiet $(TEST_SRCS) cli/cli-test.c -- $(CFLAGS)

clean:
	rm -f $(TEST_OBJS) $(TEST_OBJS_CPP) $(DEPS) \
		$(TEST_EXECUTABLE) $(TEST_EXECUTABLE_CPP) $(CLI_EXECUTABLE)

.PHONY: all clean test test-cpp test-docker cli tidy

-include $(DEPS)
