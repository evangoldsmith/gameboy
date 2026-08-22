#!/usr/bin/env python3
"""Run the test ROMs and compare each result screen against its reference.

Usage:
    tests/run_tests.py <gbrun-binary> <test-rom-dir> [--out DIR]

The test ROMs and their reference screenshots both come from
https://github.com/c-sp/game-boy-test-roms — the CI workflow downloads that
bundle and points this script at it.

Screens are compared by *shade index*, not by RGB. The DMG has no colours: it
stores a 2-bit shade per pixel, and the palette is an emulator choice. Ours is
green, the reference images are grey, and both are equally correct — so each
image is reduced to four ranked luminance levels before comparing.

Exit status is non-zero if any gated suite fails. Suites listed as not gated are
run and reported but cannot fail the build; they track known gaps, and the
comment on each says what is missing.
"""

import argparse
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("run_tests.py needs Pillow:  pip install pillow")

# rom path, reference screenshot, frames to run, gated, note
SUITES = [
    ("blargg/cpu_instrs/cpu_instrs.gb",   "blargg/cpu_instrs/cpu_instrs-dmg-cgb.png",   4000, True,  ""),
    ("blargg/instr_timing/instr_timing.gb","blargg/instr_timing/instr_timing-dmg-cgb.png", 500, True,  ""),
    ("blargg/mem_timing/mem_timing.gb",   "blargg/mem_timing/mem_timing-dmg-cgb.png",    500, True,  ""),
    ("blargg/halt_bug.gb",                "blargg/halt_bug-dmg-cgb.png",                1000, True,  ""),
    ("dmg-acid2/dmg-acid2.gb",            "dmg-acid2/dmg-acid2-dmg.png",                 300, True,  ""),
    ("blargg/dmg_sound/dmg_sound.gb",     "blargg/dmg_sound/dmg_sound-dmg.png",         7000, False,
     "9/12 — wave-RAM access window unimplemented"),
    ("blargg/oam_bug/oam_bug.gb",         "blargg/oam_bug/oam_bug-dmg.png",             4000, False,
     "2/8 — OAM corruption bug deferred, see roadmap.md"),
]


def shade_indices(img):
    """Reduce an image to one 0-3 shade index per pixel, ranked light to dark."""
    rgb = img.convert("RGB")
    pixels = list(rgb.getdata())
    luminance = lambda c: 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
    ranked = sorted(set(pixels), key=luminance, reverse=True)
    if len(ranked) > 4:
        raise ValueError(f"expected at most 4 distinct shades, found {len(ranked)}")
    index = {c: i for i, c in enumerate(ranked)}
    return [index[p] for p in pixels], rgb.size


def compare(actual_path, reference_path):
    """Returns (mismatched_pixels, total_pixels)."""
    a, a_size = shade_indices(Image.open(actual_path))
    b, b_size = shade_indices(Image.open(reference_path))
    if a_size != b_size:
        raise ValueError(f"size mismatch: {a_size} vs {b_size}")
    return sum(1 for x, y in zip(a, b) if x != y), len(a)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("gbrun", help="path to the gbrun binary")
    parser.add_argument("rom_dir", help="unpacked game-boy-test-roms directory")
    parser.add_argument("--out", default="test-output",
                        help="where to write each suite's result screen")
    args = parser.parse_args()

    gbrun = Path(args.gbrun).resolve()
    roms = Path(args.rom_dir).resolve()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    failures, skipped = [], []
    print(f"{'suite':<24} {'result':<22} notes")
    print("-" * 78)

    for rom_rel, ref_rel, frames, gated, note in SUITES:
        rom, ref = roms / rom_rel, roms / ref_rel
        name = Path(rom_rel).stem

        if not rom.exists():
            print(f"{name:<24} {'ROM MISSING':<22} {rom_rel}")
            failures.append(name) if gated else skipped.append(name)
            continue

        bmp = out / f"{name}.bmp"
        proc = subprocess.run([str(gbrun), str(rom), str(frames), str(bmp)],
                              capture_output=True, text=True)
        serial = " ".join(proc.stdout.split())

        try:
            bad, total = compare(bmp, ref)
        except Exception as exc:                      # noqa: BLE001
            print(f"{name:<24} {'COMPARE ERROR':<22} {exc}")
            failures.append(name) if gated else skipped.append(name)
            continue

        if bad == 0:
            status = "PASS"
        else:
            status = f"DIFF {bad}/{total} px"
            (failures if gated else skipped).append(name)

        detail = note if note else (serial[:44] if serial else "")
        print(f"{name:<24} {status:<22} {detail}")

    print("-" * 78)
    if skipped:
        print(f"not gated (known gaps): {', '.join(skipped)}")
    if failures:
        print(f"FAILED: {', '.join(failures)}")
        return 1
    print("all gated suites passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
