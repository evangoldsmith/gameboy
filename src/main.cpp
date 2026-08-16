#include "debug/logger.h"
#include "gameboy.h"

#include <SDL2/SDL.h>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr int GB_WIDTH  = PPU::WIDTH;
constexpr int GB_HEIGHT = PPU::HEIGHT;
constexpr int SCALE     = 4;

// Keyboard layout: arrows for the D-pad, Z/X for A/B, Enter/Backspace for
// Start/Select. Escape quits.
std::optional<Button> buttonForKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_RIGHT:     return Button::Right;
        case SDLK_LEFT:      return Button::Left;
        case SDLK_UP:        return Button::Up;
        case SDLK_DOWN:      return Button::Down;
        case SDLK_z:         return Button::A;
        case SDLK_x:         return Button::B;
        case SDLK_RETURN:    return Button::Start;
        case SDLK_BACKSPACE: return Button::Select;
        default:             return std::nullopt;
    }
}

void printHeader(const CartridgeHeader& hdr) {
    std::cout << "Title:    " << hdr.title << "\n"
              << "MBC:      " << mbcTypeName(hdr.mbcType) << "\n"
              << "ROM size: " << hdr.romBytes / 1024 << " KB\n"
              << "RAM size: " << hdr.ramBytes / 1024 << " KB\n";
}

// Headless run that prints a Game Boy Doctor trace line before every
// instruction. Redirect stdout to a file and diff it against the golden log.
int runDoctor(GameBoy& gb, long maxSteps) {
    gb.ppu().setLyStub(true);      // the reference logs were captured this way
    gb.serial().setEcho(false);    // keep serial output out of the trace

    for (long i = 0; i < maxSteps && !gb.cpu().stopped(); ++i) {
        std::cout << logger::traceLine(gb.cpu(), gb.mmu()) << "\n";
        gb.step();
    }

    std::cout.flush();
    std::cerr << "\n--- serial output ---\n" << gb.serial().output() << "\n";
    return EXIT_SUCCESS;
}

int runSDL(GameBoy& gb, const CartridgeHeader& hdr) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
        return EXIT_FAILURE;
    }

    const std::string windowTitle = "Game Boy — " + hdr.title;
    SDL_Window* window = SDL_CreateWindow(
        windowTitle.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        GB_WIDTH * SCALE, GB_HEIGHT * SCALE,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << "\n";
        SDL_Quit();
        return EXIT_FAILURE;
    }

    // PRESENTVSYNC paces the loop at the display's refresh rate. Close enough to
    // the DMG's 59.7 Hz for now; real frame pacing is Phase 14.
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    // Texture the PPU will write into: 160x144, 32-bit ARGB
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        GB_WIDTH, GB_HEIGHT
    );
    if (!texture) {
        std::cerr << "SDL_CreateTexture error: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    // Scale the 160x144 texture up to fill the window with no filtering
    SDL_RenderSetLogicalSize(renderer, GB_WIDTH, GB_HEIGHT);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    running = false;
                } else if (const auto button = buttonForKey(key)) {
                    gb.joypad().setButton(*button, event.type == SDL_KEYDOWN);
                }
            }
        }

        gb.runFrame();

        const auto& fb = gb.ppu().framebuffer();
        SDL_UpdateTexture(texture, nullptr, fb.data(),
                          GB_WIDTH * static_cast<int>(sizeof(uint32_t)));

        SDL_SetRenderDrawColor(renderer, 0x9B, 0xBC, 0x0F, 0xFF); // Classic GB green
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: gameboy <rom.gb> [--doctor [steps]]\n";
        return EXIT_FAILURE;
    }

    bool doctor   = false;
    long maxSteps = 1'000'000;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--doctor") {
            doctor = true;
            if (i + 1 < argc) {
                if (const long n = std::strtol(argv[i + 1], nullptr, 10); n > 0) {
                    maxSteps = n;
                    ++i;
                }
            }
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return EXIT_FAILURE;
        }
    }

    try {
        GameBoy gb(argv[1]);

        if (doctor) return runDoctor(gb, maxSteps);

        printHeader(gb.header());
        return runSDL(gb, gb.header());
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
