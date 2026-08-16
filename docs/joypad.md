# Joypad — button input

**Source:** `src/joypad.h`, `src/joypad.cpp`

Owns the state of the eight buttons and serves register `$FF00`. Raises the
Joypad interrupt.

## Public API

```cpp
enum class Button : uint8_t {
    Right, Left, Up, Down,   // direction row
    A, B, Select, Start      // action row
};

void setButton(Button b, bool pressed);

uint8_t read() const;        // $FF00
void    write(uint8_t val);  // only bits 4-5 are writable

bool takeIrq();
```

## Everything here is active low

This is the single thing to keep straight: **0 means selected or pressed.** Get
it backwards and every button reads as permanently held.

That is not a hypothetical — before this component existed, `$FF00` was an inert
slot in the MMU's flat I/O array that returned whatever the game last wrote.
Its low nibble came back `$0`, so Tetris saw all eight buttons held from boot,
took a path that disabled the LCD, and deadlocked in a wait-for-VBlank loop
before drawing anything.

## How `$FF00` works

A 2×4 matrix. The CPU writes bits 4–5 to pick a row, then reads bits 0–3 back
from the same register.

| Bit | Meaning |
|---|---|
| 7–6 | Unused, always read 1 |
| 5 | Select action buttons (**0** = selected) |
| 4 | Select direction buttons (**0** = selected) |
| 3 | Down / Start |
| 2 | Up / Select |
| 1 | Left / B |
| 0 | Right / A |

State is kept as two nibbles, `m_dirs` and `m_actions`, where **1 means
pressed** — the inverse of the wire format. `lines()` does the conversion:

```cpp
uint8_t out = 0x0F;                       // nothing pressed
if ((m_select & SELECT_DIRS) == 0)    out &= ~m_dirs;
if ((m_select & SELECT_ACTIONS) == 0) out &= ~m_actions;
```

Storing the intuitive polarity and inverting once, at the boundary, keeps the
confusing part in one place.

Two behaviours fall out of this rather than needing special cases:

- **Both rows selected** → the results are ANDed, because each branch clears
  bits from the same value.
- **Neither row selected** → all four lines stay high, i.e. nothing pressed.

`setButton` maps the enum to a bit with `1 << (idx & 3)` and picks the row from
`idx < 4`, which is why the enum's declaration order is load-bearing.

## The interrupt

Fires on a **high-to-low transition** of any output line, so on press but not
release. `updateIrq()` compares against the previous line state:

```cpp
if ((m_lastLines & ~now & 0x0F) != 0) m_irq = true;
```

It runs from both `setButton()` and `write()` — changing the selected row can
pull a line low by bringing an already-held button into view, which counts as a
real edge exactly like a fresh press.

`takeIrq()` is read-and-clear, drained by `GameBoy::step()`. The joypad holds no
MMU reference, matching the pattern the PPU, timer and serial port use — see
[gameboy.md](gameboy.md).

Few games use this interrupt; its main purpose is waking from STOP. Correct
register reads matter far more.

## Frontend mapping

`main.cpp` translates SDL key events on both `SDL_KEYDOWN` and `SDL_KEYUP`:

| Key | Button |
|---|---|
| Arrow keys | D-pad |
| `Z` | A |
| `X` | B |
| `Enter` | Start |
| `Backspace` | Select |
| `Escape` | quit |

## Current state

Complete for roadmap Phase 8, whose milestone is Tetris being playable with
keyboard input. Verified headlessly: driving `Start` through the menus reaches
gameplay, and `Left` moves the falling piece.

## Not implemented yet

- **No gamepad support** — keyboard only, no `SDL_GameController`.
- **Key bindings are compile-time constants** in `buttonForKey()`.
- **No STOP interaction.** The CPU does not implement STOP properly, so the
  wake-from-STOP behaviour this interrupt exists for is untested.
- **No Mooneye joypad tests** — not in `roms/`.
