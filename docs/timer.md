# Timer — DIV and TIMA

**Source:** `src/timer.h`, `src/timer.cpp`

Owns the 16-bit internal system counter that drives both the DIV register and
the configurable TIMA counter, and raises the Timer interrupt on TIMA overflow.

## Public API

```cpp
void tick(uint8_t tcycles);       // expects a multiple of 4

uint8_t div()  const;             // $FF04
uint8_t tima() const;             // $FF05
uint8_t tma()  const;             // $FF06
uint8_t tac()  const;             // $FF07

void resetDiv();
void writeTima(uint8_t val);
void writeTma(uint8_t val);
void writeTac(uint8_t val);

bool takeIrq();                   // consumes the flag
```

## The system counter

A single `uint16_t m_counter` increments once per T-cycle, so it runs at the
full 4.194 MHz system clock.

`div()` returns its **upper 8 bits**, which is why DIV increments at 16,384 Hz —
once every 256 T-cycles.

`resetDiv()` zeroes the **entire 16-bit counter**, not just the visible byte.
`MMU::writeIO` routes any write to `$FF04` here regardless of value.

## How TIMA is clocked

TIMA is not counted directly. The hardware watches one bit of the system
counter, ANDs it with TAC's enable bit, and increments TIMA when that combined
signal **falls from 1 to 0**. That is `timerBit()`:

```cpp
bool Timer::timerBit(uint16_t counter) const {
    if ((m_tac & TAC_ENABLE) == 0) return false;
    return ((counter >> kClockBit[m_tac & 0x03]) & 1u) != 0;
}
```

TAC bits 1–0 select which bit, and therefore the rate:

| TAC 1–0 | Counter bit | Falls every | Frequency |
|---|---|---|---|
| `00` | 9 | 1024 T-cycles | 4096 Hz |
| `01` | 3 | 16 T-cycles | 262144 Hz |
| `10` | 5 | 64 T-cycles | 65536 Hz |
| `11` | 7 | 256 T-cycles | 16384 Hz |

TAC bit 2 enables the timer. Bits 3–7 do not exist and read as 1, so `tac()`
ORs in `0xF8`.

Every counter assignment goes through `setCounter()`, which samples
`timerBit()` before and after and calls `incTima()` on a falling edge. Nothing
writes `m_counter` directly — that single funnel is what makes the quirks below
fall out automatically rather than needing special cases.

## Why the quirks exist

Because TIMA is edge-triggered on a bit of a shared counter, three things that
look unrelated all produce increments:

- **Writing to DIV.** `resetDiv()` is `setCounter(0)`. If the watched bit was
  set, zeroing the counter *is* a falling edge, so TIMA ticks.
- **Changing TAC's clock select.** Switching from a set bit to a clear one drops
  the signal, which reads as an edge. `writeTac()` samples around the
  assignment for exactly this reason.
- **Disabling the timer** while the watched bit is set. Clearing the enable bit
  forces `timerBit()` to false — another edge.

None of these are special-cased. They are consequences of routing every change
through the same before/after comparison.

## Overflow and the reload delay

TIMA overflow does **not** reload immediately. `incTima()` sets TIMA to `$00`
and starts a 4 T-cycle (1 M-cycle) countdown:

```
cycle A:  TIMA reads $00, no interrupt yet, m_reloadDelay = 4
cycle B:  TIMA = TMA, IF bit 2 set
```

`tick()` decrements the delay at the top of each M-cycle step, before advancing
the counter, so a reload started in one step lands on the next.

**Writing TIMA during the delay cancels the overflow entirely** — no TMA reload
and no interrupt. That is `writeTima()` clearing `m_reloadDelay` before storing
the value.

## Step granularity

`tick()` advances the counter one M-cycle at a time rather than one T-cycle at a
time. This is safe because the fastest watched bit (3) holds each state for 8
T-cycles, so a 4-cycle step can never skip a transition — and it is 4× cheaper
than a per-T-cycle loop.

The loop condition is `done + 4 <= tcycles`, which assumes the input is a
multiple of 4. `CPU::step()` always returns one, including the 20-cycle
interrupt dispatch.

## Interrupt routing

`takeIrq()` is read-and-clear, drained by `GameBoy::step()` into IF bit 2. The
timer has no MMU reference, matching the pattern the PPU and serial port use —
see [gameboy.md](gameboy.md).

## Current state

Complete for roadmap Phase 4. **All 11 Blargg `cpu_instrs` sub-tests pass**,
including `02-interrupts`, whose sub-test 4 sets `TAC=$05`, zeroes TIMA and IF,
and asserts that the timer interrupt fires within a specific window — checking
both that it does not fire too early and that it does fire in time.

## Not implemented yet

- **`instr_timing` and the Mooneye timer tests have not been run.** Neither ROM
  is in `roms/`, so the edge cases above are implemented to spec but only
  verified as far as `cpu_instrs` exercises them.
- **Sub-M-cycle write timing.** The distinction between writing TIMA during the
  overflow cycle versus the reload cycle collapses into one window at M-cycle
  granularity. Separating them needs the T-cycle-accurate memory access model
  described in [cpu.md](cpu.md).
- **DIV is not reset on power-up state changes** — there is no STOP handling, so
  the counter keeps running through a STOP instruction.
