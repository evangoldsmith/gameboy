#include "opcodes.h"

#include "cpu.h"

#include <cstdio>

// ── ALU ──────────────────────────────────────────────────────────────────────

void CPU::aluAdd(uint8_t v, bool withCarry) {
    const unsigned cy  = (withCarry && flag(FLAG_C)) ? 1u : 0u;
    const unsigned av  = a();
    const unsigned res = av + v + cy;
    const bool     hc  = ((av & 0x0Fu) + (v & 0x0Fu) + cy) > 0x0Fu;
    setHi(m_af, static_cast<uint8_t>(res));
    setFlags((res & 0xFFu) == 0, false, hc, res > 0xFFu);
}

void CPU::aluSub(uint8_t v, bool withCarry) {
    const int cy  = (withCarry && flag(FLAG_C)) ? 1 : 0;
    const int av  = a();
    const int res = av - v - cy;
    const bool hc = ((av & 0x0F) - (v & 0x0F) - cy) < 0;
    setHi(m_af, static_cast<uint8_t>(res));
    setFlags((res & 0xFF) == 0, true, hc, res < 0);
}

void CPU::aluAnd(uint8_t v) {
    const uint8_t res = static_cast<uint8_t>(a() & v);
    setHi(m_af, res);
    setFlags(res == 0, false, true, false);
}

void CPU::aluXor(uint8_t v) {
    const uint8_t res = static_cast<uint8_t>(a() ^ v);
    setHi(m_af, res);
    setFlags(res == 0, false, false, false);
}

void CPU::aluOr(uint8_t v) {
    const uint8_t res = static_cast<uint8_t>(a() | v);
    setHi(m_af, res);
    setFlags(res == 0, false, false, false);
}

void CPU::aluCp(uint8_t v) {
    const int av  = a();
    const int res = av - v;
    setFlags((res & 0xFF) == 0, true, ((av & 0x0F) - (v & 0x0F)) < 0, res < 0);
}

uint8_t CPU::aluInc(uint8_t v) {
    const uint8_t res = static_cast<uint8_t>(v + 1);
    // Carry is preserved by INC.
    setFlags(res == 0, false, (v & 0x0F) == 0x0F, flag(FLAG_C));
    return res;
}

uint8_t CPU::aluDec(uint8_t v) {
    const uint8_t res = static_cast<uint8_t>(v - 1);
    setFlags(res == 0, true, (v & 0x0F) == 0x00, flag(FLAG_C));
    return res;
}

void CPU::aluAddHL(uint16_t v) {
    const unsigned res = static_cast<unsigned>(m_hl) + v;
    const bool     hc  = ((m_hl & 0x0FFFu) + (v & 0x0FFFu)) > 0x0FFFu;
    // Z is untouched by ADD HL,rr.
    setFlags(flag(FLAG_Z), false, hc, res > 0xFFFFu);
    m_hl = static_cast<uint16_t>(res);
}

uint16_t CPU::aluAddSP(int8_t e) {
    // H and C come from the *low byte* addition, and Z/N are always cleared —
    // the opposite of what the 16-bit width suggests.
    const unsigned spv = m_sp;
    const unsigned ev  = static_cast<uint8_t>(e);
    setFlags(false, false,
             ((spv & 0x0Fu) + (ev & 0x0Fu)) > 0x0Fu,
             ((spv & 0xFFu) + (ev & 0xFFu)) > 0xFFu);
    return static_cast<uint16_t>(static_cast<int>(m_sp) + e);
}

// ── CB rotates and shifts ────────────────────────────────────────────────────

uint8_t CPU::cbRlc(uint8_t v) {
    const unsigned cy  = (v >> 7) & 1u;
    const uint8_t  res = static_cast<uint8_t>((static_cast<unsigned>(v) << 1) | cy);
    setFlags(res == 0, false, false, cy != 0);
    return res;
}

uint8_t CPU::cbRrc(uint8_t v) {
    const unsigned cy  = v & 1u;
    const uint8_t  res = static_cast<uint8_t>((v >> 1) | (cy << 7));
    setFlags(res == 0, false, false, cy != 0);
    return res;
}

uint8_t CPU::cbRl(uint8_t v) {
    const unsigned old = flag(FLAG_C) ? 1u : 0u;
    const unsigned cy  = (v >> 7) & 1u;
    const uint8_t  res = static_cast<uint8_t>((static_cast<unsigned>(v) << 1) | old);
    setFlags(res == 0, false, false, cy != 0);
    return res;
}

