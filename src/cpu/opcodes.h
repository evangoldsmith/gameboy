#ifndef OPCODES_H
#define OPCODES_H

#include <array>
#include <cstdint>

// T-cycles per base opcode.
//
// For conditional jumps/calls/returns this is the *not taken* cost; the
// instruction adds the difference itself when the branch is taken.
// 0 marks one of the 11 opcodes that do not exist on the SM83.
// Entry $CB is a placeholder — CB-prefixed timing comes from kCBCycles below.
inline constexpr std::array<uint8_t, 256> kBaseCycles = {
    //  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
        4, 12,  8,  8,  4,  4,  8,  4, 20,  8,  8,  8,  4,  4,  8,  4, // 0x
        8, 12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4, // 1x
        8, 12,  8,  8,  4,  4,  8,  4,  8,  8,  8,  8,  4,  4,  8,  4, // 2x
        8, 12,  8,  8, 12, 12, 12,  4,  8,  8,  8,  8,  4,  4,  8,  4, // 3x
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 4x
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 5x
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 6x
        8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4, // 7x
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 8x
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 9x
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // Ax
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // Bx
        8, 12, 12, 16, 12, 16,  8, 16,  8, 16, 12,  4, 12, 24,  8, 16, // Cx
        8, 12, 12,  0, 12, 16,  8, 16,  8, 16, 12,  0, 12,  0,  8, 16, // Dx
       12, 12,  8,  0,  0, 16,  8, 16, 16,  4, 16,  0,  0,  0,  8, 16, // Ex
       12, 12,  8,  4,  0, 16,  8, 16, 12,  8, 16,  4,  0,  0,  8, 16, // Fx
};

// T-cycles per CB-prefixed opcode, including the 4 cycles for the $CB fetch.
// Register operands cost 8; (HL) operands cost 16, except BIT b,(HL) which is
// 12 because it only reads.
inline constexpr std::array<uint8_t, 256> kCBCycles = [] {
    std::array<uint8_t, 256> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
        const bool isMem = (i & 0x07) == 0x06;
        const bool isBit = i >= 0x40 && i < 0x80;
        t[i] = isMem ? (isBit ? uint8_t{12} : uint8_t{16}) : uint8_t{8};
    }
    return t;
}();

#endif // OPCODES_H
