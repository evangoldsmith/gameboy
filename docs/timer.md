# Timer — DIV and TIMA

**Source:** `src/timer.h`, `src/timer.cpp`

> **Status: DIV only.** TIMA, TMA, and TAC are not implemented.

Owns the 16-bit internal system counter that drives both the DIV register and,
eventually, the configurable TIMA counter and its interrupt.

## Public API

```cpp
void    tick(uint8_t tcycles);
uint8_t div() const;      // $FF04
void    resetDiv();
```

Everything is inline in the header for now. `timer.cpp` exists as the home for
the Phase 4 TIMA logic.

## How it works

A single `uint16_t m_counter` increments once per T-cycle, so it rolls over at
the full 4.194 MHz system clock rate.

`div()` returns the **upper 8 bits** (`m_counter >> 8`), which is why DIV
increments at 16,384 Hz — once every 256 T-cycles.

`resetDiv()` zeroes the **entire 16-bit counter**, not just the visible byte.
This distinction is the source of several hardware quirks: because TIMA is
clocked by watching a specific bit of this counter, a DIV write can drop that
bit from 1 to 0 and trigger a spurious TIMA increment.

`MMU::writeIO` routes any write to `$FF04` to `resetDiv()` regardless of value.

## Current state

DIV works. It is read by test ROMs for RNG seeding, which is why it was needed
before the rest of the timer.

## Not implemented yet

All of roadmap Phase 4. This is the direct cause of the one failing
`cpu_instrs` sub-test (`02-interrupts`).

- **TIMA (`$FF05`), TMA (`$FF06`), TAC (`$FF07`)** — none exist. Writes land in
  the MMU's flat I/O array and reads return whatever was written.
- **The falling-edge mechanism.** TIMA increments on a falling edge of
  (selected counter bit AND TAC enable bit), where TAC bits 1–0 select bit 9, 3,
  5, or 7 — giving 4096, 262144, 65536, or 16384 Hz.
- **The 1 M-cycle overflow delay.** On overflow TIMA reads `$00` for one M-cycle
  before TMA is reloaded and the interrupt requested. Writing TIMA during that
  window cancels the overflow entirely.
- **Edge-case increments** from DIV writes, TAC clock-select changes, and
  disabling the timer while the monitored bit is set.
- **No Timer interrupt** is ever requested.
