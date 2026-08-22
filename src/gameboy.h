#ifndef GAMEBOY_H
#define GAMEBOY_H

#include "apu/apu.h"
#include "cartridge/cartridge.h"
#include "cpu/cpu.h"
#include "joypad.h"
#include "memory/mmu.h"
#include "ppu/ppu.h"
#include "serial.h"
#include "timer.h"

#include <cstdint>
#include <string>
#include <vector>

class GameBoy {
public:
    // 154 scanlines x 456 dots.
    static constexpr uint32_t TCYCLES_PER_FRAME = 70224;

    // Throws std::runtime_error if the ROM cannot be loaded. bootPath is
    // optional: with a 256-byte boot ROM the system cold-starts at $0000 and
    // plays the startup sequence, without one it jumps straight to $0100.
    explicit GameBoy(const std::string& romPath, const std::string& bootPath = "");

    // Reads a 256-byte boot ROM, or returns empty if the path is blank, missing
    // or the wrong size. A missing boot ROM is a normal configuration, not an
    // error.
    static std::vector<uint8_t> loadBootRom(const std::string& path);

    bool bootRomActive() const { return m_mmu.bootRomActive(); }

    // Runs one instruction and advances every peripheral by the same number of
    // T-cycles. Returns the T-cycles consumed.
    uint8_t step();

    // Runs a frame's worth of cycles.
    void runFrame();

    const CartridgeHeader& header() const { return m_cart.header(); }

    // Persists cartridge RAM if the cartridge is battery-backed and RAM has
    // changed. Cheap to call repeatedly — it is a no-op when nothing is dirty.
    bool flushSave() { return m_cart.flushSave(); }

    const CPU& cpu() const { return m_cpu; }
    MMU&       mmu()       { return m_mmu; }
    Serial&    serial()    { return m_serial; }
    PPU&       ppu()       { return m_ppu; }
    Joypad&    joypad()    { return m_joypad; }
    APU&       apu()       { return m_apu; }

private:
    // Declaration order is initialisation order: MMU needs the peripherals,
    // and CPU needs the MMU.
    Cartridge m_cart;
    Serial    m_serial;
    Timer     m_timer;
    PPU       m_ppu;
    Joypad    m_joypad;
    APU       m_apu;
    // Declared before the MMU because the MMU consumes it.
    std::vector<uint8_t> m_bootRom;
    MMU       m_mmu;
    CPU       m_cpu;
};

#endif // GAMEBOY_H
