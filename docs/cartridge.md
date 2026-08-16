# Cartridge — ROM loading and banking

**Source:** `src/cartridge/cartridge.h`, `src/cartridge/cartridge.cpp`

Loads a `.gb` file, parses its header, and serves reads and writes for
`$0000–$7FFF` (ROM) and `$A000–$BFFF` (external RAM). Owns the memory bank
controller state.

## Public API

```cpp
static Cartridge load(const std::string& path);   // throws std::runtime_error

uint8_t read(uint16_t addr) const;
void    write(uint16_t addr, uint8_t val);

const CartridgeHeader& header() const;

bool flushSave();                       // persist cartridge RAM if dirty
const std::string& savePath() const;
```

The constructor is private; `load` is the only way to build one. It returns by
value and relies on the implicit move constructor, so `GameBoy` can initialise
its member with `m_cart(Cartridge::load(path))`.

## Header parsing

`load` reads the whole file into `m_rom`, rejects anything smaller than `$150`
(too small to hold a header), then decodes:

| Offset | Field | Decoder |
|---|---|---|
| `$0134–$0143` | Title, up to 16 bytes, NUL-terminated | inline |
| `$0147` | Cartridge type → `MBCType` | `mbcTypeFromByte` |
| `$0148` | ROM size | `romSizeFromByte` — `32 KB << b` |
| `$0149` | RAM size | `ramSizeFromByte` — table lookup |
| `$0147` | Battery present | `batteryFromByte` — see below |

`ramSizeFromByte` is a lookup rather than a formula because the encoding is not
monotonic: `$05` is 64 KB but `$04` is 128 KB.

MBC2 is special-cased at load: its 512 half-bytes of RAM live inside the mapper
chip, so the header reports none and the buffer is allocated anyway.

## Reading

Two bank-resolution helpers keep `read()` short:

- **`lowBank()`** — the bank behind `$0000–$3FFF`. Always 0 *except* in MBC1's
  alternate mode, where the secondary register shifts up into the high bits and
  exposes banks `$20`/`$40`/`$60` that are otherwise unreachable.
- **`highBank()`** — the bank behind `$4000–$7FFF`.

Both take `% romBankCount()`, so a bank number past the end of a small ROM wraps
instead of reading out of bounds. `romByte()` is the single bounds-checked
accessor; anything past the end of the ROM reads `$FF`.

## Mapper differences

`write()` routes to a per-mapper handler. The differences between them are
small but not interchangeable — treating one as another is exactly the bug
described at the bottom of this page.

### MBC1

| Range | Effect |
|---|---|
| `$0000–$1FFF` | RAM enable (`$0A` in the low nibble) |
| `$2000–$3FFF` | Low **5 bits** of the ROM bank; writing 0 selects 1 |
| `$4000–$5FFF` | 2 bits: upper ROM bits *or* RAM bank, depending on mode |
| `$6000–$7FFF` | Mode select |

The write-0-becomes-1 quirk is why MBC1 cannot reach banks `$20`, `$40` and
`$60` in the default mode: their low five bits are zero, so they alias to `$21`,
`$41`, `$61`. Mode 1 is the workaround, remapping the low ROM window instead.

### MBC2

The smallest mapper, and the only one where **bit 8 of the address** picks the
register rather than the address range: `$0000–$3FFF` with bit 8 clear is RAM
enable, with bit 8 set is a 4-bit ROM bank.

Its RAM is 512 half-bytes mirrored across the whole window. Writes keep only the
low nibble; reads OR in `$F0`, because the upper nibble is not wired up.

### MBC3

| Range | Effect |
|---|---|
| `$0000–$1FFF` | RAM and clock enable |
| `$2000–$3FFF` | Full **7-bit** ROM bank in one register; writing 0 selects 1 |
| `$4000–$5FFF` | `$00–$03` RAM bank, `$08–$0C` select a clock register |
| `$6000–$7FFF` | Latch clock data |

Two things distinguish it from MBC1, and both matter:

- The bank number is **7 bits in a single register** — nothing is borrowed from
  the secondary register.
