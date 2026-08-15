#ifndef CPU_H
#define CPU_H

#include <cstdint>

class MMU;

// Flag bits, stored in the low byte of AF. The low nibble of F is always zero.
inline constexpr uint8_t FLAG_Z = 0x80;  // Zero
inline constexpr uint8_t FLAG_N = 0x40;  // Subtract  (BCD)
inline constexpr uint8_t FLAG_H = 0x20;  // Half carry (BCD)
inline constexpr uint8_t FLAG_C = 0x10;  // Carry

class CPU {
public:
    explicit CPU(MMU& mmu);

    // Runs one instruction, or services one pending interrupt, and returns the
    // T-cycles consumed. Always a multiple of 4.
    uint8_t step();

    // ── Register file ────────────────────────────────────────────────────────
    // Stored as 16-bit pairs; 8-bit halves go through shift-and-mask accessors
    // because union-based punning is undefined behaviour in C++.
    uint16_t af() const { return m_af; }
    uint16_t bc() const { return m_bc; }
    uint16_t de() const { return m_de; }
    uint16_t hl() const { return m_hl; }
    uint16_t sp() const { return m_sp; }
    uint16_t pc() const { return m_pc; }

    uint8_t a() const { return hi(m_af); }
    uint8_t f() const { return lo(m_af); }
    uint8_t b() const { return hi(m_bc); }
    uint8_t c() const { return lo(m_bc); }
    uint8_t d() const { return hi(m_de); }
    uint8_t e() const { return lo(m_de); }
    uint8_t h() const { return hi(m_hl); }
    uint8_t l() const { return lo(m_hl); }

    bool halted() const { return m_halted; }
    bool ime() const { return m_ime; }

    // Set true when the CPU hits an opcode that does not exist on the SM83.
    bool stopped() const { return m_stopped; }

private:
    // ── 16-bit halves ────────────────────────────────────────────────────────
    static constexpr uint8_t hi(uint16_t r) { return static_cast<uint8_t>(r >> 8); }
    static constexpr uint8_t lo(uint16_t r) { return static_cast<uint8_t>(r & 0x00FF); }

    static constexpr void setHi(uint16_t& r, uint8_t v) {
        r = static_cast<uint16_t>((r & 0x00FFu) | (static_cast<unsigned>(v) << 8));
    }
    static constexpr void setLo(uint16_t& r, uint8_t v) {
        r = static_cast<uint16_t>((r & 0xFF00u) | v);
    }

    // ── Flags ────────────────────────────────────────────────────────────────
    bool flag(uint8_t mask) const { return (m_af & mask) != 0; }
    void setFlag(uint8_t mask, bool on);
    void setFlags(bool z, bool n, bool halfCarry, bool carry);

    // ── Memory helpers ───────────────────────────────────────────────────────
    uint8_t  read8(uint16_t addr);
    void     write8(uint16_t addr, uint8_t val);
    uint8_t  fetch8();
    uint16_t fetch16();
    void     push16(uint16_t val);
    uint16_t pop16();

    // Register index used by the 0x40–0xBF opcode block and all CB opcodes:
    // 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
    uint8_t readR8(int idx);
    void    writeR8(int idx, uint8_t val);

    // 16-bit pair index: 0=BC 1=DE 2=HL 3=SP
    uint16_t readR16(int idx) const;
    void     writeR16(int idx, uint16_t val);

    // ── ALU (defined in opcodes.cpp) ─────────────────────────────────────────
    void    aluAdd(uint8_t v, bool withCarry);
    void    aluSub(uint8_t v, bool withCarry);
    void    aluAnd(uint8_t v);
    void    aluXor(uint8_t v);
    void    aluOr(uint8_t v);
    void    aluCp(uint8_t v);
    uint8_t aluInc(uint8_t v);
    uint8_t aluDec(uint8_t v);
    void    aluAddHL(uint16_t v);
    uint16_t aluAddSP(int8_t e);

    uint8_t cbRlc(uint8_t v);
    uint8_t cbRrc(uint8_t v);
    uint8_t cbRl(uint8_t v);
    uint8_t cbRr(uint8_t v);
    uint8_t cbSla(uint8_t v);
    uint8_t cbSra(uint8_t v);
    uint8_t cbSwap(uint8_t v);
    uint8_t cbSrl(uint8_t v);

    // ── Execution (defined in opcodes.cpp) ───────────────────────────────────
    uint8_t execute(uint8_t op);
    void    executeCB(uint8_t op);

    // Returns 20 if an interrupt was dispatched, 0 otherwise.
    uint8_t serviceInterrupts();

    MMU& m_mmu;

    uint16_t m_af{};
    uint16_t m_bc{};
    uint16_t m_de{};
    uint16_t m_hl{};
    uint16_t m_sp{};
    uint16_t m_pc{};

    bool m_ime{};        // Interrupt Master Enable
    bool m_imePending{}; // EI takes effect after the *following* instruction
    bool m_halted{};
    bool m_haltBug{};    // HALT with IME=0 and an interrupt already pending
    bool m_stopped{};
};

#endif // CPU_H
