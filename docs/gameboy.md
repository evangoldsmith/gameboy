# GameBoy — top-level orchestrator

**Source:** `src/gameboy.h`, `src/gameboy.cpp`

Owns every subsystem instance and drives the emulation loop. This is the whole
public surface of `gameboy_core` — the SDL frontend talks to this and nothing
else.

## Public API

```cpp
explicit GameBoy(const std::string& romPath);   // throws std::runtime_error

uint8_t step();       // one instruction + peripheral catch-up; returns T-cycles
void    runFrame();   // TCYCLES_PER_FRAME worth of steps

const CartridgeHeader& header() const;

const CPU& cpu() const;
MMU&       mmu();
Serial&    serial();
PPU&       ppu();
Joypad&    joypad();

static constexpr uint32_t TCYCLES_PER_FRAME = 70224;   // 154 lines x 456 dots
```

## Ownership and initialisation order

Members are declared in dependency order, because C++ initialises them in
declaration order regardless of what the initialiser list says:

```
Cartridge  m_cart      // no dependencies
Serial     m_serial
Timer      m_timer
PPU        m_ppu
Joypad     m_joypad
MMU        m_mmu       // needs cart, serial, timer, ppu, joypad
CPU        m_cpu       // needs mmu
```

Reordering these silently binds references to not-yet-constructed objects, so
the order is load-bearing.

`m_cart` is initialised with `Cartridge::load(romPath)`, which returns by value
and moves. `Cartridge`'s constructor is private, but its implicit move
constructor is public, so this works.

## `step()`

The core interleaving:

1. `m_cpu.step()` runs one instruction (or services one interrupt) and returns
   its T-cycle cost.
2. `m_timer.tick()` and `m_ppu.tick()` advance by exactly that many cycles.
3. Pending peripheral interrupts are drained into IF.

Peripherals are advanced **after** the CPU, in a lump equal to the instruction's
full cost. Real hardware interleaves them cycle by cycle. This is invisible to
`cpu_instrs` and to games, but it is the reason `mem_timing` and the Mealybug
tests cannot pass — see [cpu.md](cpu.md) for the same caveat on the CPU side.

Because peripherals are ticked from the returned cycle count, they keep running
while the CPU is halted: `CPU::step()` returns 4 cycles per halted step rather
than zero, so the PPU still reaches VBlank and wakes it.

## Interrupt routing

`requestInterrupt(Interrupt)` sets the matching bit in IF (`$FF0F`) via the MMU.
The `Interrupt` enum in `types.h` doubles as the bit index, so the mask is
`1 << static_cast<uint8_t>(which)`.

Peripherals do not request interrupts themselves. Each exposes a read-and-clear
`takeIrq()`-style method that `step()` drains:

```cpp
if (m_ppu.takeVBlankIrq()) requestInterrupt(Interrupt::VBlank);
if (m_ppu.takeStatIrq())   requestInterrupt(Interrupt::LCDStat);
if (m_timer.takeIrq())     requestInterrupt(Interrupt::Timer);
if (m_serial.takeIrq())    requestInterrupt(Interrupt::Serial);
if (m_joypad.takeIrq())    requestInterrupt(Interrupt::Joypad);
```

This keeps dependencies one-directional — no peripheral needs an MMU reference,
so there is no cycle in the object graph.

## `runFrame()`

Accumulates `step()` results until `TCYCLES_PER_FRAME` is reached, bailing early
if the CPU has stopped on an illegal opcode. It overshoots by up to 23 cycles
depending on where the last instruction lands; the remainder is not carried into
the next frame yet.

## Current state

Wiring is complete for the components that exist.

## Not implemented yet

- **No APU member** — that component is still a stub.
- **No frame-time pacing in the core.** `runFrame()` runs as fast as the host
  allows; the frontend leans on SDL vsync instead. Real pacing is Phase 14.
- **No save states.** Phase 14.
- **Cycle remainder is dropped** between frames rather than carried.
