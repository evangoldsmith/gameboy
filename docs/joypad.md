# Joypad — button input

**Source:** `src/joypad.h`, `src/joypad.cpp`

> **Status: stub.** `joypad.h` is an empty include guard and `joypad.cpp`
> contains only its own `#include`. There is no `Joypad` class. `GameBoy` has no
> joypad member, and `main.cpp` handles no keys except Escape and window close.
>
> Nothing below is implemented — this documents the intended design so the file
> is ready when Phase 8 starts.

## Planned responsibility

Own the state of the eight buttons and serve register `$FF00`, which is
currently an inert slot in the MMU's flat I/O array.

## How `$FF00` works

A 2×4 button matrix. The CPU selects which row to read by writing bits 4–5, then
reads the result back from bits 0–3 of the same register.

| Bit | Meaning |
|---|---|
| 5 | Select action buttons (**0** = selected) |
| 4 | Select direction buttons (**0** = selected) |
| 3 | Down / Start |
| 2 | Up / Select |
| 1 | Left / B |
| 0 | Right / A |

Both the row selects and the button bits are **active low** — a zero means
selected or pressed. Bits 6–7 are unused and read as 1.

If both rows are selected the results are ANDed together; if neither is, all
four low bits read as 1.

## Planned integration

Following the pattern the other peripherals use (see [gameboy.md](gameboy.md)):

- `MMU::readIO`/`writeIO` gain a `$FF00` case routing to the joypad, alongside
  the existing SB/SC/DIV/STAT/LY cases.
- The frontend translates SDL key events into a button state update.
- A `takeIrq()`-style read-and-clear method lets `GameBoy::step()` raise
  `Interrupt::Joypad` without the joypad needing an MMU reference.

The Joypad interrupt (`$0060`) fires on a high-to-low transition of any selected
button line. Few games use it — its main purpose is waking from STOP mode — so
correct register reads matter far more than the interrupt.

## Reference

Roadmap Phase 8. Its milestone is Tetris being fully playable, which is the
first point at which the emulator is a *thing you can use* rather than a thing
that passes tests.