uint8_t CPU::cbRr(uint8_t v) {
    const unsigned old = flag(FLAG_C) ? 1u : 0u;
    const unsigned cy  = v & 1u;
    const uint8_t  res = static_cast<uint8_t>((v >> 1) | (old << 7));
    setFlags(res == 0, false, false, cy != 0);
    return res;
}

uint8_t CPU::cbSla(uint8_t v) {
    const unsigned cy  = (v >> 7) & 1u;
    const uint8_t  res = static_cast<uint8_t>(v << 1);
    setFlags(res == 0, false, false, cy != 0);
    return res;
}

uint8_t CPU::cbSra(uint8_t v) {
    const unsigned cy  = v & 1u;
    const uint8_t  res = static_cast<uint8_t>((v >> 1) | (v & 0x80u));  // bit 7 sticks
    setFlags(res == 0, false, false, cy != 0);
    return res;
}

uint8_t CPU::cbSwap(uint8_t v) {
    const uint8_t res = static_cast<uint8_t>((v << 4) | (v >> 4));
    setFlags(res == 0, false, false, false);
    return res;
}

uint8_t CPU::cbSrl(uint8_t v) {
    const unsigned cy  = v & 1u;
    const uint8_t  res = static_cast<uint8_t>(v >> 1);
    setFlags(res == 0, false, false, cy != 0);
    return res;
}

// ── CB-prefixed dispatch ─────────────────────────────────────────────────────

void CPU::executeCB(uint8_t op) {
    const int reg = op & 0x07;         // 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
    const int idx = (op >> 3) & 0x07;  // operation, or bit number

    const uint8_t v = readR8(reg);

    if (op < 0x40) {  // RLC RRC RL RR SLA SRA SWAP SRL
        uint8_t res = 0;
        switch (idx) {
            case 0: res = cbRlc(v);  break;
            case 1: res = cbRrc(v);  break;
            case 2: res = cbRl(v);   break;
            case 3: res = cbRr(v);   break;
            case 4: res = cbSla(v);  break;
            case 5: res = cbSra(v);  break;
            case 6: res = cbSwap(v); break;
            default: res = cbSrl(v); break;
        }
        writeR8(reg, res);
    } else if (op < 0x80) {  // BIT b,r — carry is untouched
        setFlag(FLAG_Z, (v & (1u << idx)) == 0);
        setFlag(FLAG_N, false);
        setFlag(FLAG_H, true);
    } else if (op < 0xC0) {  // RES b,r
        writeR8(reg, static_cast<uint8_t>(v & ~(1u << idx)));
    } else {                 // SET b,r
        writeR8(reg, static_cast<uint8_t>(v | (1u << idx)));
    }
}

// ── Base dispatch ────────────────────────────────────────────────────────────

