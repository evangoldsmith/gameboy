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

    uint8_t  romByte(std::size_t offset) const;
    uint32_t romBankCount() const;

    // Bank feeding $0000–$3FFF. Always 0 except in MBC1's alternate mode.
    std::size_t lowBank() const;
    // Bank feeding $4000–$7FFF.
    std::size_t highBank() const;

    uint8_t readRam(uint16_t addr) const;
    void    writeRam(uint16_t addr, uint8_t val);

    void writeMBC1(uint16_t addr, uint8_t val);
    void writeMBC2(uint16_t addr, uint8_t val);
    void writeMBC3(uint16_t addr, uint8_t val);
    void writeMBC5(uint16_t addr, uint8_t val);

    std::vector<uint8_t> m_rom;
    std::vector<uint8_t> m_ram;
    CartridgeHeader      m_header{};

    // Primary bank register. 5 bits on MBC1, 7 on MBC3, 9 on MBC5.
    uint16_t m_romBank{1};
    // MBC1's 2-bit secondary register; also the RAM bank on MBC3/MBC5.
    uint8_t  m_ramBank{0};
    bool     m_ramEnabled{false};

    // MBC1 only: swaps the secondary register between upper ROM bits (mode 0)
    // and RAM banking (mode 1).
    bool m_mode1{false};

    // MBC3 only. $08–$0C select an RTC register instead of a RAM bank.
    uint8_t                m_rtcSelect{0};
    bool                   m_rtcLatchArmed{false};
    std::array<uint8_t, 5> m_rtc{};
    std::array<uint8_t, 5> m_rtcLatched{};
};

#endif // CARTRIDGE_H
