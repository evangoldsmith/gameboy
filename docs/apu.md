# APU — audio processing unit

**Source:** `src/apu/apu.h`, `src/apu/apu.cpp`, `src/apu/channels.h`, `src/apu/channels.cpp`

Owns the sound registers (`$FF10–$FF3F`), drives all four channels from the
frame sequencer, and resamples their combined output down to a normal audio
rate. Has no SDL dependency — the frontend drains `samples()` once a frame.

## Structure

The channel logic lives in `channels.h`/`channels.cpp` as standalone types that
know nothing about the Game Boy address space; `APU` is a thin register-decode
layer over them.

| Type | Role |
|---|---|
| `FrameSequencer` | 512 Hz clock driving length, sweep and envelope |
| `VolumeEnvelope` | NRx2 envelope, shared by channels 1, 2 and 4 |
| `PulseChannel` | Channels 1 and 2; sweep is constructor-selected |
| `WaveChannel` | Channel 3 |
| `NoiseChannel` | Channel 4 |

That split is deliberate. The GBA's four legacy PSG channels are this same
hardware at different addresses, so keeping these free of address decoding is
what makes them reusable if a GBA core ever happens — see the appendix in
`roadmap.md`.

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
| `$FF1A–$FF1E` | NR30–NR34 | Channel 3: DAC, length, output level, frequency |
| `$FF20–$FF23` | NR41–NR44 | Channel 4: length, envelope, LFSR clock |
| `$FF30–$FF3F` | Wave RAM | 32 four-bit samples, upper nibble first |
| `$FF24` | NR50 | Master volume per side, 0–7, scaling as `(n+1)/8` |
| `$FF25` | NR51 | Panning: bits 0–3 route channels 1–4 right, 4–7 left |
| `$FF26` | NR52 | Bit 7 powers the APU; bits 0–3 are read-only channel status |

Powering the APU off through NR52 clears every register and makes them
read-only until it is switched back on. **Wave RAM survives** a power cycle and
stays accessible either way — `WaveChannel::powerOff()` deliberately leaves the
sample data alone.

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

## WaveChannel

Plays 32 four-bit samples from its own wave RAM, upper nibble of each byte
first. No envelope — volume is a coarse right shift from NR32: mute, 100%, 50%,
25% (shifts of 4, 0, 1, 2).

Its period timer is `(2048 - N) * 2` T-cycles, **half** the pulse channels'
`* 4`. With 32 samples per cycle rather than 8 duty steps, a full waveform
repeats at `65536 / (2048 - N)` Hz — exactly half a pulse channel's rate for the
same `N`, which is a convenient sanity check.

Its DAC is NR30 bit 7, not an envelope, so `dacEnabled()` reads differently from
the other channels.

Two quirks:

- **The first sample played after a trigger is index 1, not 0.** Trigger resets
  the position to 0, and the first timer expiry advances *before* reading.
- **Reading wave RAM while the channel is running** returns whichever byte the
  wave pointer currently sits on, not the byte addressed. Writes behave the same
  way. This is DMG behaviour; CGB differs.

## NoiseChannel

A 15-bit linear feedback shift register clocked at `divisor << shift`, where the
divisor comes from a table (`8, 16, 32, 48, 64, 80, 96, 112`) rather than a
formula — code 0 is a half-step, not zero.

Each clock XORs the bottom two bits and feeds the result back into bit 14:

```cpp
bit = (lfsr & 1) ^ ((lfsr >> 1) & 1);
lfsr = (lfsr >> 1) | (bit << 14);
if (width7) lfsr = (lfsr & ~(1 << 6)) | (bit << 6);
```

The width bit also feeds bit 6, shortening the repeat from 32767 steps to 127 —
short enough to hear as a pitch, which is the metallic tone games use for
certain effects.

Output is **bit 0 inverted**, so a register full of ones idles quiet. The LFSR
resets to all ones on trigger.

Length and envelope behave exactly as on the pulse channels, via the shared
`VolumeEnvelope`.

## Mixing and resampling

Each channel's digital 0–15 level maps to an analogue swing of `level/7.5 - 1`.
Channels are routed per side by NR51, scaled by NR50, and written as interleaved
signed 16-bit stereo.

