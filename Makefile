# Convenience wrapper around CMake.
# Usage:
#   make          – debug build
#   make release  – optimised release build
#   make run      – debug build + launch (optionally: make run ROM=path/to/rom.gb)
#   make clean    – remove build artefacts
#   make distclean – remove all build directories

BUILD_ROOT  := build
DEBUG_DIR   := $(BUILD_ROOT)/debug
RELEASE_DIR := $(BUILD_ROOT)/release

CMAKE       := cmake
CMAKE_FLAGS := -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

BINARY      := gameboy
ROM         ?= roms/pokemon_red.gb

# ── Default target ─────────────────────────────────────────────────────────────
.PHONY: all
all: debug

# ── Debug ──────────────────────────────────────────────────────────────────────
.PHONY: debug
debug: $(DEBUG_DIR)/Makefile
	$(CMAKE) --build $(DEBUG_DIR) --parallel

$(DEBUG_DIR)/Makefile:
	$(CMAKE) -S . -B $(DEBUG_DIR) -DCMAKE_BUILD_TYPE=Debug $(CMAKE_FLAGS)

# ── Release ────────────────────────────────────────────────────────────────────
.PHONY: release
release: $(RELEASE_DIR)/Makefile
	$(CMAKE) --build $(RELEASE_DIR) --parallel

$(RELEASE_DIR)/Makefile:
	$(CMAKE) -S . -B $(RELEASE_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_FLAGS)

# ── Run ────────────────────────────────────────────────────────────────────────
.PHONY: run
run: debug
	./$(DEBUG_DIR)/$(BINARY) $(ROM)

.PHONY: run-release
run-release: release
	./$(RELEASE_DIR)/$(BINARY) $(ROM)

# ── Reconfigure (force CMake to re-run) ───────────────────────────────────────
.PHONY: reconfigure
reconfigure:
	$(CMAKE) -S . -B $(DEBUG_DIR) -DCMAKE_BUILD_TYPE=Debug $(CMAKE_FLAGS)
	$(CMAKE) -S . -B $(RELEASE_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_FLAGS)

# ── Clean ──────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	$(CMAKE) --build $(DEBUG_DIR) --target clean 2>/dev/null || true
	$(CMAKE) --build $(RELEASE_DIR) --target clean 2>/dev/null || true

.PHONY: distclean
distclean:
	rm -rf $(BUILD_ROOT)

# ── Help ───────────────────────────────────────────────────────────────────────
.PHONY: help
help:
	@echo "Targets:"
	@echo "  all / debug     – debug build (default)"
	@echo "  release         – optimised release build"
	@echo "  run [ROM=<path>]     – build (debug) and launch"
	@echo "  run-release [ROM=<path>] – build (release) and launch"
	@echo "  reconfigure     – re-run CMake for both configs"
	@echo "  clean           – remove compiled objects"
	@echo "  distclean       – remove all build directories"
