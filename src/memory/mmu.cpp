#include "mmu.h"

namespace {
// I/O register addresses handled by a subsystem rather than the flat array.
constexpr uint16_t REG_SB   = 0xFF01;  // Serial transfer data
constexpr uint16_t REG_SC   = 0xFF02;  // Serial transfer control
constexpr uint16_t REG_DIV  = 0xFF04;  // Divider
constexpr uint16_t REG_TIMA = 0xFF05;  // Timer counter
constexpr uint16_t REG_TMA  = 0xFF06;  // Timer modulo
constexpr uint16_t REG_TAC  = 0xFF07;  // Timer control
constexpr uint16_t REG_P1   = 0xFF00;  // Joypad
constexpr uint16_t REG_IF   = 0xFF0F;  // Interrupt flags

constexpr uint16_t REG_DMA  = 0xFF46;  // OAM DMA source
constexpr uint16_t REG_BOOT = 0xFF50;  // boot ROM unmap latch

// $FF10-$FF26 are the sound registers and $FF30-$FF3F is wave RAM. The gap at
// $FF27-$FF2F is unused but still belongs to the APU: those addresses read as
// $FF, which they would not do if they fell through to the flat I/O array.
constexpr bool isApuReg(uint16_t addr) {
    return addr >= 0xFF10 && addr <= 0xFF3F;
}

// $FF40–$FF45 and $FF47–$FF4B belong to the PPU. $FF46 sits in the middle of
// that range but stays here: it copies from anywhere in the address space, so
// only the MMU can service it.
constexpr bool isPpuReg(uint16_t addr) {
    return addr >= 0xFF40 && addr <= 0xFF4B && addr != REG_DMA;
}
}  // namespace

MMU::MMU(Cartridge& cart, Serial& serial, Timer& timer, PPU& ppu, Joypad& joypad,
         APU& apu, std::vector<uint8_t> bootRom)
    : m_cart(cart), m_serial(serial), m_timer(timer), m_ppu(ppu), m_joypad(joypad),
      m_apu(apu), m_bootRom(std::move(bootRom)), m_bootActive(!m_bootRom.empty()) {}

void MMU::requestInterrupt(Interrupt which) {
    const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(which));
    m_io[REG_IF - 0xFF00] = static_cast<uint8_t>(m_io[REG_IF - 0xFF00] | bit);
}

// Peripherals advance from here rather than from GameBoy::step(), so they move
// partway through an instruction instead of in a lump afterwards. A ROM that
// reads a timer or LCD register mid-instruction sees the value hardware would
// have produced at that point.
void MMU::tick(uint8_t tcycles) {
    // One DMA byte per M-cycle. tcycles is always a multiple of 4.
    for (uint8_t done = 0; done + 4 <= tcycles; done = static_cast<uint8_t>(done + 4))
        stepOamDma();

    m_timer.tick(tcycles);
    m_ppu.tick(tcycles);
    // The APU's frame sequencer runs off DIV bit 4, so the timer must advance
    // first — writing to DIV clears that bit and clocks the sequencer.
    m_apu.tick(tcycles, m_timer.apuDivBit());

    if (m_ppu.takeVBlankIrq()) requestInterrupt(Interrupt::VBlank);
    if (m_ppu.takeStatIrq())   requestInterrupt(Interrupt::LCDStat);
    if (m_timer.takeIrq())     requestInterrupt(Interrupt::Timer);
    if (m_serial.takeIrq())    requestInterrupt(Interrupt::Serial);
    if (m_joypad.takeIrq())    requestInterrupt(Interrupt::Joypad);
}

uint8_t MMU::read(uint16_t addr) {
    if (dmaBlocks(addr)) return 0xFF;

    // $0000–$00FF: the boot ROM shadows cartridge ROM until it unmaps itself.
    // The cartridge header at $0100 onwards is never covered, which is how the
    // boot ROM reads the logo it is about to verify.
    if (m_bootActive && addr < 0x0100)
        return m_bootRom[addr];

    // $0000–$7FFF: Cartridge ROM
    if (addr < 0x8000)
        return m_cart.read(addr);

    // $8000–$9FFF: VRAM (owned by the PPU)
    if (addr < 0xA000)
        return m_ppu.readVram(addr);

    // $A000–$BFFF: External RAM (cartridge)
    if (addr < 0xC000)
        return m_cart.read(addr);

    // $C000–$DFFF: WRAM
    if (addr < 0xE000)
        return m_wram[addr - 0xC000];

    // $E000–$FDFF: Echo RAM (mirror of $C000–$DDFF)
    if (addr < 0xFE00)
        return m_wram[addr - 0xE000];

    // $FE00–$FE9F: OAM (owned by the PPU)
    if (addr < 0xFEA0)
        return m_ppu.readOam(addr);

    // $FEA0–$FEFF: Unusable
    if (addr < 0xFF00)
        return 0xFF;

    // $FF00–$FF7F: I/O registers
    if (addr < 0xFF80)
        return readIO(addr);

    // $FF80–$FFFE: HRAM
    if (addr < 0xFFFF)
        return m_hram[addr - 0xFF80];

    // $FFFF: IE register
    return m_ie;
}

