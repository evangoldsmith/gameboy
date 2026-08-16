# Serial — link cable port

**Source:** `src/serial.h`, `src/serial.cpp`

Implements `$FF01` (SB) and `$FF02` (SC). No device is connected, so transfers
complete instantly and shift in `$FF`.

Its real job right now is **test harness output**: Blargg's suites report
pass/fail through the serial port long before they draw anything, so this is how
the emulator gets objective feedback without a working PPU.

## Public API

```cpp
uint8_t readSB() const;
uint8_t readSC() const;
void    writeSB(uint8_t val);
void    writeSC(uint8_t val);

const std::string& output() const;   // everything printed so far
bool takeIrq();                      // consumes the flag
void setEcho(bool on);               // mirror to stdout, default true
```

## How it works

`writeSB` just stores the byte.

`writeSC` is where everything happens. Bit 7 starts a transfer and bit 0
selects the clock source. **Both must be set** for anything to happen:

```cpp
if ((val & 0x81) == 0x81) { ... }
```

With no cable attached, only an *internally* clocked transfer can complete — the
console drives the clock itself and shifts in `$FF`. An **external**-clock
transfer waits for a partner to supply the clock, so with nothing connected it
stays pending forever and never raises an interrupt.

Checking only bit 7 makes every transfer succeed, which looks to the ROM like a
phantom partner replying. That is enough to strand a game: Tetris polls the link
port with the external clock on its title screen, and completing those transfers
kept it in link negotiation instead of starting a one-player game.

When both bits are set:

1. The current SB byte is appended to `m_output`.
2. If echo is on, it is written to stdout and flushed immediately — unbuffered,
   so output survives a hang or a crash.
3. SB is set to `$FF`, the value shifted in from an absent device.
4. Bit 7 of SC is cleared to mark the transfer complete.
5. `m_irq` is set.

Real hardware takes 8 serial clocks at 8192 Hz to shift a byte out. Completing
instantly is a simplification that no test ROM notices, because they all poll
for completion rather than counting cycles.

`readSC` ORs in `$7E` — bits 1–6 do not exist and always read as 1.

`takeIrq()` is read-and-clear, consumed by `GameBoy::step()` and turned into an
IF bit. Serial has no MMU reference, keeping the dependency one-directional.

## Echo control

`--doctor` mode turns echo off (`main.cpp`) so that ROM output does not
interleave with trace lines on stdout. The captured string is printed to stderr
when the run ends.

## Current state

Output capture works. Verified against `roms/cpu_instrs.gb`, which prints its
banner and per-sub-test results this way. External-clock transfers correctly
hang pending, verified against Tetris.

## Not implemented yet

- **No transfer timing.** Internally clocked bytes complete in zero cycles
  instead of taking 8 shifts at 8192 Hz. Roadmap Phase 13.
- **No link cable emulation** — nothing to connect to, so an external-clock
  transfer never completes rather than ever receiving real data.
- **Input is always `$FF`.** A ROM that expects a real partner sees a
  disconnected cable.
