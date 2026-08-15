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

    cart.m_ram.assign(cart.m_header.ramBytes, 0);

    return cart;
}

// ── read / write ─────────────────────────────────────────────────────────────

uint32_t Cartridge::romBankCount() const {
    return m_rom.empty() ? 1u
                         : static_cast<uint32_t>((m_rom.size() + 0x3FFF) / 0x4000);
}

uint8_t Cartridge::romByte(std::size_t offset) const {
    return offset < m_rom.size() ? m_rom[offset] : uint8_t{0xFF};
}

uint8_t Cartridge::read(uint16_t addr) const {
    // $0000–$3FFF: fixed bank 0
    if (addr < 0x4000)
        return romByte(addr);

    // $4000–$7FFF: switchable bank
    if (addr < 0x8000) {
        const std::size_t bank = m_romBank % romBankCount();
        return romByte(bank * 0x4000 + (addr - 0x4000));
    }

    // $A000–$BFFF: external RAM
    if (addr >= 0xA000 && addr < 0xC000) {
        if (!m_ramEnabled || m_ram.empty()) return 0xFF;
        const std::size_t off =
            (static_cast<std::size_t>(m_ramBank) * 0x2000 + (addr - 0xA000)) % m_ram.size();
        return m_ram[off];
    }

    return 0xFF;
}

void Cartridge::write(uint16_t addr, uint8_t val) {
    if (m_header.mbcType == MBCType::None) return;

    // $0000–$1FFF: RAM enable ($0A in the low nibble enables)
    if (addr < 0x2000) {
        m_ramEnabled = (val & 0x0F) == 0x0A;
        return;
    }

    // $2000–$3FFF: low 5 bits of the ROM bank. Writing 0 selects bank 1 —
    // the hardware quirk that makes banks $20/$40/$60 unreachable on MBC1.
    if (addr < 0x4000) {
        uint8_t bank = static_cast<uint8_t>(val & 0x1F);
        if (bank == 0) bank = 1;
        m_romBank = static_cast<uint8_t>((m_romBank & 0x60) | bank);
        return;
    }

    // $4000–$5FFF: 2 bits, used as RAM bank or as ROM bank bits 5–6.
    if (addr < 0x6000) {
        const uint8_t bits = static_cast<uint8_t>(val & 0x03);
        m_ramBank = bits;
        m_romBank = static_cast<uint8_t>((m_romBank & 0x1F) | (bits << 5));
        return;
    }

    // $6000–$7FFF: banking mode select. Mode 1 is Phase 9.
    if (addr < 0x8000) return;

    // $A000–$BFFF: external RAM
    if (addr >= 0xA000 && addr < 0xC000) {
        if (!m_ramEnabled || m_ram.empty()) return;
        const std::size_t off =
            (static_cast<std::size_t>(m_ramBank) * 0x2000 + (addr - 0xA000)) % m_ram.size();
        m_ram[off] = val;
    }
}
