# Main — SDL2 frontend

**Source:** `src/main.cpp`

Entry point. Parses arguments, constructs a `GameBoy`, and runs it in one of two
modes. This is the only translation unit that links SDL2 — `gameboy_core` has no
SDL dependency, which is what makes headless test runs possible.

## Usage

```
gameboy <rom.gb> [--boot <boot.bin>] [--doctor [steps]]
```

`--boot` supplies a 256-byte boot ROM. Without the flag the frontend looks for
`roms/dmg_boot.bin` then `roms/boot.bin`, so `make run` picks one up
automatically; with neither present the emulator jumps straight to `$0100` as
before. The header dump reports which was used.

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
streaming `ARGB8888` texture at native resolution. `GB_WIDTH`/`GB_HEIGHT` come
from `PPU::WIDTH`/`PPU::HEIGHT` rather than being duplicated here.

The renderer requests `SDL_RENDERER_PRESENTVSYNC`, which paces the loop at the
display's refresh rate. Close enough to the DMG's 59.7 Hz for now, and it stops
the loop spinning at whatever speed the host manages.

`SDL_RenderSetLogicalSize` handles scaling, and `SDL_HINT_RENDER_SCALE_QUALITY`
is `"0"` for nearest-neighbour — bilinear filtering on a 160×144 source looks
wrong.

Each iteration polls events, calls `gb.runFrame()`, uploads the PPU framebuffer
with `SDL_UpdateTexture`, then clears and presents.

### Audio

`openAudio()` opens a device matching the APU's format (48 kHz, signed 16-bit,
stereo). If it fails the emulator still runs, just silently.

Output uses **`SDL_QueueAudio` rather than an audio callback**. A callback runs
on SDL's audio thread and would need a lock-free ring buffer between it and the
emulation thread; queueing is thread-safe on its own, so the frontend can simply
push whatever the APU produced after each `runFrame()`.

Every sample is queued — nothing is dropped, because the pacing below stops the
queue from running away in the first place.

### Frame pacing

**Vsync cannot pace emulation.** The Game Boy runs at 59.73 Hz, so on a 120 Hz
display a vsync-driven loop runs the emulator at almost exactly *double* speed.
That was a real bug: music played twice as fast, and the APU then produced
about 96000 samples a second against the device's 48000, overrunning the queue.
Dropping the excess is what turned the overrun into audible distortion.

The audio device is the reliable clock. It consumes exactly `SAMPLE_RATE`
samples per second no matter what the display is doing, so the loop gates on
queue depth:

```cpp
while (SDL_GetQueuedAudioSize(audio) > targetQueuedBytes) SDL_Delay(1);
```

`targetQueuedBytes` is four frames' worth, about 67 ms of latency. Measured over
15 seconds this holds **1.003×** real speed with the queue stable at the target
— roughly 5 cents of pitch error, inaudible.

Vsync stays enabled. It no longer sets the pace, it only quantises *when* a
finished frame is shown, which avoids tearing without affecting emulation speed.

When no audio device could be opened there is a wall-clock fallback using
`SDL_GetPerformanceCounter`, since vsync alone would otherwise run at double
speed again.

### Saves

`gb.flushSave()` runs every 120 frames and once more on exit, so a crash or a
kill costs at most a couple of seconds rather than the whole run. It is a no-op
unless the cartridge is battery-backed and RAM has actually changed — see
[cartridge.md](cartridge.md).

Key events are handled on both `SDL_KEYDOWN` and `SDL_KEYUP`, mapped through
`buttonForKey()` into `Joypad::setButton()` — arrows for the D-pad, `Z`/`X` for
A/B, `Enter`/`Backspace` for Start/Select, `Escape` to quit. See
[joypad.md](joypad.md). Cleanup tears down the texture, renderer, and window in
reverse order of creation.

The framebuffer is `std::array<uint32_t, 160*144>` in ARGB8888, which matches
the texture format exactly — the upload is a straight memcpy with no conversion.

## Error handling

`GameBoy`'s constructor throws on a bad ROM path or a file too short to hold a
header. `main` wraps construction and both run modes in a single try/catch that
reports to stderr and returns `EXIT_FAILURE`.

## Current state

Both modes work. `--doctor` is the thoroughly verified path.

**The SDL path is built and wired but still has not been exercised
interactively.** Rendering was verified headlessly instead, by linking
`gameboy_core` into a harness that runs frames and dumps the framebuffer to a
BMP — the `cpu_instrs` results screen renders correctly. The only untested delta
is the `SDL_UpdateTexture` call itself. Run `make run` to confirm.

## Not implemented yet

- **Pacing granularity is `SDL_Delay(1)`**, which is why the measured rate is
  1.003× rather than exactly 1.000×.
- **Key bindings are compile-time constants**, and there is no gamepad support.
- **No fast-forward, pause or frame-advance.** Phase 14.
- **No configuration** — scale, key bindings, and palette are all compile-time
  constants.
