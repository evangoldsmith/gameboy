# Testing and CI

**Source:** `tests/gbrun.cpp`, `tests/run_tests.py`, `.github/workflows/ci.yml`

Every change runs through GitHub Actions: it must **build in both
configurations without warnings**, and only then are the test ROMs run.

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
| Mooneye `acceptance` | baseline | 32/66 |
| Mooneye `emulator-only` | baseline | 25/28 (MBC tests) |

### What the Mooneye failures say

They cluster, and each cluster maps onto a gap this documentation already
records:

| Cluster | Tests | Cause |
|---|---|---|
| Instruction timing (`call`, `jp`, `ret`, `rst`, `push`, `add_sp`) | 12 | Which M-cycle *within* an instruction each access lands on. `mem_timing` passes, so this is a finer distinction than that suite makes |
| PPU timing (`intr_2_*`, `lcdon_*`, `stat_lyc_onoff`, `hblank_ly_scx`) | 9 | Fixed mode 3 length and imprecise mode transitions — see [ppu.md](ppu.md) |
| OAM DMA (`oam_dma_*`, `sources`) | 4 | The copy is instantaneous rather than 160 M-cycles — see [mmu.md](mmu.md) |
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
