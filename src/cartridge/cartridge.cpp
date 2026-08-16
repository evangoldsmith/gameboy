#include "cartridge.h"

#include <fstream>
#include <stdexcept>

namespace {
// MBC2 has 512 half-bytes of RAM built into the chip; the header reports none.
constexpr uint32_t MBC2_RAM_BYTES = 512;
}  // namespace

// ── Header decode helpers ────────────────────────────────────────────────────

MBCType mbcTypeFromByte(uint8_t b) {
    switch (b) {
        case 0x00:                   return MBCType::None;
        case 0x01: case 0x02: case 0x03: return MBCType::MBC1;
        case 0x05: case 0x06:        return MBCType::MBC2;
        case 0x0F: case 0x10:
        case 0x11: case 0x12: case 0x13: return MBCType::MBC3;
        case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E: return MBCType::MBC5;
        default:                     return MBCType::None;
    }
}

uint32_t romSizeFromByte(uint8_t b) {
    // 32 KB × (1 << b)
    return 32u * 1024u * (1u << b);
}

uint32_t ramSizeFromByte(uint8_t b) {
    switch (b) {
        case 0x00: return 0;
        case 0x01: return 2u   * 1024u;
        case 0x02: return 8u   * 1024u;
        case 0x03: return 32u  * 1024u;
        case 0x04: return 128u * 1024u;
        case 0x05: return 64u  * 1024u;
        default:   return 0;
    }
}

// ── Cartridge::load ──────────────────────────────────────────────────────────

Cartridge Cartridge::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        throw std::runtime_error("Cannot open ROM: " + path);

    auto size = static_cast<std::streamsize>(f.tellg());
    if (size < 0x150)
        throw std::runtime_error("ROM too small to contain a valid header");

    f.seekg(0);
    Cartridge cart;
    cart.m_rom.resize(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(cart.m_rom.data()), size);

    // ── Parse header ────────────────────────────────────────────────────────
    // Title: $0134–$0143 (up to 16 bytes, null-terminated)
    constexpr std::size_t TITLE_OFFSET = 0x0134;
    constexpr std::size_t TITLE_LEN    = 16;
    std::string title;
    for (std::size_t i = 0; i < TITLE_LEN; ++i) {
        uint8_t c = cart.m_rom[TITLE_OFFSET + i];
        if (c == 0) break;
        title += static_cast<char>(c);
    }

    cart.m_header = CartridgeHeader{
        .title    = title,
        .mbcType  = mbcTypeFromByte(cart.m_rom[0x0147]),
        .romBytes = romSizeFromByte(cart.m_rom[0x0148]),
        .ramBytes = ramSizeFromByte(cart.m_rom[0x0149]),
    };

    cart.m_ram.assign(cart.m_header.mbcType == MBCType::MBC2 ? MBC2_RAM_BYTES
                                                             : cart.m_header.ramBytes,
                      0);

    return cart;
}

// ── Bank resolution ──────────────────────────────────────────────────────────

uint32_t Cartridge::romBankCount() const {
    return m_rom.empty() ? 1u
                         : static_cast<uint32_t>((m_rom.size() + 0x3FFF) / 0x4000);
}

uint8_t Cartridge::romByte(std::size_t offset) const {
    return offset < m_rom.size() ? m_rom[offset] : uint8_t{0xFF};
}

std::size_t Cartridge::lowBank() const {
    // Only MBC1's alternate mode can put something other than bank 0 here: the
    // secondary register shifts into the upper bits, exposing banks $20/$40/$60
    // that are otherwise unreachable.
    if (m_header.mbcType == MBCType::MBC1 && m_mode1)
        return (static_cast<std::size_t>(m_ramBank) << 5) % romBankCount();
    return 0;
}

std::size_t Cartridge::highBank() const {
    return static_cast<std::size_t>(m_romBank) % romBankCount();
}

uint8_t Cartridge::read(uint16_t addr) const {
    if (addr < 0x4000)
        return romByte(lowBank() * 0x4000 + addr);

    if (addr < 0x8000)
        return romByte(highBank() * 0x4000 + (addr - 0x4000));

    if (addr >= 0xA000 && addr < 0xC000)
        return readRam(addr);

    return 0xFF;
}

// ── Cartridge RAM (and the MBC3 clock registers) ─────────────────────────────

uint8_t Cartridge::readRam(uint16_t addr) const {
    if (!m_ramEnabled) return 0xFF;

    // MBC3 maps a clock register over the RAM window when one is selected.
    if (m_header.mbcType == MBCType::MBC3 && m_rtcSelect >= 0x08 && m_rtcSelect <= 0x0C)
        return m_rtcLatched[m_rtcSelect - 0x08];

    if (m_ram.empty()) return 0xFF;

    if (m_header.mbcType == MBCType::MBC2) {
        // 512 half-bytes, mirrored across the whole window. The upper nibble
        // is not wired up and reads back as ones.
        return static_cast<uint8_t>(m_ram[(addr - 0xA000) % MBC2_RAM_BYTES] | 0xF0);
    }

    const std::size_t off =
        (static_cast<std::size_t>(m_ramBank) * 0x2000 + (addr - 0xA000)) % m_ram.size();
    return m_ram[off];
}

