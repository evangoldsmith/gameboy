#include "gameboy.h"

#include <fstream>
#include <iostream>

// A boot ROM is exactly 256 bytes. Anything else is rejected rather than
// padded: a wrong-sized file is a mistake, and running it would fail in
// confusing ways much later.
std::vector<uint8_t> GameBoy::loadBootRom(const std::string& path) {
    if (path.empty()) return {};

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::cerr << "Warning: cannot open boot ROM " << path
                  << ", starting without it\n";
        return {};
    }

    const auto size = static_cast<std::size_t>(f.tellg());
    if (size != 0x100) {
        std::cerr << "Warning: boot ROM " << path << " is " << size
                  << " bytes, expected 256 — starting without it\n";
        return {};
    }

    std::vector<uint8_t> rom(size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(rom.data()), static_cast<std::streamsize>(size));
    return rom;
}

GameBoy::GameBoy(const std::string& romPath, const std::string& bootPath)
    : m_cart(Cartridge::load(romPath)),
      m_bootRom(loadBootRom(bootPath)),
      m_mmu(m_cart, m_serial, m_timer, m_ppu, m_joypad, m_apu, m_bootRom),
      m_cpu(m_mmu) {
    if (!m_mmu.bootRomActive()) return;

    // Components default to the state a boot ROM would have left behind, since
    // that is the common case. With one actually running, hand it a cold
    // machine instead and let it do the initialising itself — otherwise the
    // LCD is already on and rendering before the boot ROM has set anything up.
    m_mmu.write(0xFF40, 0x00);  // LCDC: LCD off
    m_mmu.write(0xFF47, 0x00);  // BGP
    m_mmu.write(0xFF48, 0x00);  // OBP0
    m_mmu.write(0xFF49, 0x00);  // OBP1
}

// Peripherals are advanced from inside CPU::step(), one M-cycle at a time, via
// MMU::tick(). See docs/cpu.md.
uint8_t GameBoy::step() { return m_cpu.step(); }

void GameBoy::runFrame() {
    uint32_t elapsed = 0;
    while (elapsed < TCYCLES_PER_FRAME) {
        if (m_cpu.stopped()) return;
        elapsed += step();
    }
}
