#include "cpu.h"

#include "../memory/mmu.h"
#include "opcodes.h"

#include <cstdio>
#include <cstdlib>

CPU::CPU(MMU& mmu) : m_mmu(mmu) {
    // Post-boot register state (DMG). Lets us skip the boot ROM and jump
    // straight into cartridge code at $0100. See roadmap Phase 1.
    m_af = 0x01B0;
    m_bc = 0x0013;
    m_de = 0x00D8;
    m_hl = 0x014D;
    m_sp = 0xFFFE;
    m_pc = 0x0100;

    m_mmu.write(0xFF50, 0x01);  // boot ROM unmapped
}

// ── Flags ────────────────────────────────────────────────────────────────────

void CPU::setFlag(uint8_t mask, bool on) {
    const uint8_t cur = lo(m_af);
    setLo(m_af, static_cast<uint8_t>(on ? (cur | mask)
                                        : (cur & static_cast<uint8_t>(~mask))));
}

void CPU::setFlags(bool z, bool n, bool halfCarry, bool carry) {
    unsigned nf = 0;
    if (z) nf |= FLAG_Z;
    if (n) nf |= FLAG_N;
    if (halfCarry) nf |= FLAG_H;
    if (carry) nf |= FLAG_C;
    setLo(m_af, static_cast<uint8_t>(nf));
}

// ── Memory helpers ───────────────────────────────────────────────────────────

void CPU::tick(uint8_t tcycles) {
    m_stepCycles = static_cast<uint8_t>(m_stepCycles + tcycles);
    m_mmu.tick(tcycles);
}

uint8_t CPU::read8(uint16_t addr) {
    tick(4);
    return m_mmu.read(addr);
}

void CPU::write8(uint16_t addr, uint8_t val) {
    tick(4);
    m_mmu.write(addr, val);
}

uint8_t CPU::fetch8() {
    const uint8_t v = read8(m_pc);
    // The HALT bug makes PC fail to increment exactly once.
    if (m_haltBug)
        m_haltBug = false;
    else
        m_pc = static_cast<uint16_t>(m_pc + 1);
    return v;
}

uint16_t CPU::fetch16() {
    const uint8_t low  = fetch8();
    const uint8_t high = fetch8();
    return static_cast<uint16_t>((static_cast<unsigned>(high) << 8) | low);
}

void CPU::push16(uint16_t val) {
    m_sp = static_cast<uint16_t>(m_sp - 1);
    write8(m_sp, hi(val));
    m_sp = static_cast<uint16_t>(m_sp - 1);
    write8(m_sp, lo(val));
}

uint16_t CPU::pop16() {
    const uint8_t low = read8(m_sp);
    m_sp = static_cast<uint16_t>(m_sp + 1);
    const uint8_t high = read8(m_sp);
    m_sp = static_cast<uint16_t>(m_sp + 1);
    return static_cast<uint16_t>((static_cast<unsigned>(high) << 8) | low);
}

// ── Indexed register access ──────────────────────────────────────────────────

uint8_t CPU::readR8(int idx) {
    switch (idx) {
        case 0:  return b();
        case 1:  return c();
        case 2:  return d();
        case 3:  return e();
        case 4:  return h();
        case 5:  return l();
        case 6:  return read8(m_hl);
        default: return a();
    }
}

void CPU::writeR8(int idx, uint8_t val) {
    switch (idx) {
        case 0:  setHi(m_bc, val); break;
        case 1:  setLo(m_bc, val); break;
        case 2:  setHi(m_de, val); break;
        case 3:  setLo(m_de, val); break;
        case 4:  setHi(m_hl, val); break;
        case 5:  setLo(m_hl, val); break;
        case 6:  write8(m_hl, val); break;
        default: setHi(m_af, val); break;
    }
}

uint16_t CPU::readR16(int idx) const {
    switch (idx) {
        case 0:  return m_bc;
        case 1:  return m_de;
        case 2:  return m_hl;
        default: return m_sp;
    }
}

void CPU::writeR16(int idx, uint16_t val) {
    switch (idx) {
        case 0:  m_bc = val; break;
        case 1:  m_de = val; break;
        case 2:  m_hl = val; break;
        default: m_sp = val; break;
    }
}

// ── Interrupts ───────────────────────────────────────────────────────────────

bool CPU::serviceInterrupts() {
    // Peek without ticking: deciding whether to dispatch is not itself a bus
    // access, and charging cycles for it would break every instruction's count.
    const uint8_t ie      = m_mmu.read(0xFFFF);
    const uint8_t irqf    = m_mmu.read(0xFF0F);
    const uint8_t pending = static_cast<uint8_t>(ie & irqf & 0x1F);

    if (pending == 0) return false;

    // Any pending interrupt wakes the CPU, whether or not IME is set.
    m_halted = false;
    if (!m_ime) return false;

    // Highest priority (lowest bit) wins: VBlank, STAT, Timer, Serial, Joypad.
    int bit = 0;
    while (((pending >> bit) & 1) == 0) ++bit;

    m_ime = false;

    // 5 M-cycles: two internal, the two stack writes from push16, then one more
    // while the vector is loaded.
    tick(4);
    tick(4);
    m_mmu.write(0xFF0F, static_cast<uint8_t>(irqf & ~(1u << bit)));
    push16(m_pc);
    m_pc = static_cast<uint16_t>(0x0040 + bit * 8);
    tick(4);
    return true;
}

// Blargg's instr_timing independently verifies the cycle table, so it serves as
// an oracle here: spreading an instruction's time across individual M-cycles
// must not change its total. Anything that does is a bug in the distribution,
// and this catches it at the instruction that caused it rather than as a
// mysterious timing failure much later.
void CPU::verifyStepCycles() const {
    if (m_stepCycles == m_expectedCycles) return;

    std::fprintf(stderr,
                 "CPU cycle mismatch at $%04X: opcode $%02X ticked %u T-cycles, "
                 "cycle table expects %u\n",
                 static_cast<unsigned>(m_pc), m_currentOp,
                 static_cast<unsigned>(m_stepCycles),
                 static_cast<unsigned>(m_expectedCycles));
    std::abort();
}

// ── Step ─────────────────────────────────────────────────────────────────────

uint8_t CPU::step() {
    m_stepCycles = 0;

    // EI enables interrupts only *after* the instruction that follows it, so
    // the pending flag is consumed at the top of the next step.
    const bool enableImeAfter = m_imePending;

    if (serviceInterrupts()) {
        if (enableImeAfter) m_imePending = false;
        return m_stepCycles;
    }

    if (enableImeAfter) {
        m_ime = true;
        m_imePending = false;
    }

    if (m_halted || m_stopped) {
        // The CPU idles but peripherals keep running, which is what eventually
        // produces the interrupt that wakes it.
        tick(4);
        return m_stepCycles;
    }

    execute(fetch8());

#ifndef NDEBUG
    // The cycle table is independently verified by Blargg's instr_timing, so
    // it doubles as a check that redistributing time across M-cycles did not
    // change any instruction's total.
    verifyStepCycles();
#endif

    return m_stepCycles;
}
