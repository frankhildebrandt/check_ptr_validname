UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
HOST_OS := macos
else ifeq ($(UNAME_S),Linux)
HOST_OS := linux
else ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
HOST_OS := windows
else ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
HOST_OS := windows
else
HOST_OS := unknown
endif

ifeq ($(UNAME_M),x86_64)
HOST_ARCH := x86_64
else ifeq ($(UNAME_M),amd64)
HOST_ARCH := x86_64
else ifeq ($(UNAME_M),arm64)
HOST_ARCH := arm64
else ifeq ($(UNAME_M),aarch64)
HOST_ARCH := arm64
else
HOST_ARCH := unknown
endif

OS ?= $(HOST_OS)
ARCH ?= $(HOST_ARCH)

TARGET_BASE := check_ptr_validname
SRC := check_ptr_validname.c
OUT_DIR := build/$(OS)-$(ARCH)

ifeq ($(OS),windows)
EXE_EXT := .exe
else
EXE_EXT :=
endif

TARGET := $(OUT_DIR)/$(TARGET_BASE)$(EXE_EXT)

# CROSS can override toolchain prefix, e.g.:
# make OS=linux ARCH=arm64 CROSS=aarch64-linux-gnu-
CROSS ?=

ifeq ($(OS),$(HOST_OS))
  ifeq ($(ARCH),$(HOST_ARCH))
    NATIVE_TARGET := 1
  endif
endif

ifeq ($(NATIVE_TARGET),1)
  DEFAULT_CROSS :=
else
  ifeq ($(OS),linux)
    ifeq ($(ARCH),x86_64)
      DEFAULT_CROSS :=
    else ifeq ($(ARCH),arm64)
      DEFAULT_CROSS := aarch64-linux-gnu-
    endif
  else ifeq ($(OS),macos)
    ifeq ($(ARCH),x86_64)
      DEFAULT_CROSS := x86_64-apple-darwin-
    else ifeq ($(ARCH),arm64)
      DEFAULT_CROSS := aarch64-apple-darwin-
    endif
  else ifeq ($(OS),windows)
    ifeq ($(ARCH),x86_64)
      DEFAULT_CROSS := x86_64-w64-mingw32-
    else ifeq ($(ARCH),arm64)
      DEFAULT_CROSS := aarch64-w64-mingw32-
    endif
  endif
endif

ifeq ($(strip $(CROSS)),)
TOOLCHAIN := $(DEFAULT_CROSS)
else
TOOLCHAIN := $(CROSS)
endif

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

.PHONY: all clean print-config validate

ifeq ($(origin CC), default)
  ifeq ($(strip $(TOOLCHAIN)),)
    CC := cc
  else
    CC := $(TOOLCHAIN)cc
  endif
endif

all: validate $(TARGET)

validate:
	@if [ "$(OS)" != "linux" ] && [ "$(OS)" != "macos" ] && [ "$(OS)" != "windows" ]; then \
		echo "Unsupported OS: $(OS) (allowed: linux, macos, windows)"; \
		exit 1; \
	fi
	@if [ "$(ARCH)" != "x86_64" ] && [ "$(ARCH)" != "arm64" ]; then \
		echo "Unsupported ARCH: $(ARCH) (allowed: x86_64, arm64)"; \
		exit 1; \
	fi

$(OUT_DIR):
	mkdir -p $@

$(TARGET): $(SRC) | $(OUT_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

print-config:
	@echo "Host OS/ARCH : $(HOST_OS)/$(HOST_ARCH)"
	@echo "Target OS/ARCH: $(OS)/$(ARCH)"
	@echo "CC           : $(CC)"
	@echo "Output       : $(TARGET)"

clean:
	rm -rf build
