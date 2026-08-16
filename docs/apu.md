# APU — audio processing unit

**Source:** `src/apu/apu.h`, `src/apu/apu.cpp`, `src/apu/channels.h`, `src/apu/channels.cpp`

Owns the sound registers (`$FF10–$FF3F`), drives the channels from the frame
sequencer, and resamples their combined output down to a normal audio rate. Has
no SDL dependency — the frontend drains `samples()` once a frame.

**Channels 1 and 2 (pulse) are implemented. Channels 3 (wave) and 4 (noise) are
not** — that is roadmap Phase 11.

## Structure

The channel logic lives in `channels.h`/`channels.cpp` as standalone types that
know nothing about the Game Boy address space; `APU` is a thin register-decode
layer over them.

That split is deliberate. The GBA's four legacy PSG channels are this same
hardware at different addresses, so keeping `PulseChannel` and `FrameSequencer`
free of address decoding is what makes them reusable if a GBA core ever happens
— see the appendix in `roadmap.md`.

## Public API

```cpp
static constexpr int SAMPLE_RATE = 48000;
static constexpr int CHANNELS    = 2;     // interleaved stereo

void tick(uint8_t tcycles);

uint8_t readReg(uint16_t addr) const;
void    writeReg(uint16_t addr, uint8_t val);

const std::vector<int16_t>& samples() const;   // interleaved, left first
void clearSamples();
```

## Registers

| Address | Name | Purpose |
|---|---|---|
| `$FF10–$FF14` | NR10–NR14 | Channel 1: sweep, duty/length, envelope, frequency |
| `$FF16–$FF19` | NR21–NR24 | Channel 2: same without sweep |
| `$FF1A–$FF1E` | NR30–NR34 | Channel 3 — **not implemented** |
| `$FF20–$FF23` | NR41–NR44 | Channel 4 — **not implemented** |
| `$FF24` | NR50 | Master volume per side, 0–7, scaling as `(n+1)/8` |
| `$FF25` | NR51 | Panning: bits 0–3 route channels 1–4 right, 4–7 left |
| `$FF26` | NR52 | Bit 7 powers the APU; bits 0–3 are read-only channel status |

Powering the APU off through NR52 clears every register and makes them
read-only until it is switched back on. Wave RAM (`$FF30–$FF3F`) stays
accessible either way, and is stored so writes are not lost even though nothing
plays it yet.

## The frame sequencer

A 512 Hz clock — one step every 8192 T-cycles — driving three units on an
8-step cycle:

| Step | Length (256 Hz) | Sweep (128 Hz) | Envelope (64 Hz) |
|---|---|---|---|
| 0, 4 | ✓ | | |
| 2, 6 | ✓ | ✓ | |
| 7 | | | ✓ |

`FrameSequencer::tick()` returns the step that just began, or `-1` when the
boundary was not crossed, so `APU::tick()` reads as a straight dispatch.

## PulseChannel

A square wave with four selectable duty cycles, plus three modulation units.

**Duty and frequency.** The waveform is 8 steps of one bit, stored as a byte per
duty setting and read from bit 7 down. The period register `N` means a step
every `(2048 - N) * 4` T-cycles, so *larger* `N` is a higher pitch. The
resulting tone is `131072 / (2048 - N)` Hz.

**Length counter.** Loaded as `64 - (NRx1 & 0x3F)`, decremented at 256 Hz when
NRx4 bit 6 is set, and disables the channel on reaching zero. A trigger with a
zero counter reloads it to 64.

**Volume envelope.** Ramps volume by 1 every `NRx2 & 7` envelope ticks, clamped
at 0 and 15. A period of **0 disables the envelope entirely** rather than making
it maximally fast.

**Frequency sweep (channel 1 only).** Recalculates as
`new = shadow ± (shadow >> shift)`. Exceeding 2047 disables the channel. Two
non-obvious behaviours are implemented because Blargg's sound tests check them:

- After writing back, a **second calculation runs purely as an overflow check**.
  Its result is discarded, but it can still disable the channel.
- Clearing the negate bit after a subtract has already happened disables the
  channel immediately. `m_sweepNegated` tracks that.

**The DAC** is enabled when `NRx2 & 0xF8 != 0`. Turning it off kills the
channel; turning it back on does *not* restart it — that needs a trigger. A
disabled DAC contributes silence rather than the level the channel last held.

## Mixing and resampling

Each channel's digital 0–15 level maps to an analogue swing of `level/7.5 - 1`.
Channels are routed per side by NR51, scaled by NR50, and written as interleaved
signed 16-bit stereo.

`MIX_SCALE` is `32767/4` — headroom for four channels at full scale, so adding
channels 3 and 4 will not clip. With both pulse channels at full volume the
observed peak is 16383, exactly half of full scale, as expected.

Resampling from the 4.19 MHz master clock uses a fixed-point accumulator:

```cpp
m_sampleAccum += tcycles * SAMPLE_RATE;
while (m_sampleAccum >= CPU_HZ) { m_sampleAccum -= CPU_HZ; generateSample(); }
```

Carrying the remainder rather than dividing avoids drift — one emulated second
produces exactly 48000 frames.

While the APU is powered off it still emits silence at the same rate, so the
frontend's audio queue does not starve.

## Current state

Complete for roadmap Phase 10. Verified two ways:

- **Synthetic tone.** Programming channel 1 with period 1750 and measuring zero
  crossings gives 440.0 Hz against a predicted 439.8 Hz, with exactly 48000
  frames per emulated second and a peak of 8191.
- **Real game.** Tetris's title music captured to a 48 kHz stereo WAV shows
  varying per-second RMS (6400–10100) and a peak of 16383, i.e. both pulse
  channels active and modulating.

## Not implemented yet

- **Channel 3 (wave)** — Wave RAM is stored but never played. Phase 11.
- **Channel 4 (noise)** — the 15-bit LFSR. Phase 11.
- **The frame sequencer runs on its own counter**, not off DIV bit 4 as the
  hardware does. Writing to DIV can therefore not clock the sequencer, which is
  observable in some test ROMs.
- **No high-pass filter.** Hardware removes the DC offset from
  inactive-but-DAC-enabled channels.
- **No `dmg_sound` test ROM** in `roms/`, so the sweep and envelope edge cases
  above are implemented to spec but unverified.
- **Frame-paced output.** Samples are queued once per frame rather than
  streamed, and the frontend drops samples if emulation outruns playback.
