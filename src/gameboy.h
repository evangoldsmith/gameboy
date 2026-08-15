#ifndef GAMEBOY_H
#define GAMEBOY_H

#include "cartridge/cartridge.h"
#include "cpu/cpu.h"
#include "memory/mmu.h"
#include "ppu/ppu.h"
#include "serial.h"
#include "timer.h"

#include <cstdint>
#include <string>

class GameBoy {
public:
    // 154 scanlines x 456 dots.
    static constexpr uint32_t TCYCLES_PER_FRAME = 70224;

    // Throws std::runtime_error if the ROM cannot be loaded.
    explicit GameBoy(const std::string& romPath);

    // Runs one instruction and advances every peripheral by the same number of
    // T-cycles. Returns the T-cycles consumed.
    uint8_t step();

    // Runs a frame's worth of cycles.
    void runFrame();

    const CartridgeHeader& header() const { return m_cart.header(); }

    const CPU& cpu() const { return m_cpu; }
    MMU&       mmu()       { return m_mmu; }
    Serial&    serial()    { return m_serial; }
    PPU&       ppu()       { return m_ppu; }

private:
    void requestInterrupt(Interrupt which);

    // Declaration order is initialisation order: MMU needs the peripherals,
    // and CPU needs the MMU.
    Cartridge m_cart;
    Serial    m_serial;
    Timer     m_timer;
    PPU       m_ppu;
    MMU       m_mmu;
    CPU       m_cpu;
};

#endif // GAMEBOY_H
