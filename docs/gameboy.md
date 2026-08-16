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
bool flushSave();     // persist cartridge RAM; no-op unless battery-backed and dirty

const CPU& cpu() const;
MMU&       mmu();
Serial&    serial();
PPU&       ppu();
Joypad&    joypad();
APU&       apu();

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
APU        m_apu
MMU        m_mmu       // needs cart, serial, timer, ppu, joypad, apu
CPU        m_cpu       // needs mmu
```

Reordering these silently binds references to not-yet-constructed objects, so
the order is load-bearing.

`m_cart` is initialised with `Cartridge::load(romPath)`, which returns by value
and moves. `Cartridge`'s constructor is private, but its implicit move
constructor is public, so this works.

## `step()`

```cpp
uint8_t GameBoy::step() { return m_cpu.step(); }
```

That is the whole thing. Peripherals are **not** advanced here: the CPU ticks
them one M-cycle at a time from inside each instruction, through `MMU::tick()`.
Doing it here instead — in a lump after the instruction finished — is what used
to make `mem_timing` fail. See [mmu.md](mmu.md) and [cpu.md](cpu.md).

Peripherals keep running while the CPU is halted, because `CPU::step()` ticks 4
cycles per halted step rather than returning zero. That is what eventually
produces the interrupt that wakes it.

## Interrupt routing

Interrupts are raised in `MMU::tick()`, again so that one raised mid-instruction
is visible to a later access within that same instruction.

Peripherals do not request interrupts themselves. Each exposes a read-and-clear
`takeIrq()`-style method that the MMU drains, which keeps dependencies
one-directional — no peripheral needs an MMU reference, so there is no cycle in
the object graph.

## `runFrame()`

Accumulates `step()` results until `TCYCLES_PER_FRAME` is reached, bailing early
if the CPU has stopped on an illegal opcode. It overshoots by up to 23 cycles
depending on where the last instruction lands; the remainder is not carried into
the next frame yet.

## Current state

Wiring is complete for the components that exist.

## Not implemented yet

- **Sound channels 3 and 4** are missing from the APU — see [apu.md](apu.md).
- **No frame-time pacing in the core.** `runFrame()` runs as fast as the host
  allows; the frontend paces itself on the audio queue — see [main.md](main.md).
  That is what makes headless harnesses run at full speed.
- **No save states.** Phase 14.
- **Cycle remainder is dropped** between frames rather than carried.
