# gameboy

DMG-01 Game Boy emulator in C++20.

Download your own roms and add the `*.gb` files into the roms/ directory.

Games with a battery-backed cartridge save to a `.sav` file next to the ROM.

## Boot ROM (optional)

Drop a 256-byte boot ROM at `roms/dmg_boot.bin` and it runs on startup — the
scrolling Nintendo logo and the chime. Without one the emulator jumps straight
into the game, which is the default.

Nintendo's original boot ROM is copyrighted. [Bootix](https://github.com/Hacktix/Bootix)
is an MIT-licensed drop-in reimplementation; SameBoy also ships open ones.

```bash
./build/debug/gameboy roms/tetris.gb --boot path/to/boot.bin
```

`make run` doesn't forward extra flags, so for that route put the file at
`roms/dmg_boot.bin` and it's picked up automatically.

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
