#!/usr/bin/env python3
"""Font/VRAM ground-truth sanity check (PyBoy).

Complements the harness, it doesn't replace it. docs/testing.md's "never
assert on pixels when semantic state exists" rule holds for gameplay state
(g_ui_screen_buf, telemetry) because mGBA's CLI debugger can't read VRAM
(see docs/graphics.md's g_tilemap_mirror note) -- semantic mirrors are the
right substitute *there*. But a mirror only proves the game *intended* to
draw character 'M'; it can't prove the tile that character maps to actually
looks like an 'M' on the PPU. That's a font/VRAM-layout property with no
semantic representation at all, so it needs a real pixel read -- this is
the one place a pixel-level check is the correct tool, not a workaround.

PyBoy reads VRAM directly (no debugger-access restrictions), so this runs
headless and fast. It boots the real ROM (not the harness/debug build) so
what it sees is exactly what a player sees, including LCDC as actually
configured, not the harness's frame-stepped mGBA view.

Checks:
  1. blank-space: the tile the console font maps ' ' to must be a blank
     (all-zero) bitmap. This is a strong, cheap invariant: the space glyph
     in font_ibm is blank by construction, so *anything* wrong in the
     char-to-tile formula (missing offset, wrong base, wrong VRAM address
     for the active LCDC addressing mode) almost always lands on a non-blank
     tile instead. This exact check would have caught the font-offset bug
     (docs/graphics.md ui_font_tile_base usage) in seconds instead of a
     multi-hour trace.
  2. hud-nonblank: every HUD row PyBoy's tile decode disagrees with ' '
     should paint *some* ink (catches the opposite failure mode: a HUD
     that's silently rendering as all-blank tiles).

Run inside the Nix dev shell:
    make vram-check
or: nix develop --command python3 tools/vram_check.py [--rom PATH] [--frames N]

Exits non-zero if any check fails.
"""

import argparse
import os
import sys

ROM_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "build", "kaartenheld.gb")

failures = []


def check(label, ok, detail=""):
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {label}" + (f" -- {detail}" if detail and not ok else ""))
    if not ok:
        failures.append(label)


def tile_addr(tile_id, lcdc):
    """VRAM address of a BG/window tile id under the LCDC-selected
    addressing mode (bit 4: 1 = unsigned/0x8000, 0 = signed/0x9000,
    where ids 128-255 alias the shared 0x8800-0x8FFF block either way)."""
    if lcdc & 0x10:
        return 0x8000 + tile_id * 16
    if tile_id < 128:
        return 0x9000 + tile_id * 16
    return 0x8800 + (tile_id - 128) * 16


def read_tile(pb, tile_id, lcdc):
    addr = tile_addr(tile_id, lcdc)
    return [pb.memory[addr + i] for i in range(16)]


def is_blank(tile_bytes):
    return all(b == 0 for b in tile_bytes)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=ROM_DEFAULT, help="ROM to boot (release build by default)")
    ap.add_argument("--frames", type=int, default=180,
                     help="frames to run before sampling (default: ~3s, past boot)")
    args = ap.parse_args()

    if not os.path.isfile(args.rom):
        print(f"error: ROM not found: {args.rom}", file=sys.stderr)
        print("Build it first (make release / make debug).", file=sys.stderr)
        return 1

    try:
        from pyboy import PyBoy
    except ImportError:
        print("error: pyboy not installed. `pip install pyboy` in the dev shell.", file=sys.stderr)
        return 1

    pb = PyBoy(args.rom, window="null")
    for _ in range(args.frames):
        pb.tick()

    lcdc = pb.memory[0xFF40]
    print(f"LCDC = 0x{lcdc:02X} (BG/win tile addressing: "
          f"{'unsigned/0x8000' if lcdc & 0x10 else 'signed/0x9000'})")

    # ── Check 1: blank-space ────────────────────────────────────────────
    # Find the tile id the game actually wrote for a space in the HUD row
    # (window tilemap 0x9C00) rather than assuming a formula, so this check
    # keeps working even if the char->tile mapping in ui.c changes shape.
    tm = pb.tilemap_window
    space_tile_id = None
    # tile_identifier(x, y); PyBoy adds +256 when the signed/0x9000 block
    # is in play, so normalise back to the raw 0-255 VRAM tile id.
    def raw_id(x, y):
        ident = tm.tile_identifier(x, y)
        return ident - 256 if ident >= 256 else ident

    row0_ids = [raw_id(col, 0) for col in range(20)]
    for r in (0, 3, 2, 4, 1):
        r_ids = [raw_id(col, r) for col in range(20)]
        if r_ids and len(set(r_ids)) == 1:
            space_tile_id = r_ids[0]
            break

    if space_tile_id is None:
        check("blank-space", False, "could not identify the space tile from HUD row 3 (layout changed?)")
    else:
        bitmap = read_tile(pb, space_tile_id, lcdc)
        ok = is_blank(bitmap)
        detail = ""
        if not ok:
            rows = []
            for r in range(8):
                lo, hi = bitmap[r * 2], bitmap[r * 2 + 1]
                rows.append("".join(" .:#"[((hi >> b) & 1) << 1 | ((lo >> b) & 1)]
                                     for b in range(7, -1, -1)))
            detail = (f"tile {space_tile_id} (addr 0x{tile_addr(space_tile_id, lcdc):04X}) "
                      f"is not blank -- char-to-tile offset is likely wrong: " + " / ".join(rows))
        check("blank-space", ok, detail)

    # ── Check 2: hud-nonblank ────────────────────────────────────────────
    # The inverse failure mode: HUD text rows that are non-space but render
    # as blank tiles (font not loaded / wrong bank / VRAM never written).
    text_row_ids = [raw_id(col, 1) for col in range(20)]  # "MAP:... | HP:.." row
    non_space = [t for t in text_row_ids if t != space_tile_id]
    all_blank = all(is_blank(read_tile(pb, t, lcdc)) for t in non_space) if non_space else True
    check("hud-nonblank", not all_blank,
          "every non-space HUD tile on row 1 renders blank" if all_blank else "")

    print()
    if failures:
        print(f"{len(failures)} check(s) failed: {', '.join(failures)}")
        return 1
    print("All VRAM sanity checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
