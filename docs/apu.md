# APU — audio processing unit

**Source:** `src/apu/apu.h`, `src/apu/apu.cpp`

> **Status: stub.** `apu.h` is an empty include guard and `apu.cpp` contains only
> its own `#include`. There is no `APU` class. `GameBoy` does not have an APU
> member, and `main.cpp` never initialises `SDL_INIT_AUDIO`.
>
> Nothing below is implemented — this documents the intended design so the file
> is ready when Phase 10 starts.

## Planned responsibility

Four sound channels mixed to stereo, driven from the same T-cycle tick as every
other peripheral, producing samples for the frontend to consume through an
`SDL_AudioCallback` and a ring buffer.

Registers `$FF10–$FF3F` are currently inert slots in the MMU's flat I/O array.

## Planned structure

| Channel | Type | Distinguishing feature |
|---|---|---|
| CH1 | Pulse / square | Frequency sweep |
| CH2 | Pulse / square | No sweep |
| CH3 | Custom waveform | 32 4-bit samples from Wave RAM (`$FF30–$FF3F`) |
| CH4 | Noise | 15-bit LFSR |

### Frame sequencer

A 512 Hz clock derived from DIV bit 4's falling edge, driving three subsystems
on an 8-step cycle:

- Length counters on steps 0, 2, 4, 6 → 256 Hz
- Frequency sweep (CH1 only) on steps 2, 6 → 128 Hz
- Volume envelopes on step 7 → 64 Hz

This is a direct dependency on the [timer](timer.md)'s internal counter, so
Phase 4 should land first.

### DAC behaviour

Each channel's DAC maps digital 0–15 to analog +1..−1, inverted. A DAC is
enabled when `NRx2 & 0xF8 != 0` for CH1/CH2/CH4, or NR30 bit 7 for CH3. A
disabled DAC outputs analog 0, and a high-pass filter removes the DC offset from
inactive-but-enabled channels.

## Design note for the GBA path

`roadmap.md`'s GBA appendix calls for the channel logic to be written as
standalone `PulseChannel` / `WaveChannel` / `NoiseChannel` / `FrameSequencer`
types with a thin register-decode layer on top, rather than being buried inside
an `APU` class. The GBA's four legacy PSG channels are the same hardware at
different addresses, so this is the one part of the emulator that transfers
directly. **Make that structural decision when writing this file, not after.**

## Reference

Roadmap Phases 10 and 11 cover the full design, including the wave-RAM quirks,
the LFSR's 7-bit mode, envelope timing, and the sweep overflow rules.
