# gameboy

DMG-01 Game Boy emulator in C++20.

Passes all 11 of Blargg's `cpu_instrs` tests. Tetris is playable; Pokémon Red
boots to its title screen. No audio yet.

## Running

```bash
make run                        # defaults to roms/tetris.gb
make run ROM=path/to/game.gb
```

Needs SDL2 (`brew install sdl2` on macOS).

## Controls

| Key | Button |
|-----|--------|
| Arrow keys | D-pad |
| `Z` | A |
| `X` | B |
| `Enter` | Start |
| `Backspace` | Select |
| `Escape` | Quit |

## Build

```bash
make            # debug (default)
make release    # optimised
make clean
```

Binaries land in `build/debug/` and `build/release/`.

## Debugging

`--doctor` runs headless and prints a
[Game Boy Doctor](https://github.com/robert/gameboy-doctor) trace line per
instruction. Test ROM output goes to stderr, so the trace can be redirected
away:

```bash
./build/release/gameboy roms/cpu_instrs.gb --doctor > trace.log
```

## Documentation

[`docs/`](docs/) has one file per component covering what it owns, how the logic
works, and what is not implemented yet. Start with [`docs/README.md`](docs/README.md).
