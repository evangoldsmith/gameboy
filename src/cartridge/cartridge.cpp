#include "cartridge.h"

#include <fstream>
#include <stdexcept>

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

    return cart;
}

// ── read / write ─────────────────────────────────────────────────────────────

uint8_t Cartridge::read(uint16_t addr) const {
    // $0000–$7FFF: ROM (ROM-only: direct index; MBC banking handled later)
    if (addr < 0x8000) {
        if (addr < m_rom.size())
            return m_rom[addr];
        return 0xFF;
    }
    // $A000–$BFFF: external RAM (not wired yet)
    return 0xFF;
}

void Cartridge::write([[maybe_unused]] uint16_t addr,
                      [[maybe_unused]] uint8_t  val) {
    // ROM-only cartridges ignore all writes.
    // MBC register writes will be dispatched here in Phase 9.
}
