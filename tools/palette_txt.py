#!/usr/bin/env python3
"""palette_txt.py -- Single source of truth for authored colors.

`assets/palette.txt` is artist-owned and holds three things (tutorial:
`assets/palette_tutorial.md`):

1. COLOR dictionary (named sections): `name: #hex` entries.
2. RAMP tables (`RAMPS/<set>`): 8 ramps x 4 shades each (4 for OBJ),
   composed from dictionary references as `SECTION/name`.
3. ANCHORS: `sheet: SECTION/name` (or a raw `#hex` chroma-key).

This module parses the file, resolves every reference, and exposes the
canonical tables. There are no hardcoded colors left in the pipeline:
a slot changes if and only if its dictionary hex (or its reference)
changes in palette.txt.

Consumers:
  tools/palette_compiler.py  FIXED_PALETTES / ANCHOR_COLORS / RAMP_NAMES
  tools/palette_check.py     drift check vs tiles_content.c / ui.c /
                             Makefile / assets/palettes.md
  assets/palettes.md         generated semantic tables (make manifest)
"""

from pathlib import Path
from typing import Dict, List, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
PALETTE_TXT = REPO_ROOT / "assets" / "palette.txt"
DOC_PATH = REPO_ROOT / "assets" / "palettes.md"

RGB = Tuple[int, int, int]
# Ramp sets required by the engine (RAMPS/<SET> sections in palette.txt).
RAMP_SETS = ("base", "forest", "desolate_landscape", "castle", "village")
# Short file headers the artist may use (RAMPS/DESOLATE, ...).
SET_ALIASES = {"desolate": "desolate_landscape"}
# Sheet keys required in the ANCHORS section.
ANCHOR_KEYS = ("forest", "desolate_landscape", "castle", "village", "title",
               "npc_tiles", "enemy_ow")


def _hex_to_rgb(h: str, where: str) -> RGB:
    h = h.strip().lstrip("#")
    if len(h) != 6:
        raise ValueError(f"{where}: expected 6-digit hex, got '{h}'")
    try:
        return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
    except ValueError:
        raise ValueError(f"{where}: invalid hex color '{h}'")


def _parse_ref(ref: str, where: str) -> Tuple[str, str]:
    """Split a `SECTION/name` reference (exactly one '/')."""
    parts = ref.split("/")
    if len(parts) != 2 or not parts[0].strip() or not parts[1].strip():
        raise ValueError(
            f"{where}: bad reference '{ref}' (want SECTION/name)")
    return parts[0].strip(), parts[1].strip()


def load_palette(path: Path = PALETTE_TXT):
    """Parse palette.txt -> (colors, ramps, anchors).

    colors: {SECTION: {name: (r,g,b)}}
    ramps:  {setkey: [(rampname, [ref, ref, ref, ref])]} in file order
    anchors: {sheetkey: ref-or-#hex}
    Lines starting with '#' are comments. Fails loudly on any malformed
    line, unresolvable reference, or missing required set/key.
    """
    colors: Dict[str, Dict[str, RGB]] = {}
    ramps: Dict[str, List[Tuple[str, List[str]]]] = {}
    anchors: Dict[str, str] = {}
    seen = set()
    current = ""
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        where = f"{path}:{lineno}"
        if ":" not in line:
            current = line.upper()
            if current in seen:
                raise ValueError(f"{where}: duplicate section '{current}'")
            seen.add(current)
            if current.startswith("RAMPS/"):
                setkey = current[len("RAMPS/"):].lower()
                setkey = SET_ALIASES.get(setkey, setkey)
                if setkey in ramps:
                    raise ValueError(
                        f"{where}: duplicate ramp set '{setkey}'")
                ramps[setkey] = []
            elif current == "ANCHORS":
                pass
            else:
                colors[current] = {}
            continue
        if not current:
            raise ValueError(f"{where}: entry before any section: '{raw}'")
        name, _, value = line.partition(":")
        name = name.strip()
        value = value.strip()
        if not name or not value:
            raise ValueError(f"{where}: malformed entry: '{raw}'")
        if current.startswith("RAMPS/"):
            setkey = SET_ALIASES.get(current[len("RAMPS/"):].lower(),
                                     current[len("RAMPS/"):].lower())
            refs = [r.strip() for r in value.split(",")]
            if len(refs) != 4 or any(not r for r in refs):
                raise ValueError(
                    f"{where}: ramp '{name}' needs exactly 4 "
                    f"SECTION/name refs, got {len(refs)}")
            for r in refs:
                _parse_ref(r, where)
            if any(n == name for n, _ in ramps[setkey]):
                raise ValueError(
                    f"{where}: duplicate ramp '{name}' in '{current}'")
            ramps[setkey].append((name, refs))
        elif current == "ANCHORS":
            if value.startswith("#"):
                _hex_to_rgb(value, where)
            else:
                _parse_ref(value, where)
            if name in anchors:
                raise ValueError(f"{where}: duplicate anchor '{name}'")
            anchors[name] = value
        else:
            rgb = _hex_to_rgb(value, where)
            if name in colors[current]:
                raise ValueError(
                    f"{where}: duplicate '{name}' in [{current}]")
            colors[current][name] = rgb

    for setkey in list(RAMP_SETS) + ["obj"]:
        if setkey not in ramps:
            raise ValueError(f"{path}: missing RAMPS/{setkey.upper()} section")
        want = 4 if setkey == "obj" else 8
        if len(ramps[setkey]) != want:
            raise ValueError(
                f"{path}: RAMPS/{setkey.upper()} has "
                f"{len(ramps[setkey])} ramps (hardware needs {want})")
    for key in ANCHOR_KEYS:
        if key not in anchors:
            raise ValueError(f"{path}: missing ANCHORS entry '{key}'")
    return colors, ramps, anchors


