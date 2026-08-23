#ifndef TESTS_SYNTHETIC_ROM_H
#define TESTS_SYNTHETIC_ROM_H

// Builds a throwaway cartridge image on disk so tests can exercise banking and
// bus behaviour without shipping a real ROM. Every bank is filled with its own
// bank number, so a single read says which bank is currently mapped.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class SyntheticRom {
public:
    // romSizeCode follows the header encoding: the image is 32 KB << code.
    SyntheticRom(uint8_t cartType, uint8_t romSizeCode, uint8_t ramSizeCode) {
        const std::size_t banks = 2u << romSizeCode;
        std::vector<uint8_t> rom(banks * 0x4000, 0);
        for (std::size_t b = 0; b < banks; ++b)
            std::fill_n(rom.begin() + static_cast<std::ptrdiff_t>(b * 0x4000),
                        std::size_t{0x4000}, static_cast<uint8_t>(b));

        rom[0x0147] = cartType;
        rom[0x0148] = romSizeCode;
        rom[0x0149] = ramSizeCode;

        m_path = (std::filesystem::temp_directory_path() /
                  ("gbtest_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".gb"))
                     .string();
        std::ofstream f(m_path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(rom.data()),
                static_cast<std::streamsize>(rom.size()));
    }

    ~SyntheticRom() {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }

    SyntheticRom(const SyntheticRom&) = delete;
    SyntheticRom& operator=(const SyntheticRom&) = delete;

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

#endif  // TESTS_SYNTHETIC_ROM_H
