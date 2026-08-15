# Main — SDL2 frontend

**Source:** `src/main.cpp`

Entry point. Parses arguments, constructs a `GameBoy`, and runs it in one of two
modes. This is the only translation unit that links SDL2 — `gameboy_core` has no
SDL dependency, which is what makes headless test runs possible.

## Usage

```
gameboy <rom.gb> [--doctor [steps]]
```

| Mode | Behaviour |
|---|---|
| default | Opens an SDL window and runs the emulator frame by frame |
| `--doctor [steps]` | Headless; prints a trace line per instruction to stdout |

`--doctor` takes an optional step count, defaulting to 1,000,000. Argument
parsing peeks at the next argv entry and consumes it only if it parses as a
positive integer, so `--doctor` alone is valid.

## `runDoctor()`

1. `ppu().setLyStub(true)` — the reference logs need LY pinned to `$90`.
2. `serial().setEcho(false)` — keep ROM output out of the trace stream.
3. Loop: print `logger::traceLine(...)`, then `gb.step()`, until the step limit
   or `cpu().stopped()`.
4. Print captured serial output to **stderr** so it stays separate from the
   trace on stdout.

The two-stream split means this works:

```bash
./build/release/gameboy roms/cpu_instrs.gb --doctor 250000000 > /dev/null
```

The trace is discarded, and only the test results reach the terminal.

> **Note:** in zsh, `2>&1 >/dev/null` does *not* isolate stderr the way it does
> in bash — `MULTIOS` sends stdout to both destinations. Redirect stdout to a
> file or `/dev/null` directly, as above.

## `runSDL()`

Standard SDL setup: window at `160×144 × SCALE`, accelerated renderer, and a
streaming `ARGB8888` texture at native resolution.

`SDL_RenderSetLogicalSize` handles scaling, and `SDL_HINT_RENDER_SCALE_QUALITY`
is `"0"` for nearest-neighbour — bilinear filtering on a 160×144 source looks
wrong.

Each iteration polls events (quit on window close or Escape), calls
`gb.runFrame()`, then clears to DMG green and presents. Cleanup tears down the
texture, renderer, and window in reverse order of creation.

## Error handling

`GameBoy`'s constructor throws on a bad ROM path or a file too short to hold a
header. `main` wraps construction and both run modes in a single try/catch that
reports to stderr and returns `EXIT_FAILURE`.

## Current state

Both modes work. `--doctor` is the verified path — it was used to run the full
`cpu_instrs` suite.

**The SDL path is built and wired but has not been exercised interactively.** It
will show a blank green window, since nothing renders until Phase 6.

## Not implemented yet

- **No framebuffer upload.** The `SDL_UpdateTexture` call is commented out
  pending a PPU framebuffer; the texture is created but never written, so
  `SDL_RenderCopy` draws undefined contents over the green clear.
- **No frame pacing.** The loop runs as fast as the host allows instead of
  throttling to 59.7 Hz. There is no vsync flag on the renderer.
- **No input handling** beyond quit — see [joypad.md](joypad.md).
- **No audio.** `SDL_INIT_VIDEO` only; no audio device is opened.
- **No configuration** — scale, key bindings, and palette are all compile-time
  constants.