def _resolve(colors, ref: str, where: str) -> RGB:
    section, name = _parse_ref(ref, where)
    try:
        return colors[section][name]
    except KeyError:
        raise KeyError(f"{where}: reference '{ref}' is not defined "
                       f"(want [{section}] '{name}' in palette.txt)")


def build_ramps(colors, ramps) -> Dict[str, list]:
    """Resolve every ramp reference to RGB tuples: {set: [8x4 rgb]}."""
    out: Dict[str, list] = {}
    for setkey in list(RAMP_SETS) + ["obj"]:
        out[setkey] = [[_resolve(colors, r, f"RAMPS/{setkey}")
                        for r in refs]
                       for _, refs in ramps[setkey]]
    return out


def build_anchors(colors, anchors) -> Dict[str, str]:
    """Resolve anchors to '#rrggbb' (raw-hex chroma-keys pass through)."""
    out: Dict[str, str] = {}
    for key in ANCHOR_KEYS:
        value = anchors[key]
        if value.startswith("#"):
            out[key] = value.lower()
        else:
            r, g, b = _resolve(colors, value, "ANCHORS")
            out[key] = f"#{r:02x}{g:02x}{b:02x}"
    return out


def unreferenced(colors, ramps, anchors) -> Dict[str, List[str]]:
    """Dictionary names no ramp/anchor references (artist visibility)."""
    used = set()
    for entries in ramps.values():
        for _, refs in entries:
            used.update(_parse_ref(r, "ramps")[0:2] for r in refs)
    for value in anchors.values():
        if not value.startswith("#"):
            used.add(_parse_ref(value, "anchors")[0:2])
    # Tuples compare (section, name) against used (section, name) pairs.
    out: Dict[str, List[str]] = {}
    for section, names in colors.items():
        spare = sorted(n for n in names
                       if (section, n) not in
                       {(s, m) for s, m in used})
        if spare:
            out[section] = spare
    return out


COLORS, _RAMPS_RAW, _ANCHORS_RAW = load_palette()
SECTIONS = COLORS
RAMPS = build_ramps(COLORS, _RAMPS_RAW)
# OBJ set keeps ramp names aligned with the ui.c OBJ order.
OBJ_RAMPS = {name: ramp for (name, _), ramp in
             zip(_RAMPS_RAW["obj"], RAMPS.pop("obj"))}
ANCHORS = build_anchors(COLORS, _ANCHORS_RAW)
RAMP_NAMES = {s: [n for n, _ in _RAMPS_RAW[s]] for s in RAMP_SETS}

# What each ramp serves (engine-side docs for the generated tables).
RAMP_USES = {
    "base": ["UI text/backdrop, spider art", "burn cards, kobold art",
             "sword/freeze cards", "slime art, heal cards",
             "poison/dagger cards, dialogue paper", "shield cards, mimic art",
             "bow cards", "grey-out, bat/boss art"],
    "forest": ["misc gray", "campfire tiles", "iron accents",
               "canopy + grass (UI_COLOR_FIELD)", "poison accents",
               "trunks/stumps, merchant NPC (UI_COLOR_WOOD)",
               "gold accents, mayor NPC", "rocks (UI_COLOR_DIM)"],
    "desolate_landscape": ["misc gray", "campfire tiles", "iron accents",
                           "flora accents", "poison accents",
                           "dead trees (UI_COLOR_WOOD)",
                           "gold accents, treasure chest",
                           "slate ground (UI_COLOR_DIM)"],
    "castle": ["stone floors/walls (UI_COLOR_NONE)", "curtains",
               "iron accents", "moss accents", "poison accents",
               "furniture (UI_COLOR_WOOD)", "gold accents, chest",
               "shading (UI_COLOR_DIM)"],
    "village": ["stonework (UI_COLOR_NONE)", "braziers/torches",
                "iron accents", "dirt ground (UI_COLOR_FIELD)",
                "mauve accents", "houses/barrels, merchant NPC (UI_COLOR_WOOD)",
                "walls/roofs, mayor NPC", "shading (UI_COLOR_DIM)"],
}