### Headroom

`MIX_SCALE` is `32767/5.5`, not `32767/4`. Four channels each swing ±1, so a
scale of `/4` fills the range *exactly* and leaves nothing for the high-pass
filter's ripple — measured that way, roughly 6% of samples clipped, and
steadily rather than as a startup transient. The extra divisor costs about
2.5 dB and brings the worst case to a peak of 26432 with zero clipped samples.

`toSample()` clamps rather than relying on the cast: converting an out-of-range
float to `int16_t` is undefined behaviour, so the clamp is a correctness
requirement, not just a quality one.

### High-pass filter

A one-pole filter per side removes the DC offset a channel contributes while its
DAC is enabled but it is not playing:

```cpp
out = in - capacitor;
capacitor = in - out * HPF_CHARGE;
```

Hardware bleeds this away through an RC network decaying by ~0.999958 per
T-cycle; raised to the number of cycles between output samples that becomes the
0.996337 used here. Without it, enabling a DAC introduces a constant bias that
clicks audibly on every trigger.

### Resampling

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

Complete for roadmap Phases 10 and 11 — all four channels. Verified three ways:

- **Synthetic tones**, measured by zero crossings over one emulated second:
  channel 1 at period 1750 gives 440.0 Hz against a predicted 439.8; channel 3
  with the same period gives 220.0 Hz against 219.9, confirming its half-rate
  timer. Channel 4 produces broadband output at ~24000 crossings per second.
- **Clipping.** All four channels driven simultaneously at maximum volume peak
  at 26432 with zero clipped samples.
- **Real game.** Over 300 frames of Tetris's title music, channel status in NR52
  shows CH1 and CH2 active for 201 frames each, CH3 for all 300 (the bass line)
  and CH4 for 12 (percussion) — the shape you would expect from the
  arrangement.

## Blargg `dmg_sound`: 7 / 12

| Sub-test | Result |
|---|---|
| **01-registers** | **ok** |
| **02-len ctr** | **ok** |
| 03-trigger | fail (03) |
| **04-sweep** | **ok** |
| **05-sweep details** | **ok** |
| **06-overflow on trigger** | **ok** |
| 07-len sweep period sync | fail (05) |
| **08-len ctr during power** | **ok** |
| 09-wave read while on | fail (01) |
| 10-wave trigger while on | fail (01) |
| **11-regs after power** | **ok** |
| 12-wave write while on | fail (01) |

**All three sweep tests pass**, including the discarded-second-calculation
overflow check and the negate-bit rule — the two behaviours that looked most
like bugs when writing them.

Two fixes took this from 4/12:

- **`$FF27–$FF2F` must read `$FF`.** Those addresses are unused, but they still
  belong to the APU. Leaving them out of the MMU's APU range let them fall
  through to the flat I/O array and read back as `$00`. Fixed `01-registers`.
- **Length counters stay writable while the APU is powered off.** On DMG they
  survive a power cycle *and* remain reachable through the NRx1 load registers,
  even though every other register is read-only in that state. That is what
  `writeLengthLoad()` exists for. Fixed `11-regs after power` and `08-len ctr
  during power`.

The five remaining failures fall into two groups:

- **Frame sequencer phase** (03, 07) — trigger behaviour and length/sweep
  synchronisation both depend on *where* in the 8-step cycle an event lands,
  which is where running the sequencer off its own counter rather than DIV
  bit 4 shows up.
- **Wave RAM while the channel is running** (09, 10, 12) — the read/write
  redirect is implemented, but not the narrow timing window it applies in, nor
  the corruption a retrigger causes.

## Not implemented yet

- **The frame sequencer runs on its own counter**, not off DIV bit 4 as the
  hardware does. Writing to DIV therefore cannot clock the sequencer, which is
  likely behind the length-counter failures above.
- **The wave-RAM corruption quirk is not emulated** — on DMG, retriggering
  channel 3 while it is reading can corrupt the first four bytes.
- **7-bit LFSR periodicity is not separately verified.** Both widths produce
  noise, but the shortened repeat was not measured.
- **Frame-paced output.** Samples are queued once per frame rather than
  streamed, and the frontend drops samples if emulation outruns playback.
