# CPU — Sharp SM83

**Source:** `src/cpu/cpu.h`, `src/cpu/cpu.cpp`, `src/cpu/opcodes.h`, `src/cpu/opcodes.cpp`

Executes the LR35902/SM83 instruction set. Owns the register file, the interrupt
master enable, and halt state. Reaches everything else in the system through an
`MMU&` — it has no direct knowledge of any other component.

The SM83 is **not a Z80**: no index registers, no shadow register set, no
ED/DD/FD prefixes, no block transfers, no IN/OUT, and no sign or parity flags.
It adds `LD (HL+),A`, `LD (HL-),A`, `LDH`, `SWAP`, and `ADD SP,r8`.

## Public API

```cpp
explicit CPU(MMU& mmu);

uint8_t step();          // one instruction or one interrupt; returns T-cycles

uint16_t af/bc/de/hl/sp/pc() const;
uint8_t  a/f/b/c/d/e/h/l() const;
bool     halted() const;
bool     ime() const;
bool     stopped() const;   // hit an opcode that does not exist on the SM83
```

## Register file

Registers are stored as six `uint16_t` pairs. The 8-bit halves are read and
written through `hi()`/`lo()`/`setHi()`/`setLo()` — shift-and-mask rather than a
`union`, because union-based type punning is undefined behaviour in C++.

Flags live in the low byte of `AF`:

| Bit | Constant | Meaning |
|---|---|---|
| 7 | `FLAG_Z` | Zero |
| 6 | `FLAG_N` | Subtract (used by DAA) |
| 5 | `FLAG_H` | Half carry (used by DAA) |
| 4 | `FLAG_C` | Carry |

The low nibble of F is always zero. `POP AF` masks it off explicitly
(`opcodes.cpp`, case `0xF1`) — this is a real hardware behaviour that Blargg's
tests check.

Two flag helpers:

- `setFlag(mask, bool)` — touches one flag, leaves the rest alone. Used where an
  instruction only affects some flags (`BIT`, `CPL`).
- `setFlags(z, n, h, c)` — rebuilds the whole F byte at once. Used by the ALU,
  where every flag has a defined value.

## Indexed register access

The opcode grid encodes operands as a 3-bit index, which `readR8`/`writeR8`
decode:

```
0=B  1=C  2=D  3=E  4=H  5=L  6=(HL)  7=A
```

Index 6 is a **memory access through HL**, not a register. This is what makes
`readR8`/`writeR8` non-const and why they must go through the MMU. The extra
cycles that memory operand costs are already baked into the cycle table, so
callers do not add anything.

`readR16`/`writeR16` do the same for 16-bit pairs (`0=BC 1=DE 2=HL 3=SP`). Note
that `PUSH`/`POP` use a *different* mapping where index 3 is `AF` instead of
`SP`, so those four opcodes are written out longhand rather than going through
`readR16`.

## `step()`

Order of operations matters here:

1. Latch `m_imePending`. `EI` does not enable interrupts immediately — the flag
   flips *after* the instruction following `EI` executes, so the pending state
   is captured at the top of the step and applied at the bottom.
2. `serviceInterrupts()`. If an interrupt dispatches, return 20 T-cycles and
   execute nothing else this step.
3. Apply the latched IME enable.
4. If halted or stopped, burn 4 T-cycles and return. Peripherals keep ticking
   because `GameBoy::step()` advances them from the returned cycle count.
5. Otherwise fetch and execute.

## `serviceInterrupts()`

```
pending = IE ($FFFF) & IF ($FF0F) & 0x1F
```

If `pending` is zero, nothing happens. Otherwise the CPU **wakes from HALT
regardless of IME** — that is deliberate, and separate from whether the handler
runs. If IME is clear, it returns 0 and execution simply continues after the
HALT.

With IME set, the lowest set bit wins (VBlank → STAT → Timer → Serial → Joypad).
Dispatch clears IME, clears that one IF bit, pushes PC, and jumps to
`0x0040 + bit * 8`. Costs 20 T-cycles.

## Instruction dispatch

`execute(op)` starts from `kBaseCycles[op]` and returns the final count, adding
to it when a conditional branch is taken.

Most of the instruction set is decoded structurally rather than case by case:

**`$40`–`$BF` (128 opcodes)** is a regular grid. The low three bits are the
source register index; bits 3–5 are either the destination register (`$40`–`$7F`,
`LD r,r'`) or the ALU operation (`$80`–`$BF`). `$76` would be `LD (HL),(HL)` and
is `HALT` instead, so it is excluded and handled in the main switch.

**`$00`–`$3F` columns 4, 5, 6** are `INC r`, `DEC r`, and `LD r,d8`, indexed the
same way.

**`(op & 0xC7) == 0xC6`** is ALU-with-immediate; **`== 0xC7`** is `RST`, whose
vector is `op & 0x38`.

**All 256 CB opcodes** decode as `reg = op & 7`, `idx = (op >> 3) & 7`, split
into four ranges: rotates/shifts (`$00`–`$3F`), `BIT` (`$40`–`$7F`), `RES`
(`$80`–`$BF`), `SET` (`$C0`–`$FF`).

Everything left over — jumps, calls, stack ops, the accumulator rotates, the
misc flag ops — is a conventional `switch`.

The 11 opcodes that do not exist on the SM83 (`$D3 $DB $DD $E3 $E4 $EB $EC $ED
$F4 $FC $FD`) fall to `default`, which prints to stderr and sets `m_stopped`.

## Cycle tables

`opcodes.h` holds two `constexpr std::array<uint8_t, 256>` tables in T-cycles.

`kBaseCycles` stores the **not-taken** cost for conditional branches; the
instruction adds the difference itself (+4 for `JR`/`JP`, +12 for `CALL`/`RET`).
Entries of `0` mark nonexistent opcodes. Entry `$CB` is a placeholder that
`execute` overwrites from the CB table.

`kCBCycles` is built by a constexpr lambda rather than typed out, because the
rule is mechanical: 8 cycles for register operands, 16 for `(HL)`, except
`BIT b,(HL)` which is 12 because it only reads.

## ALU details

These are the parts that break first, and the reasons why:

- **`aluAdd` / `aluSub`** compute half-carry from the low nibbles only:
  `((a & 0xF) + (v & 0xF) + carry) > 0xF`. Subtraction mirrors it with `< 0`.
- **`aluInc` / `aluDec`** preserve the carry flag — they are the only arithmetic
  ops that do. Half-carry is derived from the value *before* the operation.
- **`aluAddHL`** leaves Z untouched, and takes half-carry from bit 11 → 12
  (`0x0FFF`), not bit 3 → 4.
- **`aluAddSP`** (used by both `ADD SP,r8` and `LD HL,SP+r8`) sets H and C from
  the **low byte** addition and always clears Z and N — the opposite of what the
  16-bit operand width suggests.
- **`DAA`** builds a correction from N/H/C and the current value of A, then adds
  or subtracts it depending on N. Carry is set-or-kept, never cleared.
- **Accumulator rotates** (`RLCA`, `RRCA`, `RLA`, `RRA`) reuse the CB rotate
  helpers but then force `Z = 0`, unlike their CB counterparts which set Z
  normally.
- **`cbSra`** preserves bit 7 (arithmetic shift); `cbSrl` does not.

## HALT and the HALT bug

`HALT` checks `IE & IF & 0x1F` at the moment it executes:

- IME set, or nothing pending → `m_halted = true`, normal sleep.
- IME clear **and** something already pending → `m_haltBug = true`.

The HALT bug makes PC fail to increment exactly once. This is implemented inside
`fetch8()`: if the flag is set, the byte is read and the flag cleared, but PC
stays put — so the next byte gets read twice. That is real hardware behaviour,
tested by Blargg's `halt_bug`.

## Current state

All 245 valid base opcodes and all 256 CB opcodes are implemented. 10 of 11
`cpu_instrs` sub-tests pass.

## Not implemented yet

- **Cycle granularity is per-instruction, not per-M-cycle.** Memory accesses all
  happen at once rather than being spread across the instruction's cycles. This
  is invisible to `cpu_instrs` but matters for `mem_timing` (Phase 12).
- **`STOP`** consumes its padding byte and otherwise does nothing.
- `02-interrupts` fails pending the timer — see [timer.md](timer.md).
