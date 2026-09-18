#!/usr/bin/env python3
"""palette_check.py -- Fail loudly on palette drift.

Single contract: assets/palette.txt (via tools/palette_txt.py) is the source
of truth. This check asserts:

1. palette.txt parses (bad refs / ties / slotmap problems fail loudly).
2. tools/palette_compiler.py FIXED_PALETTES / ANCHOR_COLORS equal the
   palette_txt RAMPS / ANCHORS (i.e. the compiler was not hand-edited
   past the source).
3. src/game/tiles_content.c cgb_bg_palettes* RGB8() values equal the
   palette_txt RAMPS (base/forest/desolate_landscape/castle/village).
4. src/ui/ui.c OAM 0..3 ramps equal palette_txt OBJ tables positionally.
5. Every hard index consumer (tile overrides, npc
   overlays, battle-art palettes, ow palettes, UI base set) resolves to
   a REAL ramp -- consumed-but-missing slots fail naming the consumer;
   duplicated placeholders are reported.
6. assets/palettes.md is freshly generated.

Messages are French-first (the artist reads these failures).

Usage:
    python3 tools/palette_check.py
    make palette-check
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))

import palette_txt
from palette_txt import (SECTIONS, RAMPS, OBJ_BY_SLOT, ANCHORS,
                         REAL_SLOTS, DUPLICATES, UNMAPPED, LOAD_ERRORS,
                         LOAD_WARNINGS, BUILD_ERRORS, BUILD_WARNINGS,
                         hard_consumers)

ERRORS: list = []


def fail(msg: str):
    ERRORS.append(msg)
    print(f"palette-check FAIL: {msg}")


def info(msg: str):
    print(f"palette-check info: {msg}")


def check_compiler():
    import palette_compiler as pc

    if pc.FIXED_PALETTES != {k: RAMPS[k] for k in
                             ("forest", "desolate_landscape", "castle", "village")}:
        fail("palette_compiler.FIXED_PALETTES != palette_txt.RAMPS")
        for ts in ("forest", "desolate_landscape", "castle", "village"):
            for i, (a, b) in enumerate(zip(pc.FIXED_PALETTES[ts], RAMPS[ts])):
                if [tuple(c) for c in a] != [tuple(c) for c in b]:
                    fail(f"  {ts} ramp {i}: compiler {a} vs palette.txt {b}")
    if pc.ANCHOR_COLORS != {k: ANCHORS[k] for k in pc.ANCHOR_COLORS}:
        fail(f"palette_compiler.ANCHOR_COLORS {pc.ANCHOR_COLORS} != {ANCHORS}")


def _parse_c_arrays(path: Path, symbols: list) -> dict:
    """Parse `const ... SYM[8][4] = { RGB8(..), ... };` -> {sym: [8x4 rgb]}."""
    text = path.read_text()
    out = {}
    for sym in symbols:
        m = re.search(re.escape(sym) + r"\s*\[4\]\s*=\s*\{(.*?)\};", text, re.S)
        if not m:
            m = re.search(re.escape(sym) + r"\s*\[8\]\[4\]\s*=\s*\{(.*?)\};", text, re.S)
        if not m:
            fail(f"{path.name}: array {sym} not found")
            continue
        vals = re.findall(r"RGB8\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", m.group(1))
        if len(vals) != 32 and "[8][4]" in (m.group(0)[:80]):
            fail(f"{path.name}: {sym} has {len(vals)} RGB8 values (want 32)")
            continue
        if len(vals) == 4:
            out[sym] = [tuple(int(v) for v in t) for t in vals]
        else:
            out[sym] = [[tuple(int(v) for v in t) for t in vals[i * 4:(i + 1) * 4]]
                        for i in range(8)]
    return out


def check_tables_fresh():
    # ROM tables are #included generated files: verify the committed
    # generated tables match a fresh emit (hand edits belong in
    # palette.txt, never in generated/tiles/cram_tables.h).
    import tempfile
    from palette_txt import emit_c_tables
    with tempfile.TemporaryDirectory() as tmp:
        fresh = {p.name: p.read_text()
                 for p in emit_c_tables(tmp)}
    gen_dir = REPO_ROOT / "generated" / "tiles"
    for name, want in fresh.items():
        got = (gen_dir / name).read_text() if (gen_dir / name).exists() \
            else ""
        if got != want:
            fail(f"generated/tiles/{name} is stale (run make manifest)")
    # The ROM must include (not duplicate) the generated tables.
    tiles = (REPO_ROOT / "src" / "game" / "tiles_content.c").read_text()
    if '#include "cram_tables.h"' not in tiles:
        fail("tiles_content.c must #include cram_tables.h (no hand arrays)")
    ui = (REPO_ROOT / "src" / "ui" / "ui.c").read_text()
    if '#include "obj_tables.h"' not in ui:
        fail("ui.c must #include obj_tables.h (no hand arrays)")


def check_doc():
    from palette_txt import render_doc, DOC_PATH
    want = render_doc() + "\n"
    got = DOC_PATH.read_text() if DOC_PATH.exists() else ""
    if got != want:
        fail("assets/palettes.md is stale (run make manifest)")


def check_load():
    for e in LOAD_ERRORS + BUILD_ERRORS:
        fail(f"palette.txt: {e} — voir assets/palette_tutorial.md")
    for w in LOAD_WARNINGS + BUILD_WARNINGS:
        info(w)
    for setkey, names in UNMAPPED.items():
        info(f"rampes sans slot ({setkey}, jamais affichees) : "
             f"{', '.join(names)} — les assigner dans SLOTS/"
             f"{setkey.upper()} ou les supprimer")


def check_consumers():
    consumers = hard_consumers()
    for setkey, slots in consumers.items():
        real = set(REAL_SLOTS.get(setkey, []))
        for slot, descs in sorted(slots.items()):
            if slot not in real:
                fail(f"slot {slot} ({setkey}) consomme par "
                     f"{', '.join(descs)} mais sans vraie ramp "
                     f"(FLORENT : en ecrire une / l'assigner dans SLOTS/"
                     f"{setkey.upper()})")
    for setkey, dups in DUPLICATES.items():
        for slot, (src, descs) in sorted(dups.items()):
            info(f"slot {slot} ({setkey}) duplique `{src}` pour "
                 f"{', '.join(descs)} — visuel plausible, vraie ramp "
                 f"attendee (FLORENT)")


def main() -> int:
    check_load()
    check_consumers()
    check_compiler()
    check_tables_fresh()
    check_doc()
    if ERRORS:
        print(f"palette-check: {len(ERRORS)} problem(s)")
        return 1
    print("palette-check: OK "
          f"({len(SECTIONS)} sections; BG+OBJ match palette.txt)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
