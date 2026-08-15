# Logger — trace output

**Source:** `src/debug/logger.h`, `src/debug/logger.cpp`

Formats CPU state as a Game Boy Doctor trace line. This is the primary debugging
tool for the CPU: dump a trace, diff it against a known-good log, and the first
mismatching line is the instruction that broke.

## Public API

```cpp
namespace logger {
std::string traceLine(const CPU& cpu, MMU& mmu);
}
```

`mmu` is non-const because `MMU::read` is non-const — see [mmu.md](mmu.md).

## Format

```
A:01 F:B0 B:00 C:13 D:00 E:D8 H:01 L:4D SP:FFFE PC:0100 PCMEM:00,C3,37,06
```

All values uppercase hex, 8-bit registers zero-padded to 2 digits and 16-bit to
4. `PCMEM` is the four bytes at PC through PC+3, comma-separated. Built with
`std::format`.

The format is fixed by [gameboy-doctor](https://github.com/robert/gameboy-doctor)
— it is not a design choice, and changing the spacing or padding breaks the
diff.

## Usage

One line is emitted **before** each instruction executes, so the state shown is
the input to the instruction on that line:

```bash
./build/debug/gameboy roms/cpu_instrs.gb --doctor 3000 > trace.log
```

Two things must be true for a trace to line up against the reference logs:

1. **LY must be stubbed to `$90`.** The golden logs were captured that way.
   `--doctor` sets this via `PPU::setLyStub` — see [ppu.md](ppu.md).
2. **Serial echo must be off**, so ROM output does not interleave into stdout.
   `--doctor` handles this too.

`PCMEM` reads go through the MMU, so they observe live I/O state. Reading four
bytes near an I/O register in a trace line is a read the emulator would not
otherwise perform — harmless today, but worth remembering once registers gain
read side effects in Phase 4.

## Current state

Working. `--doctor` was used to verify the CPU entry sequence against Blargg's
expected `NOP; JP $0637; JP $0430; DI; LD SP,$DFFF` init.

## Not implemented yet

- **No log levels or categories** — this is a trace formatter, not a logging
  framework. Nothing else in the codebase logs through it; `Serial` and the
  illegal-opcode path write to stdout/stderr directly.
- **No file sink.** Callers redirect stdout.
- **No disassembly.** `PCMEM` is raw bytes; there is no mnemonic decoder.
- **No conditional/breakpoint tracing.** Every step is logged, which makes long
  runs produce very large files (3M instructions is roughly 200 MB).
