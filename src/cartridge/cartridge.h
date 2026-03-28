#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include "../types.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct CartridgeHeader {
    std::string title;
    MBCType     mbcType;
    uint32_t    romBytes;  // total ROM size in bytes
    uint32_t    ramBytes;  // external RAM size in bytes
};

// Decode header byte $0147 → MBCType
MBCType mbcTypeFromByte(uint8_t b);

// Decode header byte $0148 → ROM size in bytes
uint32_t romSizeFromByte(uint8_t b);

// Decode header byte $0149 → RAM size in bytes
uint32_t ramSizeFromByte(uint8_t b);

class Cartridge {
public:
    // Throws std::runtime_error on failure
    static Cartridge load(const std::string& path);

    uint8_t read(uint16_t addr) const;
    void    write(uint16_t addr, uint8_t val);

    const CartridgeHeader& header() const { return m_header; }

private:
    Cartridge() = default;

    std::vector<uint8_t> m_rom;
    CartridgeHeader      m_header{};
};

#endif // CARTRIDGE_H