void MMU::write(uint16_t addr, uint8_t val) {
    // $0000–$7FFF: Cartridge (MBC register writes)
    if (addr < 0x8000) {
        m_cart.write(addr, val);
        return;
    }

    // $8000–$9FFF: VRAM (owned by the PPU)
    if (addr < 0xA000) {
        m_ppu.writeVram(addr, val);
        return;
    }

    // $A000–$BFFF: External RAM (cartridge)
    if (addr < 0xC000) {
        m_cart.write(addr, val);
        return;
    }

    // $C000–$DFFF: WRAM
    if (addr < 0xE000) {
        m_wram[addr - 0xC000] = val;
        return;
    }

    // $E000–$FDFF: Echo RAM
    if (addr < 0xFE00) {
        m_wram[addr - 0xE000] = val;
        return;
    }

    // $FE00–$FE9F: OAM (owned by the PPU)
    if (addr < 0xFEA0) {
        m_ppu.writeOam(addr, val);
        return;
    }

    // $FEA0–$FEFF: Unusable — ignore writes
    if (addr < 0xFF00)
        return;

    // $FF00–$FF7F: I/O registers
    if (addr < 0xFF80) {
        writeIO(addr, val);
        return;
    }

    // $FF80–$FFFE: HRAM
    if (addr < 0xFFFF) {
        m_hram[addr - 0xFF80] = val;
        return;
    }

    // $FFFF: IE register
    m_ie = val;
}

// ── I/O dispatch ─────────────────────────────────────────────────────────────
// Only registers a component actually implements are served here. Everything
// else in $FF00-$FF7F either does not exist on a DMG or is CGB-only, and reads
// as $FF — falling through to a zero-filled array instead would report every
// unused register as $00, which is what Mooneye's unused_hwio checks.

uint8_t MMU::readIO(uint16_t addr) {
    switch (addr) {
        case REG_P1:   return m_joypad.read();
        case REG_SB:   return m_serial.readSB();
        case REG_SC:   return m_serial.readSC();
        case REG_DIV:  return m_timer.div();
        case REG_TIMA: return m_timer.tima();
        case REG_TMA:  return m_timer.tma();
        case REG_TAC:  return m_timer.tac();
        case REG_IF:   return static_cast<uint8_t>(m_io[REG_IF - 0xFF00] | 0xE0);
        case REG_DMA:  return m_io[REG_DMA - 0xFF00];  // reads back its source
        default:
            if (isPpuReg(addr)) return m_ppu.readReg(addr);
            if (isApuReg(addr)) return m_apu.readReg(addr);
            return 0xFF;  // unimplemented or CGB-only on a DMG
    }
}

// Writing $XX to $FF46 starts a copy of $XX00-$XX9F into OAM, one byte per
// M-cycle for 160 M-cycles. Writing again while one is running restarts it.
//
// There is one M-cycle of setup before the first byte moves, which is what
// makes a write to $FF46 from ordinary memory work at all: the instruction
// doing the writing gets to finish its own fetch before the bus is taken away.
void MMU::startOamDma(uint8_t srcHigh) {
    m_dmaSource     = static_cast<uint16_t>(static_cast<unsigned>(srcHigh) << 8);
    m_dmaIndex      = 0;
    m_dmaStartDelay = 1;
    m_dmaActive     = true;
}

void MMU::stepOamDma() {
    if (!m_dmaActive) return;

    if (m_dmaStartDelay > 0) {
        --m_dmaStartDelay;
        return;
    }

    // dmaRead rather than read(): the copy is not a CPU access, so it must not
    // be blocked by the transfer it is part of.
    const uint16_t src = static_cast<uint16_t>(m_dmaSource + m_dmaIndex);
    m_ppu.writeOam(static_cast<uint16_t>(0xFE00 + m_dmaIndex), dmaRead(src));

    if (++m_dmaIndex >= 0xA0) m_dmaActive = false;
}

// While the transfer runs the DMA controller holds the memory bus, so ROM,
// VRAM, work RAM, cartridge RAM and OAM all read $FF. This is why games copy
// the trigger routine into HRAM and spin there: code anywhere else would be
// fetching $FF as it ran.
//
// The I/O page is *not* blocked. It lives inside the CPU rather than on the
// bus the DMA has taken, so $FF46 itself stays readable throughout — which is
// exactly what Mooneye's oam_dma/reg_read checks.
bool MMU::dmaBlocks(uint16_t addr) const {
    if (!m_dmaActive || m_dmaStartDelay > 0) return false;
    return addr < 0xFF00;
}

// The transfer's own reads bypass the block — it is the thing holding the bus,
// not a victim of it.
uint8_t MMU::dmaRead(uint16_t addr) {
    const bool wasActive = m_dmaActive;
    m_dmaActive = false;
    const uint8_t v = read(addr);
    m_dmaActive = wasActive;
    return v;
}

void MMU::writeIO(uint16_t addr, uint8_t val) {
    switch (addr) {
        case REG_P1:  m_joypad.write(val);   break;
        case REG_SB:  m_serial.writeSB(val); break;
        case REG_SC:  m_serial.writeSC(val); break;
        case REG_DIV:  m_timer.resetDiv();     break;  // any write resets it
        case REG_TIMA: m_timer.writeTima(val); break;
        case REG_TMA:  m_timer.writeTma(val);  break;
        case REG_TAC:  m_timer.writeTac(val);  break;
        case REG_IF:
            // IF is stored here rather than owned by a component, and used to
            // reach m_io through the default branch. Now that unhandled writes
            // are dropped it needs saying explicitly, or raising an interrupt
            // from software silently does nothing.
            m_io[REG_IF - 0xFF00] = val;
            break;
        case REG_DMA:
            m_io[REG_DMA - 0xFF00] = val;  // reads back the last source written
            startOamDma(val);
            break;
        case REG_BOOT:
            // One-way latch: once the boot ROM is unmapped nothing maps it back.
            if (val != 0) m_bootActive = false;
            m_io[REG_BOOT - 0xFF00] = val;
            break;
        default:
            if (isPpuReg(addr))      m_ppu.writeReg(addr, val);
            else if (isApuReg(addr)) m_apu.writeReg(addr, val);
            // Writes to registers that do not exist are simply dropped.
            break;
    }
}
