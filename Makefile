#
#  Makefile
#  sqlite-agent
#
#  Created by Gioele Cantoni on 05/11/25.
#

# Makefile for SQLite Agent Extension
# Supports compilation for Linux, macOS, Windows, Android and iOS

# Customize sqlite3 executable with
# make test SQLITE3=/opt/homebrew/Cellar/sqlite/3.49.1/bin/sqlite3
SQLITE3 ?= sqlite3

# Set default platform if not specified
ifeq ($(OS),Windows_NT)
	PLATFORM := windows
	HOST := windows
	CPUS := $(shell powershell -Command "[Environment]::ProcessorCount")
else
	HOST = $(shell uname -s | tr '[:upper:]' '[:lower:]')
	ifeq ($(HOST),darwin)
		PLATFORM := macos
		CPUS := $(shell sysctl -n hw.ncpu)
	else
		PLATFORM := $(HOST)
		CPUS := $(shell nproc)
	endif
endif

# Speed up builds by using all available CPU cores
MAKEFLAGS += -j$(CPUS)

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -I$(SRC_DIR) -I$(LIBS_DIR)

# Directories
SRC_DIR = src
DIST_DIR = dist
BUILD_DIR = build
LIBS_DIR = libs
VPATH = $(SRC_DIR)

# Files
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(patsubst %.c, $(BUILD_DIR)/%.o, $(notdir $(SRC_FILES)))

# Platform-specific settings
ifeq ($(PLATFORM),windows)
	TARGET := $(DIST_DIR)/agent.dll
	LDFLAGS += -shared
	DEF_FILE := $(BUILD_DIR)/agent.def
	STRIP = strip --strip-unneeded $@
else ifeq ($(PLATFORM),macos)
	TARGET := $(DIST_DIR)/agent.dylib
	MACOS_MIN_VERSION = 11.0
	ifndef ARCH
		LDFLAGS += -arch x86_64 -arch arm64
		CFLAGS += -arch x86_64 -arch arm64
	else
		LDFLAGS += -arch $(ARCH)
		CFLAGS += -arch $(ARCH)
	endif
	LDFLAGS += -dynamiclib -undefined dynamic_lookup -headerpad_max_install_names -mmacosx-version-min=$(MACOS_MIN_VERSION)
	CFLAGS += -mmacosx-version-min=$(MACOS_MIN_VERSION)
	STRIP = strip -x -S $@
else ifeq ($(PLATFORM),android)
	ifndef ARCH
		$(error "Android ARCH must be set to ARCH=x86_64 or ARCH=arm64-v8a")
	endif
	ifndef ANDROID_NDK
		$(error "Android NDK must be set")
	endif
	BIN = $(ANDROID_NDK)/toolchains/llvm/prebuilt/$(HOST)-x86_64/bin
	ifneq (,$(filter $(ARCH),arm64 arm64-v8a))
		override ARCH := aarch64
	endif
	CC = $(BIN)/$(ARCH)-linux-android26-clang
	TARGET := $(DIST_DIR)/agent.so
	LDFLAGS += -shared
	STRIP = $(BIN)/llvm-strip --strip-unneeded $@
else ifeq ($(PLATFORM),ios)
	TARGET := $(DIST_DIR)/agent.dylib
	SDK := -isysroot $(shell xcrun --sdk iphoneos --show-sdk-path) -miphoneos-version-min=11.0
	LDFLAGS += -dynamiclib $(SDK) -headerpad_max_install_names
	CFLAGS += -arch arm64 $(SDK)
	STRIP = strip -x -S $@
else ifeq ($(PLATFORM),ios-sim)
	TARGET := $(DIST_DIR)/agent.dylib
	SDK := -isysroot $(shell xcrun --sdk iphonesimulator --show-sdk-path) -miphonesimulator-version-min=11.0
	LDFLAGS += -arch x86_64 -arch arm64 -dynamiclib $(SDK) -headerpad_max_install_names
	CFLAGS += -arch x86_64 -arch arm64 $(SDK)
	STRIP = strip -x -S $@
else # linux
	TARGET := $(DIST_DIR)/agent.so
	LDFLAGS += -shared
	CFLAGS += -fPIC
	STRIP = strip --strip-unneeded $@
endif

# Windows .def file generation
$(DEF_FILE):
ifeq ($(PLATFORM),windows)
	@echo "LIBRARY agent.dll" > $@
	@echo "EXPORTS" >> $@
	@echo "    sqlite3_agent_init" >> $@
