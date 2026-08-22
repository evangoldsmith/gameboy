cmake_minimum_required(VERSION 3.20)
project(gameboy LANGUAGES CXX)

# ── C++20, no extensions ───────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Default to Debug if nothing was specified ──────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ── Compiler warnings ──────────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-O0>
)

# ── SDL2 ───────────────────────────────────────────────────────────────────────
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    # Normalise to the imported-target interface used below
    target_include_directories(SDL2::SDL2 INTERFACE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(SDL2::SDL2 INTERFACE ${SDL2_LIBRARIES})
    target_compile_options(SDL2::SDL2 INTERFACE ${SDL2_CFLAGS_OTHER})
endif()

# ── Core emulator library (zero SDL dependency) ────────────────────────────────
add_library(gameboy_core STATIC
    src/gameboy.cpp
    src/cartridge/cartridge.cpp
    src/cpu/cpu.cpp
    src/cpu/opcodes.cpp
    src/memory/mmu.cpp
    src/ppu/ppu.cpp
    src/apu/apu.cpp
    src/apu/channels.cpp
    src/timer.cpp
    src/joypad.cpp
    src/serial.cpp
    src/debug/logger.cpp
)

target_include_directories(gameboy_core PUBLIC ${PROJECT_SOURCE_DIR}/src)

# ── Headless test-ROM runner (no SDL) ──────────────────────────────────────────
add_executable(gbrun tests/gbrun.cpp)
target_link_libraries(gbrun PRIVATE gameboy_core)

# ── Frontend executable (SDL2 lives only here) ─────────────────────────────────
add_executable(gameboy src/main.cpp)
target_link_libraries(gameboy PRIVATE gameboy_core SDL2::SDL2)
target_include_directories(gameboy PRIVATE ${SDL2_INCLUDE_DIRS})

# On macOS SDL2 also needs the Cocoa / IOKit frameworks
if(APPLE)
    target_link_libraries(gameboy PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
endif()

# ── Output binaries go into build/<config>/ ────────────────────────────────────
set_target_properties(gameboy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