void Cartridge::writeRam(uint16_t addr, uint8_t val) {
    if (!m_ramEnabled) return;

    if (m_header.mbcType == MBCType::MBC3 && m_rtcSelect >= 0x08 && m_rtcSelect <= 0x0C) {
        m_rtc[m_rtcSelect - 0x08] = val;
        return;
    }

    if (m_ram.empty()) return;

    if (m_header.mbcType == MBCType::MBC2) {
        m_ram[(addr - 0xA000) % MBC2_RAM_BYTES] = static_cast<uint8_t>(val & 0x0F);
        return;
    }

    const std::size_t off =
        (static_cast<std::size_t>(m_ramBank) * 0x2000 + (addr - 0xA000)) % m_ram.size();
    m_ram[off] = val;
}

// ── Register writes ──────────────────────────────────────────────────────────

void Cartridge::write(uint16_t addr, uint8_t val) {
    if (addr >= 0xA000 && addr < 0xC000) {
        writeRam(addr, val);
        return;
    }
    if (addr >= 0x8000) return;

    switch (m_header.mbcType) {
        case MBCType::None: break;
        case MBCType::MBC1: writeMBC1(addr, val); break;
        case MBCType::MBC2: writeMBC2(addr, val); break;
        case MBCType::MBC3: writeMBC3(addr, val); break;
        case MBCType::MBC5: writeMBC5(addr, val); break;
    }
}

void Cartridge::writeMBC1(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        m_ramEnabled = (val & 0x0F) == 0x0A;
    } else if (addr < 0x4000) {
        // 5-bit register. Writing 0 selects 1, which is why banks $20/$40/$60
        // are unreachable in the default mode: their low five bits are zero.
        uint8_t low = static_cast<uint8_t>(val & 0x1F);
        if (low == 0) low = 1;
        m_romBank = static_cast<uint16_t>((m_romBank & 0x60) | low);
    } else if (addr < 0x6000) {
        // Two bits, used as the upper ROM bits or as the RAM bank depending on
        // the mode below.
        m_ramBank = static_cast<uint8_t>(val & 0x03);
        m_romBank = static_cast<uint16_t>((m_romBank & 0x1F) |
                                          (static_cast<uint16_t>(m_ramBank) << 5));
    } else {
        m_mode1 = (val & 0x01) != 0;
    }
}

void Cartridge::writeMBC2(uint16_t addr, uint8_t val) {
    if (addr >= 0x4000) return;

    // Bit 8 of the address, not the range, picks which register is written.
    if ((addr & 0x0100) == 0) {
        m_ramEnabled = (val & 0x0F) == 0x0A;
    } else {
        uint8_t bank = static_cast<uint8_t>(val & 0x0F);
        if (bank == 0) bank = 1;
        m_romBank = bank;
    }
}

void Cartridge::writeMBC3(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        m_ramEnabled = (val & 0x0F) == 0x0A;
    } else if (addr < 0x4000) {
        // A full 7-bit bank number in one register — no split, and no upper
        // bits borrowed from the RAM bank register.
        uint8_t bank = static_cast<uint8_t>(val & 0x7F);
        if (bank == 0) bank = 1;
        m_romBank = bank;
    } else if (addr < 0x6000) {
        // $00–$03 pick a RAM bank; $08–$0C map a clock register over the RAM
        // window instead. Critically this does *not* touch the ROM bank.
        if (val <= 0x03) {
            m_ramBank   = val;
            m_rtcSelect = 0;
        } else if (val >= 0x08 && val <= 0x0C) {
            m_rtcSelect = val;
        }
    } else {
        // Writing $00 then $01 copies the running clock into the latch that
        // reads observe, so a multi-byte read cannot straddle a tick.
        if (m_rtcLatchArmed && val == 0x01) m_rtcLatched = m_rtc;
        m_rtcLatchArmed = (val == 0x00);
    }
}

void Cartridge::writeMBC5(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        m_ramEnabled = (val & 0x0F) == 0x0A;
    } else if (addr < 0x3000) {
        // Low 8 bits. Unlike MBC1/MBC3, writing 0 really does select bank 0.
        m_romBank = static_cast<uint16_t>((m_romBank & 0x100) | val);
    } else if (addr < 0x4000) {
        m_romBank = static_cast<uint16_t>((m_romBank & 0x0FF) |
                                          (static_cast<uint16_t>(val & 0x01) << 8));
    } else if (addr < 0x6000) {
        m_ramBank = static_cast<uint8_t>(val & 0x0F);
    }
}