- `$4000–$5FFF` selects a RAM bank and **must not touch the ROM bank**.

Writing `$08–$0C` maps a clock register over the RAM window instead of a RAM
bank. The latch sequence (write `$00`, then `$01`) copies the running clock into
a separate latched copy that reads observe, so a multi-byte read cannot straddle
a tick.

### MBC5

| Range | Effect |
|---|---|
| `$0000–$1FFF` | RAM enable |
| `$2000–$2FFF` | Low 8 bits of the ROM bank |
| `$3000–$3FFF` | Bit 8 of the ROM bank |
| `$4000–$5FFF` | 4-bit RAM bank |

9 bits of bank number across two registers, and **writing 0 genuinely selects
bank 0** — the MBC1/MBC3 quirk is gone.

## Battery-backed saves

On a real cartridge the battery is not a feature — it is a coin cell soldered to
the board, wired to the SRAM chip's power pin. SRAM is volatile, so without it
the chip loses everything when the console is switched off.

The important consequence for emulation: **the game does nothing special to
save.** Writing your party to `$A000–$BFFF` is an ordinary RAM write,
indistinguishable from a scratch write it intends to discard. The software
cannot tell whether a battery is fitted. Persistence is a property of the power
supply, not a behaviour.

So all there is to emulate is "did those bytes survive to the next run", which
reduces to a flag on the cartridge type and a file:

- `batteryFromByte()` decodes `$0147`. Pokémon Red is `$13`, MBC3+RAM+BATTERY.
- The save path is the ROM path with its extension replaced by `.sav`.
- `loadSave()` runs at the end of `load()`. A missing file is normal, not an
  error. A size mismatch warns and reads what fits, since that usually just
  means the file came from another emulator with different padding.
- `flushSave()` writes the whole RAM buffer, but only when `m_ramDirty` is set —
  so calling it every couple of seconds costs nothing while the game is not
  touching SRAM.

`m_ramDirty` is set by `writeRam()`, the single place cartridge RAM changes.

MBC2's in-chip RAM persists the same way; `$06` is MBC2+BATTERY.

## The bug this replaced

Worth recording, because the symptom pointed somewhere else entirely.

Pokémon Red is MBC3 with 1 MB of ROM (64 banks) and 32 KB of save RAM (4 banks).
It was originally running on the MBC1 code path, which did:

```cpp
m_romBank = (m_romBank & 0x1F) | (bits << 5);   // $4000-$5FFF
```

On MBC1 that register holds upper ROM bits. On MBC3 it is the RAM bank. So when
the game selected **RAM bank 1**, the emulator added **32 to the ROM bank
number** and execution continued in the wrong bank — producing
`Illegal opcode $F4 at $60B6`, an address inside the switchable window.

The obvious suspect was the 5-bit mask truncating banks above 31, but
instrumenting the writes showed the game never requested a bank above `$1F`
during the intro. A single RAM-bank write was enough.

## Current state

MBC1, MBC2, MBC3 and MBC5 are implemented, along with external RAM banking,
battery-backed saves, and the MBC3 clock registers.

Verified: `cpu_instrs` (MBC1) passes 11/11, Tetris (no MBC) is playable, and
Pokémon Red (MBC3) reaches Prof. Oak's intro and the name entry screen.

Saves were checked by round-trip: writing patterns to two different RAM banks,
flushing, and reading them back from a fresh `GameBoy` instance. A non-battery
ROM correctly writes no file, and Pokémon Red produces a 32 KB `.sav` during
normal play.

## Not implemented yet

- **The RTC does not tick.** MBC3's clock registers store and latch correctly,
  but nothing advances them, so a game reading the clock sees a stopped one.
  Pokémon Red is `MBC3+RAM+BATTERY` with no timer, so it is unaffected; Gold and
  Silver would not be. The RTC is also not written to the `.sav` file, which
  real cartridges do persist.
- **MBC2 is untested** — no MBC2 ROM is available to check it against.
- **No `std::variant` dispatch.** `roadmap.md` suggests modelling the mapper as
  `std::variant<NoMBC, MBC1, …>` with `std::visit`; this is a switch over an
  enum instead.