uint8_t CPU::execute(uint8_t op) {
    uint8_t cycles = kBaseCycles[op];

    // Condition code index used by JR/JP/CALL/RET cc: 0=NZ 1=Z 2=NC 3=C
    const auto cond = [this](int cc) -> bool {
        switch (cc) {
            case 0:  return !flag(FLAG_Z);
            case 1:  return flag(FLAG_Z);
            case 2:  return !flag(FLAG_C);
            default: return flag(FLAG_C);
        }
    };

    // $40–$BF is a regular grid: LD r,r' then ALU A,r, both indexed by the low
    // three bits. $76 would be LD (HL),(HL) and is HALT instead.
    if (op >= 0x40 && op <= 0xBF && op != 0x76) {
        const int src = op & 0x07;
        if (op < 0x80) {
            writeR8((op >> 3) & 0x07, readR8(src));
        } else {
            const uint8_t v = readR8(src);
            switch ((op >> 3) & 0x07) {
                case 0: aluAdd(v, false); break;
                case 1: aluAdd(v, true);  break;
                case 2: aluSub(v, false); break;
                case 3: aluSub(v, true);  break;
                case 4: aluAnd(v);        break;
                case 5: aluXor(v);        break;
                case 6: aluOr(v);         break;
                default: aluCp(v);        break;
            }
        }
        return cycles;
    }

    // INC r / DEC r / LD r,d8 share a column pattern across $00–$3F.
    if (op < 0x40) {
        const int r = (op >> 3) & 0x07;
        switch (op & 0x07) {
            case 4: writeR8(r, aluInc(readR8(r))); return cycles;
            case 5: writeR8(r, aluDec(readR8(r))); return cycles;
            case 6: writeR8(r, fetch8());          return cycles;
            default: break;
        }
    }

    // ALU A,d8 ($C6,$CE,$D6,$DE,$E6,$EE,$F6,$FE)
    if ((op & 0xC7) == 0xC6) {
        const uint8_t v = fetch8();
        switch ((op >> 3) & 0x07) {
            case 0: aluAdd(v, false); break;
            case 1: aluAdd(v, true);  break;
            case 2: aluSub(v, false); break;
            case 3: aluSub(v, true);  break;
            case 4: aluAnd(v);        break;
            case 5: aluXor(v);        break;
            case 6: aluOr(v);         break;
            default: aluCp(v);        break;
        }
        return cycles;
    }

    // RST n ($C7,$CF,...,$FF)
    if ((op & 0xC7) == 0xC7) {
        push16(m_pc);
        m_pc = static_cast<uint16_t>(op & 0x38);
        return cycles;
    }

    switch (op) {
        case 0x00: break;  // NOP

        // ── 16-bit loads ─────────────────────────────────────────────────────
        case 0x01: case 0x11: case 0x21: case 0x31:
            writeR16((op >> 4) & 0x03, fetch16());
            break;

        case 0x08: {  // LD (a16),SP
            const uint16_t addr = fetch16();
            write8(addr, static_cast<uint8_t>(m_sp & 0x00FF));
            write8(static_cast<uint16_t>(addr + 1), static_cast<uint8_t>(m_sp >> 8));
            break;
        }

        case 0xF8:  // LD HL,SP+r8
            m_hl = aluAddSP(static_cast<int8_t>(fetch8()));
            break;

        case 0xF9: m_sp = m_hl; break;  // LD SP,HL

        // ── Indirect 8-bit loads ─────────────────────────────────────────────
        case 0x02: write8(m_bc, a()); break;
        case 0x12: write8(m_de, a()); break;
        case 0x22: write8(m_hl, a()); m_hl = static_cast<uint16_t>(m_hl + 1); break;
        case 0x32: write8(m_hl, a()); m_hl = static_cast<uint16_t>(m_hl - 1); break;

        case 0x0A: setHi(m_af, read8(m_bc)); break;
        case 0x1A: setHi(m_af, read8(m_de)); break;
        case 0x2A: setHi(m_af, read8(m_hl)); m_hl = static_cast<uint16_t>(m_hl + 1); break;
        case 0x3A: setHi(m_af, read8(m_hl)); m_hl = static_cast<uint16_t>(m_hl - 1); break;

        case 0xE0: write8(static_cast<uint16_t>(0xFF00 + fetch8()), a()); break;
        case 0xF0: setHi(m_af, read8(static_cast<uint16_t>(0xFF00 + fetch8()))); break;
        case 0xE2: write8(static_cast<uint16_t>(0xFF00 + c()), a()); break;
        case 0xF2: setHi(m_af, read8(static_cast<uint16_t>(0xFF00 + c()))); break;
        case 0xEA: write8(fetch16(), a()); break;
        case 0xFA: setHi(m_af, read8(fetch16())); break;

        // ── 16-bit arithmetic ────────────────────────────────────────────────
        case 0x03: case 0x13: case 0x23: case 0x33: {
            const int r = (op >> 4) & 0x03;
            writeR16(r, static_cast<uint16_t>(readR16(r) + 1));
            break;
        }
        case 0x0B: case 0x1B: case 0x2B: case 0x3B: {
            const int r = (op >> 4) & 0x03;
            writeR16(r, static_cast<uint16_t>(readR16(r) - 1));
            break;
        }
        case 0x09: case 0x19: case 0x29: case 0x39:
            aluAddHL(readR16((op >> 4) & 0x03));
            break;

        case 0xE8:  // ADD SP,r8
            m_sp = aluAddSP(static_cast<int8_t>(fetch8()));
            break;

        // ── Accumulator rotates (Z is always cleared, unlike the CB forms) ───
        case 0x07: setHi(m_af, cbRlc(a())); setFlag(FLAG_Z, false); break;
        case 0x0F: setHi(m_af, cbRrc(a())); setFlag(FLAG_Z, false); break;
        case 0x17: setHi(m_af, cbRl(a()));  setFlag(FLAG_Z, false); break;
        case 0x1F: setHi(m_af, cbRr(a()));  setFlag(FLAG_Z, false); break;

        // ── Misc accumulator / flag ops ──────────────────────────────────────
        case 0x27: {  // DAA
            unsigned adjust = 0;
            bool carry = flag(FLAG_C);
            if (flag(FLAG_H) || (!flag(FLAG_N) && (a() & 0x0F) > 0x09))
                adjust |= 0x06u;
            if (carry || (!flag(FLAG_N) && a() > 0x99)) {
                adjust |= 0x60u;
                carry = true;
            }
            const uint8_t res = static_cast<uint8_t>(
                flag(FLAG_N) ? a() - adjust : a() + adjust);
            setHi(m_af, res);
            setFlags(res == 0, flag(FLAG_N), false, carry);
            break;
        }
        case 0x2F:  // CPL
            setHi(m_af, static_cast<uint8_t>(~a()));
            setFlag(FLAG_N, true);
            setFlag(FLAG_H, true);
            break;
        case 0x37:  // SCF
            setFlags(flag(FLAG_Z), false, false, true);
            break;
        case 0x3F:  // CCF
            setFlags(flag(FLAG_Z), false, false, !flag(FLAG_C));
            break;

        // ── Jumps ────────────────────────────────────────────────────────────
        case 0x18: {  // JR r8
            const int8_t e = static_cast<int8_t>(fetch8());
            m_pc = static_cast<uint16_t>(m_pc + e);
            break;
        }
        case 0x20: case 0x28: case 0x30: case 0x38: {  // JR cc,r8
            const int8_t e = static_cast<int8_t>(fetch8());
            if (cond((op >> 3) & 0x03)) {
                m_pc = static_cast<uint16_t>(m_pc + e);
                cycles = static_cast<uint8_t>(cycles + 4);
            }
            break;
        }
        case 0xC3: m_pc = fetch16(); break;  // JP a16
        case 0xC2: case 0xCA: case 0xD2: case 0xDA: {  // JP cc,a16
            const uint16_t target = fetch16();
            if (cond((op >> 3) & 0x03)) {
                m_pc = target;
                cycles = static_cast<uint8_t>(cycles + 4);
            }
            break;
        }
        case 0xE9: m_pc = m_hl; break;  // JP HL

        // ── Calls and returns ────────────────────────────────────────────────
        case 0xCD: {  // CALL a16
            const uint16_t target = fetch16();
            push16(m_pc);
            m_pc = target;
            break;
        }
        case 0xC4: case 0xCC: case 0xD4: case 0xDC: {  // CALL cc,a16
            const uint16_t target = fetch16();
            if (cond((op >> 3) & 0x03)) {
                push16(m_pc);
                m_pc = target;
                cycles = static_cast<uint8_t>(cycles + 12);
            }
            break;
        }
        case 0xC9: m_pc = pop16(); break;  // RET
        case 0xD9:                         // RETI
            m_pc = pop16();
            m_ime = true;
            break;
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:  // RET cc
            if (cond((op >> 3) & 0x03)) {
                m_pc = pop16();
                cycles = static_cast<uint8_t>(cycles + 12);
            }
            break;

        // ── Stack ────────────────────────────────────────────────────────────
        case 0xC1: m_bc = pop16(); break;
        case 0xD1: m_de = pop16(); break;
        case 0xE1: m_hl = pop16(); break;
        case 0xF1: m_af = static_cast<uint16_t>(pop16() & 0xFFF0); break;  // low nibble of F is always 0

        case 0xC5: push16(m_bc); break;
        case 0xD5: push16(m_de); break;
        case 0xE5: push16(m_hl); break;
        case 0xF5: push16(m_af); break;

        // ── Control ──────────────────────────────────────────────────────────
        case 0x76: {  // HALT
            const uint8_t pending =
                static_cast<uint8_t>(read8(0xFFFF) & read8(0xFF0F) & 0x1F);
            if (!m_ime && pending != 0)
                m_haltBug = true;  // PC fails to increment on the next fetch
            else
                m_halted = true;
            break;
        }
        case 0x10:  // STOP — consumes a padding byte
            fetch8();
            break;
        case 0xF3: m_ime = false; m_imePending = false; break;  // DI
        case 0xFB: m_imePending = true; break;                  // EI

        case 0xCB: {
            const uint8_t cb = fetch8();
            cycles = kCBCycles[cb];
            executeCB(cb);
            break;
        }

        default:
            std::fprintf(stderr,
                         "Illegal opcode $%02X at $%04X — CPU stopped\n",
                         op, static_cast<unsigned>(m_pc - 1));
            m_stopped = true;
            cycles = 4;
            break;
    }

    return cycles;
}
