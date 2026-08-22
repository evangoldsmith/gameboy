// Headless test-ROM runner.
//
//   gbrun <rom> <frames> [out.bmp]
//
// Runs the ROM for a fixed number of frames, prints whatever it sent over the
// serial port to stdout, and optionally writes the final framebuffer as a
// 24-bit BMP. Deciding pass or fail is left to tests/run_tests.py — this only
// produces the evidence.
//
// Links gameboy_core only, so it needs no SDL and runs anywhere.

#include "gameboy.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

void writeBmp(GameBoy& gb, const char* path) {
    const auto& fb = gb.ppu().framebuffer();
    const int W = PPU::WIDTH, H = PPU::HEIGHT;
    const int imageBytes = W * 3 * H;

    std::vector<unsigned char> header(54, 0);
    header[0] = 'B';
    header[1] = 'M';
    const auto put32 = [&](int off, int v) {
        for (int i = 0; i < 4; ++i)
            header[static_cast<std::size_t>(off + i)] =
                static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
    };
    const auto put16 = [&](int off, int v) {
        for (int i = 0; i < 2; ++i)
            header[static_cast<std::size_t>(off + i)] =
                static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
    };
    put32(2, 54 + imageBytes);
    put32(10, 54);
    put32(14, 40);
    put32(18, W);
    put32(22, H);
    put16(26, 1);
    put16(28, 24);
    put32(34, imageBytes);

    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "gbrun: cannot write %s\n", path);
        return;
    }
    std::fwrite(header.data(), 1, header.size(), f);
    // BMP rows run bottom-up, and each pixel is stored BGR.
    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) {
            const uint32_t p = fb[static_cast<std::size_t>(y * W + x)];
            const unsigned char bgr[3] = {static_cast<unsigned char>(p & 0xFF),
                                          static_cast<unsigned char>((p >> 8) & 0xFF),
                                          static_cast<unsigned char>((p >> 16) & 0xFF)};
            std::fwrite(bgr, 1, 3, f);
        }
    }
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: gbrun <rom> <frames> [out.bmp]\n");
        return EXIT_FAILURE;
    }

    const int frames = std::atoi(argv[2]);

    try {
        GameBoy gb(argv[1]);
        gb.serial().setEcho(false);  // collected and printed once at the end

        for (int i = 0; i < frames && !gb.cpu().stopped(); ++i) gb.runFrame();

        if (gb.cpu().stopped())
            std::fprintf(stderr, "gbrun: CPU stopped on an illegal opcode\n");

        std::fputs(gb.serial().output().c_str(), stdout);

        if (argc > 3) writeBmp(gb, argv[3]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "gbrun: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
