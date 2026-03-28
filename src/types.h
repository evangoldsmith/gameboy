#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <string_view>

enum class Register8 : uint8_t { A, F, B, C, D, E, H, L };

enum class Register16 : uint8_t { AF, BC, DE, HL, SP, PC };

enum class Interrupt : uint8_t {
    VBlank  = 0,
    LCDStat = 1,
    Timer   = 2,
    Serial  = 3,
    Joypad  = 4
};

enum class PPUMode : uint8_t {
    HBlank        = 0,
    VBlank        = 1,
    OAMScan       = 2,
    PixelTransfer = 3
};

enum class MBCType : uint8_t { None, MBC1, MBC2, MBC3, MBC5 };

constexpr std::string_view mbcTypeName(MBCType t) {
    switch (t) {
        case MBCType::None: return "ROM Only";
        case MBCType::MBC1: return "MBC1";
        case MBCType::MBC2: return "MBC2";
        case MBCType::MBC3: return "MBC3";
        case MBCType::MBC5: return "MBC5";
    }
    return "Unknown";
}

#endif // TYPES_H