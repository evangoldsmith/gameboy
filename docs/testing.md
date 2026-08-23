# Testing and CI

**Source:** `tests/unit/`, `tests/gbrun.cpp`, `tests/run_tests.py`,
`.github/workflows/ci.yml`

Every change runs through GitHub Actions in three stages: it must **build in
both configurations without warnings**, then the **unit tests** must pass, and
only then are the **test ROMs** run.

Unit tests come before the ROMs deliberately — they need no downloads, finish in
milliseconds, and a failure in them points at one component rather than at a
screenful of pixels.

## Unit tests

`tests/unit/` covers components in isolation using
[doctest](https://github.com/doctest/doctest), fetched at configure time rather
than vendored so nothing third-party lives in the tree. It is header-only, so
CMake fetches the source without configuring it — its own `CMakeLists.txt`
declares a minimum below what CMake 4 accepts.

```bash
make test                             # builds debug, then runs them
make test TEST_ARGS="-ts=timer"       # one suite: timer, joypad, apu, cartridge
```

The binary is a **debug-build artefact only**. Release is what ships, so it does
not pay to compile a test binary — nor to fetch a test framework at configure
time, which would otherwise make a release build require the network. Pass
`-DGB_BUILD_TESTS=ON` to build them in a release tree anyway.

What is covered, and why those pieces:

| File | Covers |
|---|---|
| `test_timer.cpp` | DIV rate and the full-counter reset, all four TIMA rates, the three ways an unrelated write ticks TIMA, the overflow reload delay and its cancellation |
| `test_joypad.cpp` | The active-low matrix, row selection and both/neither rows, interrupt on press but not release |
| `test_apu_channels.cpp` | Envelope period-0 and clamping, sequencer edge behaviour, duty output, length counter, sweep overflow, the noise LFSR's 127-step repeat in 7-bit mode, wave RAM surviving power-off |
| `test_cartridge.cpp` | Bank arithmetic for all four mappers, including MBC3's RAM-bank register not disturbing the ROM bank |

These target the logic that end-to-end ROMs exercise only indirectly, and
several encode bugs that actually happened: the joypad's active-low polarity
deadlocked Tetris, and MBC3's RAM-bank register once sent Pokémon Red into the
wrong bank.

Writing them found three bugs — all in the tests rather than the emulator, which
is itself worth knowing. The most instructive: `NRx2 & 0xF8` reaches down to
**bit 3**, so a channel with zero volume but the direction bit set still has a
live DAC. Reading "the upper five bits" as "the volume nibble" is the easy
mistake.

The synthetic ROMs in `test_cartridge.cpp` fill each bank with its own bank
number, so a single read says which bank is mapped.

## Why the ROMs are downloaded rather than committed

`roms/` is gitignored and stays that way — the repository is deliberately free
of ROM files. CI fetches the
[c-sp/game-boy-test-roms](https://github.com/c-sp/game-boy-test-roms) bundle at
a **pinned version** (`v7.0`), so a new upstream release cannot silently change
what CI checks. Bumping it is a commit, which is the point.

That bundle is also the only practical source for Mooneye and Mealybug, neither
of which publishes releases of its own.

## `gbrun` — the runner

```
gbrun <rom> <frames> [out.bmp]
```

Runs a ROM for a fixed number of frames, prints whatever it sent over the serial
port, and optionally dumps the final framebuffer as a 24-bit BMP. It decides
nothing — it only produces evidence.

It links `gameboy_core` alone, so it needs no SDL and runs headless anywhere.
That the core has no SDL dependency, decided back in Phase 0, is what makes this
possible at all.

## `run_tests.py` — the gate

Runs each suite and compares its final screen against the reference screenshot
shipped alongside the ROM.

**Comparison is by shade index, not RGB.** The DMG has no colours: it stores a
2-bit shade per pixel and the palette is an emulator's choice. Ours is green and
the reference images are grey, and both are equally correct — so each image is
reduced to four luminance-ranked levels first. Comparing raw RGB would report
every pixel as wrong on a pixel-perfect render.

Serial output is captured too, but only as diagnostic detail in the report;
screens are what actually gate. That matters because several suites
(`halt_bug`, `dmg_sound`, `oam_bug`) report only on screen and emit nothing over
serial.

### Mooneye

Mooneye ROMs decide for themselves: each executes `LD B,B` when finished and
passes only if the registers hold **B=3 C=5 D=8 E=13 H=21 L=34** — a sequence
unlikely to arise by accident. `gbrun --mooneye` runs until that breakpoint or a
120-second emulated timeout and exits 0 or 1 accordingly.

`LD B,B` is a legitimate no-op a real game may execute, so `CPU::setBreakOnLdBB`
is **off by default** and only the test harness turns it on.

Only ROMs targeting the hardware we emulate are run. Mooneye names applicable
models after a hyphen, so `MOONEYE_MODELS` keeps no-suffix, `GS` (the whole
non-colour family), `dmgABC` and `dmgABCmgb`, and skips `dmg0`, `mgb`, `sgb`,
`sgb2` and everything CGB or AGB.

These are gated **on a baseline count** rather than all-or-nothing: CI fails if
fewer pass than last time, so improvements cost nothing but regressions are
caught. Raise `MOONEYE_BASELINE` when tests start passing — that is what stops
a gain being lost again later.

`--verbose` lists the individual failures.

### What is gated

| Suite | Gated | Status |
|---|---|---|
| `cpu_instrs` | yes | pass |
| `instr_timing` | yes | pass |
| `mem_timing` | yes | pass |
| `halt_bug` | yes | pass |
| `dmg-acid2` | yes | pass, pixel-exact |
| `dmg_sound` | no | 9/12 — wave-RAM access window |
| `oam_bug` | no | 2/8 — deferred, see `roadmap.md` |
| Mooneye `acceptance` | baseline | 40/66 |
| Mooneye `emulator-only` | baseline | 25/28 (MBC tests) |

### What the Mooneye failures say

They cluster, and each cluster maps onto a gap this documentation already
records:

| Cluster | Tests | Cause |
|---|---|---|
| Instruction timing (`call`, `jp`, `ret`, `add_sp`, `ld_hl_sp`) | 7 | These use OAM DMA as a clock and read their operands from OAM, so what remains is which regions a transfer can source from — not instruction timing as such |
| PPU timing (`intr_2_*`, `lcdon_*`, `stat_lyc_onoff`, `hblank_ly_scx`) | 9 | Fixed mode 3 length and imprecise mode transitions — see [ppu.md](ppu.md) |
| OAM DMA (`sources`) | 1 | `$FEA0–$FEFF` is modelled as flat `$FF` — see [mmu.md](mmu.md) |
| Timer (`rapid_toggle`, `tima_write_reloading`, `tma_write_reloading`) | 3 | Sub-M-cycle write timing — see [timer.md](timer.md) |
| MBC1 (`bits_mode`, `ram_256kb`) | 2 | MBC1 mode 1 banking is unimplemented — see [cartridge.md](cartridge.md) |

The PPU cluster is the one worth attacking first: it is the largest, and it also
blocks the OAM corruption bug, which failed twice for want of exactly this
precision.

The two ungated suites are run and reported but cannot fail the build; they
track known gaps, and each carries a note saying what is missing. Gating them
would make CI permanently red and therefore ignored.

**When one of them is fixed, move it to gated.** That is what stops it
regressing later.

## Running it locally

```bash
curl -fsSL -o test-roms.zip \
  https://github.com/c-sp/game-boy-test-roms/releases/download/v7.0/game-boy-test-roms-v7.0.zip
unzip -q test-roms.zip -d test-roms
pip install pillow

make release
python3 tests/run_tests.py build/release/gbrun test-roms
```

Result screens land in `test-output/`. CI uploads that directory as an artifact
**even when the run fails**, so a failure can be looked at rather than guessed
at.

## `-Werror`

CI builds with `-Werror` on top of the project's warning set. The runner uses
GCC while local development is usually Clang, and they disagree — enabling this
immediately surfaced a sign-conversion in `ppu.cpp` that Clang had never
flagged. Worth knowing that a clean local build is not proof of a clean CI
build.

## Not implemented yet

- **No unit tests.** Everything here is end-to-end through real ROMs. There is
  no framework for testing a component in isolation.
- **No Mooneye or Mealybug runner.** Mooneye signals via a `LD B,B` breakpoint
  with the Fibonacci sequence in `B,C,D,E,H,L`; Mealybug needs per-revision
  screenshot comparison. Both need harness work `gbrun` does not do.
- **No performance regression check.** Nothing notices if the emulator gets
  slower.
- **Single platform.** Ubuntu only; nothing builds on macOS or Windows in CI,
  even though development happens on macOS.
