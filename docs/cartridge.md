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

`ramSizeFromByte` is a lookup rather than a formula because the encoding is not
monotonic: `$05` is 64 KB but `$04` is 128 KB.

External RAM is allocated from the header value, so `m_ram` is empty for
cartridges that declare none.

## Banking

Reads split at `$4000`:

- `$0000–$3FFF` — always bank 0, indexed directly.
- `$4000–$7FFF` — `m_romBank % romBankCount()`, so a bank number past the end of
  a small ROM wraps instead of reading out of bounds.

`romByte()` is the single bounds-checked accessor; anything past the end of the
ROM reads `$FF`.

### MBC1 register writes

| Range | Effect |
|---|---|
| `$0000–$1FFF` | RAM enable — `$0A` in the low nibble enables, anything else disables |
| `$2000–$3FFF` | Low 5 bits of the ROM bank. **Writing 0 selects bank 1** |
| `$4000–$5FFF` | 2 bits, written to both the RAM bank and ROM bank bits 5–6 |
| `$6000–$7FFF` | Banking mode select — currently ignored |

The write-0-becomes-1 quirk is why MBC1 cannot reach banks `$20`, `$40`, and
`$60`: those bank numbers have zero in the low five bits, so they alias to `$21`,
`$41`, `$61`.

Cartridges reporting `MBCType::None` ignore all writes.

External RAM access is masked with `% m_ram.size()` so an out-of-range bank
wraps rather than reading past the buffer. Reads return `$FF` when RAM is
disabled or absent.

## Current state

Header parsing is complete for all five MBC types the enum knows about. Banking
is **minimal MBC1 only** — enough for Blargg's `cpu_instrs`, which is itself an
MBC1 ROM that switches banks to reach its 11 sub-tests.

Verified working: `roms/cpu_instrs.gb` (MBC1, 64 KB).

## Not implemented yet

Everything here is roadmap Phase 9:

- **MBC1 mode 1** (`$6000–$7FFF`), which reinterprets the 2-bit register as RAM
  banking rather than upper ROM bits.
- **MBC2, MBC3, MBC5.** `mbcTypeFromByte` identifies them but `write` treats them
  all as MBC1. `roms/pokemon_red.gb` is MBC3 and will not run correctly.
- **MBC3 RTC** — the latch sequence and the clock registers at `$A000–$BFFF`.
- **Battery-backed saves.** `m_ram` is never persisted to a `.sav` file, and the
  BATTERY flag in the header byte is not decoded.
- **`std::variant<NoMBC, MBC1, …>` dispatch** as the roadmap suggests; the
  current code is a single if-chain.
