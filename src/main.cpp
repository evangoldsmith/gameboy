#include <SDL2/SDL.h>
#include <cstdlib>
#include <iostream>

static constexpr int GB_WIDTH  = 160;
static constexpr int GB_HEIGHT = 144;
static constexpr int SCALE     = 4;

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Game Boy",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        GB_WIDTH * SCALE, GB_HEIGHT * SCALE,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << "\n";
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
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
            } else if (event.type == SDL_KEYDOWN &&
                       event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        // TODO: tick emulator, get framebuffer from PPU, upload to texture
        // SDL_UpdateTexture(texture, nullptr, framebuffer.data(), GB_WIDTH * sizeof(uint32_t));

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