_OBJ_USES = {
    "grey": "player, bats, UI sprites (OBJ 0)",
    "orange": "town braziers, kobolds (OBJ 1)",
    "brown": "hero, kobold bodies, chests (OBJ 2)",
    "green": "overworld slimes (OBJ 3)",
}


def _hx(rgb) -> str:
    return f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"


def _prov_refs(setkey: str, idx: int) -> str:
    return ", ".join(_RAMPS_RAW[setkey][idx][1])


def render_doc() -> str:
    """Render assets/palettes.md from the canonical tables."""
    for setkey in list(RAMP_SETS) + ["obj"]:
        entries = _RAMPS_RAW[setkey]
        want = 4 if setkey == "obj" else 8
        if len(entries) != want or any(len(r) != 4 for _, r in entries):
            raise ValueError(
                f"palette doc: ramp shape wrong for {setkey}")
    L = []
    L.append("# Defined palettes (generated — do not edit by hand)")
    L.append("")
    L.append("Source of truth: `assets/palette.txt`, read by")
    L.append("`tools/palette_txt.py`.  Regenerate with `make manifest`")
    L.append("(the `palette-check` gate fails on drift).  Every shade")
    L.append("cites the `palette.txt` reference it resolves from; artist")
    L.append("tutorial: `assets/palette_tutorial.md`.")
    L.append("")
    titles = {
        "base": "Base — battle / card UI (`cgb_bg_palettes`)",
        "forest": "Forest / field (`cgb_bg_palettes_forest`)",
        "desolate_landscape":
            "Desolate (`cgb_bg_palettes_desolate`)",
        "castle": "Castle (`cgb_bg_palettes_castle`)",
        "village": "Village / town (`cgb_bg_palettes_village`)",
    }
    for ts in RAMP_SETS:
        L.append(f"## {titles[ts]}")
        L.append("")
        L.append("| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |")
        L.append("|---|------|----|----|----|----|--------|------|")
        for i, ramp in enumerate(RAMPS[ts]):
            hexes = " | ".join(f"`{_hx(c)}`" for c in ramp)
            L.append(f"| {i} | {RAMP_NAMES[ts][i]} | {hexes} | "
                     f"{RAMP_USES[ts][i]} | {_prov_refs(ts, i)} |")
        L.append("")
    L.append("## OBJ sprite ramps (`src/ui/ui.c`, slot 0 = COMMON white)")
    L.append("")
    L.append("| Name | S0 | S1 | S2 | S3 | Serves | Refs |")
    L.append("|------|----|----|----|----|--------|------|")
    for i, key in enumerate(k for k, _ in _RAMPS_RAW["obj"]):
        ramp = OBJ_RAMPS[key]
        hexes = " | ".join(f"`{_hx(c)}`" for c in ramp)
        L.append(f"| {key} | {hexes} | {_OBJ_USES[key]} | "
                 f"{_prov_refs('obj', i)} |")
    L.append("")
    L.append("## Sheet anchors (`png2gb --anchor-color` → shade 0)")
    L.append("")
    L.append("| Sheet | Anchor | Ref |")
    L.append("|-------|--------|-----|")
    sheet_of = {"forest": "forest-tile", "desolate_landscape": "desolate",
                "castle": "castle-tile", "village": "village-tile",
                "title": "title-red", "npc_tiles": "npc_tiles",
                "enemy_ow": "enemy_sprites"}
    for key, anchor in ANCHORS.items():
        ref = _ANCHORS_RAW[key]
        note = "chroma-key, not a palette color" if ref.startswith("#") \
            else ref
        L.append(f"| `{sheet_of[key]}` | `{anchor}` | {note} |")
    L.append("")
    return "\n".join(L)


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Render assets/palettes.md")
    ap.add_argument("--write-doc", action="store_true",
                    help="write assets/palettes.md")
    ap.add_argument("--check-doc", action="store_true",
                    help="fail if assets/palettes.md is stale")
    ap.add_argument("--unused", action="store_true",
                    help="list dictionary names no ramp/anchor references")
    args = ap.parse_args(argv)
    if args.unused:
        spare = unreferenced(COLORS, _RAMPS_RAW, _ANCHORS_RAW)
        if not spare:
            print("palette: every dictionary name is referenced")
        for section, names in spare.items():
            print(f"[{section}] unused: {', '.join(names)}")
        return 0
    if args.check_doc:
        want = render_doc() + "\n"
        got = DOC_PATH.read_text() if DOC_PATH.exists() else ""
        if got != want:
            print("palette doc: assets/palettes.md is stale "
                  "(run make manifest)")
            return 1
        print("palette doc: OK")
        return 0
    if args.write_doc:
        DOC_PATH.write_text(render_doc() + "\n")
        print(f"wrote {DOC_PATH.relative_to(REPO_ROOT)}")
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