endif

$(shell mkdir -p $(BUILD_DIR) $(DIST_DIR))

all: extension

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -O3 -fPIC -c $< -o $@

$(TARGET): $(OBJ_FILES) $(DEF_FILE)
	$(CC) $(CFLAGS) $(SRC_DIR)/sqlite-agent.c $(LDFLAGS) -o $@
	$(STRIP)

extension: $(TARGET)

test: extension
	$(SQLITE3) ":memory:" -cmd ".bail on" ".load ./dist/agent" "SELECT agent_version();"

# Build and run Playwright MCP test
playwright: extension
	$(CC) -Wall -Wextra -Wno-unused-parameter -O3 \
		-I$(LIBS_DIR) -c $(LIBS_DIR)/sqlite3.c -o $(BUILD_DIR)/sqlite3.o 2>/dev/null || true
	$(CC) -Wall -Wextra -Wno-unused-parameter -O3 \
		-I$(LIBS_DIR) test/playwright.c $(BUILD_DIR)/sqlite3.o -o $(BUILD_DIR)/test-playwright
	@echo "Note: Make sure Playwright MCP server is running on localhost:8931"
	@echo "      Start with: npx @playwright/mcp@latest --port 8931 --headless"
	@echo ""
	$(BUILD_DIR)/test-playwright

# Build and run Airbnb MCP test
airbnb: extension
	$(CC) -Wall -Wextra -Wno-unused-parameter -O3 \
		-I$(LIBS_DIR) -c $(LIBS_DIR)/sqlite3.c -o $(BUILD_DIR)/sqlite3.o 2>/dev/null || true
	$(CC) -Wall -Wextra -Wno-unused-parameter -O3 \
		-I$(LIBS_DIR) test/airbnb.c $(BUILD_DIR)/sqlite3.o -o $(BUILD_DIR)/test-airbnb
	@echo "Note: Make sure Airbnb MCP server is running on localhost:8000/mcp"
	@echo ""
	$(BUILD_DIR)/test-airbnb

# Build and run GitHub MCP test
github: extension
	$(CC) -Wall -Wextra -Wno-unused-parameter -O3 \
		-I$(LIBS_DIR) -c $(LIBS_DIR)/sqlite3.c -o $(BUILD_DIR)/sqlite3.o 2>/dev/null || true
	$(CC) -Wall -Wextra -Wno-unused-parameter -O3 \
		-I$(LIBS_DIR) test/github.c $(BUILD_DIR)/sqlite3.o -o $(BUILD_DIR)/test-github
	@echo "Note: Set GITHUB_TOKEN environment variable with your GitHub Personal Access Token and make sure GitHub MCP server is running"
	@echo "      export GITHUB_TOKEN=\"ghp_your_token_here\""
	@echo ""
	$(BUILD_DIR)/test-github

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)

# Extract version from header
version:
	@echo $(shell sed -n 's/^#define SQLITE_AGENT_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' $(SRC_DIR)/sqlite-agent.h)

# Help target
help:
	@echo "SQLite Agent Extension Makefile"
	@echo ""
	@echo "Usage: make [PLATFORM=platform] [ARCH=arch] [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build the extension (default)"
	@echo "  extension  - Build the SQLite extension"
	@echo "  test       - Run quick CLI test"
	@echo "  playwright - Build and run Playwright test"
	@echo "  airbnb     - Build and run Airbnb test"
	@echo "  github     - Build and run GitHub test"
	@echo "  clean      - Remove all build artifacts"
	@echo "  version    - Display extension version"
	@echo "  help       - Display this help message"
	@echo ""
	@echo "Platforms:"
	@echo "  macos      - macOS (default on Darwin)"
	@echo "  linux      - Linux (default on Linux)"
	@echo "  windows    - Windows (default on Windows)"
	@echo "  android    - Android (requires ARCH and ANDROID_NDK)"
	@echo "  ios        - iOS device"
	@echo "  ios-sim    - iOS simulator"
	@echo ""
	@echo "Examples:"
	@echo "  make                           # Build for current platform"
	@echo "  make test                      # Build and test"
	@echo "  make PLATFORM=android ARCH=arm64-v8a  # Build for Android ARM64"

.PHONY: all extension test playwright airbnb github clean version help
