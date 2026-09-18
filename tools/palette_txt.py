#!/usr/bin/env python3
"""palette_txt.py -- Single source of truth for authored colors.

`assets/palette.txt` is artist-owned (tutorial:
`assets/palette_tutorial.md`) and holds:

1. COLOR dictionary (named sections): `name: #hex` entries.
2. RAMP lines: `name: SECTION/ref, ... (4 refs)` — bare or under
   `RAMPS/<SET>` headers. `UNUSED` is a valid ref meaning "repeat the
   ramp's own darkest shade".
3. `SLOTS/<SET>` slotmap (dev-seeded): `slot: rampname` pins ramp names
   to hardware indices. Tiles, art, UI and OBJ code consume fixed
   indices, so file order never matters — only the slotmap does.
4. `ANCHORS` (optional): `sheet: SECTION/name` (or raw `#hex`
   chroma-key). Absent -> previous defaults + notice.

The pipeline adapts to whatever the artist provides: inferred sets
(majority ref section), flexible counts (1..8 BG, 1..8 OBJ with 0..3 as
the overworld set and 4+ for battle), padded
slots (magenta BG canary / neutral grey OBJ), duplicated placeholders
for consumed-but-missing slots (fail loudly, render plausibly).
Over-count (>8/>4) and unresolvable refs fail; nothing is ever
silently truncated or dropped (unmapped ramps warn).

Consumers:
  tools/palette_compiler.py  FIXED_PALETTES / ANCHOR_COLORS / RAMP_NAMES /
                             REAL_SLOTS (matcher skips padded slots)
  tools/palette_check.py     hard-consumer validation, LOAD_ERRORS,
                             assets/palettes.md freshness
  assets/palettes.md         generated semantic tables (make manifest)
"""

from pathlib import Path
from typing import Dict, List, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
PALETTE_TXT = REPO_ROOT / "assets" / "palette.txt"
DOC_PATH = REPO_ROOT / "assets" / "palettes.md"

RGB = Tuple[int, int, int]
# Engine ramp sets (SLOTS/<SET> + inference targets).
RAMP_SETS = ("base", "forest", "desolate_landscape", "castle", "village")
# Short file headers the artist may use (RAMPS/DESOLATE, ...).
SET_ALIASES = {"desolate": "desolate_landscape"}
# Sheet keys for anchors.
ANCHOR_KEYS = ("forest", "desolate_landscape", "castle", "village", "title",
               "npc_tiles", "enemy_ow")
# Ref section -> inferred set (TITLE has no engine set: reported).
INFER_SET = {"COMMON": None, "GAMEPLAY": None, "FOREST": "forest",
             "DESOLATE": "desolate_landscape", "CASTLE": "castle",
             "TOWN": "village", "SPRITES": "obj", "COMBAT": "base",
             "TITLE": "title"}

MAGENTA: RGB = (255, 0, 255)
OBJ_PAD: RGB = (170, 170, 170)  # dev fallback grey (documented, never art)


def _hex_to_rgb(h: str, where: str) -> RGB:
    h = h.strip().lstrip("#")
    if len(h) != 6:
        raise ValueError(f"{where}: expected 6-digit hex, got '{h}'")
    try:
        return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
    except ValueError:
        raise ValueError(f"{where}: invalid hex color '{h}'")


def _parse_ref(ref: str, where: str) -> Tuple[str, str]:
    parts = ref.split("/")
    if len(parts) != 2 or not parts[0].strip() or not parts[1].strip():
        raise ValueError(
            f"{where}: bad reference '{ref}' (want SECTION/name)")
    return parts[0].strip(), parts[1].strip()


def _is_ramp_value(value: str) -> bool:
    parts = [r.strip() for r in value.split(",")]
    return (len(parts) == 4 and all(
        p == "UNUSED" or "/" in p for p in parts))


def load_palette(path: Path = PALETTE_TXT):
    """Parse palette.txt.

    Returns (colors, ramp_entries, slotmap, anchors, errors, warnings).
    colors: {SECTION: {name: rgb}}.
    ramp_entries: [(setkey|None, rampname, [refs], lineno)] in file order
      (None = inferred homeless like TITLE).
    slotmap: {setkey: {slot: rampname}}.
    anchors: {sheetkey: ref-or-#hex}.
    errors: fatal-by-checker list (bad refs, ties, slotmap problems).
    warnings: non-fatal list (unmapped ramps, homeless ramps, defaults).
    Structural violations (slot range, dup slots, >8 ramps) raise.
    """
    colors: Dict[str, Dict[str, RGB]] = {}
    pending: List[Tuple[str, List[str], int]] = []
    headed: Dict[str, List[Tuple[str, List[str], int]]] = {}
    slotmap: Dict[str, Dict[int, str]] = {}
    anchors: Dict[str, str] = {}
    errors: List[str] = []
    warnings: List[str] = []
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
                setkey = SET_ALIASES.get(current[len("RAMPS/"):].lower(),
                                         current[len("RAMPS/"):].lower())
                headed.setdefault(setkey, [])
            elif current.startswith("SLOTS/"):
                setkey = SET_ALIASES.get(current[len("SLOTS/"):].lower(),
                                         current[len("SLOTS/"):].lower())
                if setkey in slotmap:
                    raise ValueError(
                        f"{where}: duplicate slotmap '{setkey}'")
                slotmap[setkey] = {}
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
        if current.startswith("SLOTS/"):
            setkey = SET_ALIASES.get(current[len("SLOTS/"):].lower(),
                                     current[len("SLOTS/"):].lower())
            try:
                slot = int(name)
            except ValueError:
                raise ValueError(
                    f"{where}: slotmap key '{name}' is not a slot number")
            maxslot = 7
            if not (0 <= slot <= maxslot):
                raise ValueError(
                    f"{where}: slot {slot} out of 0..{maxslot} for {setkey}")
            if slot in slotmap[setkey]:
                raise ValueError(
                    f"{where}: duplicate slot {slot} in SLOTS/{setkey}")
            slotmap[setkey][slot] = value
        elif current.startswith("RAMPS/"):
            setkey = SET_ALIASES.get(current[len("RAMPS/"):].lower(),
                                     current[len("RAMPS/"):].lower())
            refs = [r.strip() for r in value.split(",")]
            if len(refs) != 4 or any(not r for r in refs):
                raise ValueError(
                    f"{where}: ramp '{name}' needs exactly 4 refs")
            headed[setkey].append((name, refs, lineno))
        elif current == "ANCHORS":
            if value.startswith("#"):
                _hex_to_rgb(value, where)
            else:
                _parse_ref(value, where)
            if name in anchors:
                raise ValueError(f"{where}: duplicate anchor '{name}'")
            anchors[name] = value
        elif _is_ramp_value(value):
            refs = [r.strip() for r in value.split(",")]
            pending.append((name, refs, lineno))
        else:
            rgb = _hex_to_rgb(value, where)
            if name in colors[current]:
                raise ValueError(
                    f"{where}: duplicate '{name}' in [{current}]")
            colors[current][name] = rgb

    # Infer sets for bare ramp lines (majority ref section, UNUSED skipped).
    inferred: Dict[str, List[Tuple[str, List[str], int]]] = {}
    for name, refs, lineno in pending:
        votes: Dict[str, int] = {}
        for r in refs:
            if r == "UNUSED":
                continue
            sec, _ = _parse_ref(r, f"{path}:{lineno}")
            votes[sec] = votes.get(sec, 0) + 1
        if not votes:
            errors.append(f"{path}:{lineno}: ramp '{name}' has no "
                          f"resolvable refs (all UNUSED)")
            continue
        top = max(votes.values())
        winners = [s for s, n in votes.items() if n == top]
        if len(winners) != 1 or winners[0] not in INFER_SET:
            errors.append(f"{path}:{lineno}: ramp '{name}' is ambiguous "
                          f"({', '.join(sorted(votes))}) — move it under an "
                          f"explicit RAMPS/<SET> header")
            continue
        target = INFER_SET[winners[0]]
        if target is None:
            warnings.append(f"ramp '{name}' infers to [{winners[0]}]: no "
                            f"engine home (splash palette is fixed) — kept "
                            f"for documentation")
            continue
        if target == "title":
            warnings.append(f"ramp '{name}' infers to [TITLE]: no slot home "
                            f"(title art resolves via --strict-ramp) — kept "
                            f"for documentation")
        inferred.setdefault(target, []).append((name, refs, lineno))
    for setkey, entries in headed.items():
        inferred.setdefault(setkey, []).extend(entries)
    for setkey, entries in inferred.items():
        seen_names = set()
        for n, _, ln in entries:
            if n in seen_names:
                raise ValueError(
                    f"{path}:{ln}: duplicate ramp '{n}' in set '{setkey}'")
            seen_names.add(n)
        # Over-count never raises: the slotmap places at most hardware
        # slots and every unmapped ramp warns (never silently dropped).

    if not anchors:
        warnings.append("no ANCHORS section: using previous defaults "
                        "(forest FOREST/grass, desolate DESOLATE/ground, "
                        "castle CASTLE/ground, village TOWN/ground, title "
                        "TITLE/title_bg, npc SPRITES/background, enemy "
                        "DESOLATE/ground)")
        anchors = {"forest": "FOREST/grass",
                   "desolate_landscape": "DESOLATE/ground",
                   "castle": "CASTLE/ground",
                   "village": "TOWN/ground",
                   "title": "TITLE/title_bg",
                   "npc_tiles": "SPRITES/background",
                   "enemy_ow": "DESOLATE/ground"}
    for key in ANCHOR_KEYS:
        if key not in anchors:
            raise ValueError(f"{path}: missing ANCHORS entry '{key}'")
    return colors, inferred, slotmap, anchors, errors, warnings


def _resolve(colors, ref: str, where: str,
             errors: List[str]) -> RGB | None:
    if ref == "UNUSED":
        return None
    try:
        section, name = _parse_ref(ref, where)
        return colors[section][name]
    except (KeyError, ValueError) as e:
        errors.append(f"{where}: unresolvable reference '{ref}' ({e})")
        return None


def hard_consumers() -> Dict[str, Dict[int, List[str]]]:
    """Explicit engine index consumers: {set: {slot: [descriptions]}}.

    Derived from content (not opinion): explicit tile ramps (tileset
    JSONs), village npc_pals (tiles_content.c), combat-art palettes,
    enemy/hero ow_palettes, and the UI base set (all 8). All ramp
    references are names resolved through the slotmap (seeded + auto).
    """
    import json as _json
    import re as _re
    out: Dict[str, Dict[int, List[str]]] = {s: {} for s in
                                            list(RAMP_SETS) + ["obj"]}
    # Tiles name their ramps explicitly; floor defaults are validated
    # against the slotmap like everything else. No guessing anywhere.
    # Name -> slot across seeded + auto-assigned slotmaps.
    _slot_of_name = {}
    for _set, _sm in FULL_SLOTMAP.items():
        for _slot, _rname in sorted(_sm.items()):
            _slot_of_name.setdefault(_set, {}).setdefault(_rname, _slot)

    def _slot(_set, _name, _where):
        try:
            return _slot_of_name[_set][_name]
        except KeyError:
            raise ValueError(f"{_where}: ramp '{_name}' has no slot")
    src = (REPO_ROOT / "src" / "game" / "tiles_content.c").read_text()
    m = _re.search(r"npc_pals\[6\]\s*=\s*\{([^}]*)\}", src)
    if m:
        for i, v in enumerate(_re.findall(r"\d+", m.group(1))):
            out["village"].setdefault(int(v), []).append(
                f"npc overlay {i}")
    ts_dir = REPO_ROOT / "tools" / "level_editor" / "tilesets"
    ts_key = {"forest": "forest", "castle": "castle",
              "desolate_landscape": "desolate_landscape",
              "village": "village"}
    for f in sorted(ts_dir.glob("*.json")):
        key = ts_key.get(f.stem)
        if key is None:
            continue
        try:
            tiles = _json.loads(f.read_text()).get("tiles", [])
        except ValueError:
            continue
        for t in tiles:
            if isinstance(t, dict) and t.get("palette") is not None:
                try:
                    pal = _slot(key, t["palette"], f.stem)
                except ValueError:
                    continue
                out[key].setdefault(pal, []).append(
                    f"editor tile {t.get('id', '?')}")
    art_dir = REPO_ROOT / "screens" / "combat_art"
    for f in sorted(art_dir.glob("*.json")):
        try:
            pal = _slot("base", _json.loads(f.read_text())["palette"],
                        f.stem)
            out["base"].setdefault(pal, []).append(
                f"battle art {f.stem}")
        except (KeyError, ValueError):
            pass
    ety_dir = REPO_ROOT / "screens" / "enemy_types"
    for f in sorted(ety_dir.glob("*.json")):
        try:
            pal = _slot("obj", _json.loads(f.read_text())["overworld"]
                        ["palette"], f.stem)
            out["obj"].setdefault(int(pal), []).append(
                f"ow sprite {f.stem}")
        except (KeyError, ValueError, TypeError):
            pass
    try:
        _hp = _slot("obj", _json.loads(
            (REPO_ROOT / "screens" / "hero.json").read_text())
            ["overworld"]["palette"], "hero")
        out["obj"].setdefault(int(_hp), []).append("hero ow sprite")
    except (KeyError, ValueError, TypeError):
        pass
    for i in range(8):
        out["base"].setdefault(i, []).append("UI_COLOR_* card/UI spans")
    return out


COLORS, _RAMPS_RAW, _SLOTMAP, _ANCHORS_RAW, LOAD_ERRORS, LOAD_WARNINGS = \
    load_palette()
SECTIONS = COLORS


def _place_slots():
    """Seeded slotmap plus auto-assignments (exhaustive name -> slot).

    Returns (full, unmapped, errors, warnings). Unmapped ramps warn;
    a full set errors naming every leftover ramp.
    """
    full, unmapped, errors, warnings = {}, {}, [], []
    for setkey in list(RAMP_SETS) + ["obj"]:
        entries = [n for n, _, _ in _RAMPS_RAW.get(setkey, [])]
        sm = dict(_SLOTMAP.get(setkey, {}))
        nslots = 8
        placed = {r for r in sm.values() if r in entries}
        for rname in entries:
            if rname not in placed:
                free = [s for s in range(nslots) if s not in sm]
                if not free:
                    continue
                sm[free[0]] = rname
                placed.add(rname)
                warnings.append(
                    f"ramp '{rname}' ({setkey}) auto-assigned to slot "
                    f"{free[0]}: pin it in SLOTS/{setkey.upper()} to "
                    f"make the placement deliberate")
        left = [r for r in entries if r not in placed]
        for r in left:
            unmapped.setdefault(setkey, []).append(r)
        if left and not [s for s in range(nslots) if s not in sm]:
            errors.append(
                f"set '{setkey}' is full (hardware fits {nslots}): "
                f"{', '.join(left)} have no slot — pick which "
                f"{nslots} ship (FLORENT)")
        full[setkey] = sm
    return full, unmapped, errors, warnings


FULL_SLOTMAP, _UNMAPPED_AUTO, _PLACE_ERRORS, _PLACE_WARNINGS = \
    _place_slots()
LOAD_ERRORS.extend(_PLACE_ERRORS)
LOAD_WARNINGS.extend(_PLACE_WARNINGS)


def _build_tables():
    # Fresh lists: placement messages already live in LOAD_ERRORS /
    # LOAD_WARNINGS; doubling them here prints everything twice.
    errors: list = []
    warnings: list = []
    tables: Dict[str, list] = {}
    names: Dict[str, list] = {}
    real: Dict[str, list] = {}
    dups: Dict[str, dict] = {}
    by_name: Dict[str, Dict[str, Tuple[List[str], int]]] = {}
    for setkey, entries in _RAMPS_RAW.items():
        by_name[setkey] = {n: (refs, ln) for n, refs, ln in entries}
    consumers = hard_consumers()
    for setkey in list(RAMP_SETS) + ["obj"]:
        entries = by_name.get(setkey, {})
        sm = FULL_SLOTMAP.get(setkey, {})
        nslots = 8
        # Validate slotmap: refs exist and sit in this set.
        for slot, rname in sorted(sm.items()):
            if rname not in entries:
                errors.append(
                    f"SLOTS/{setkey} slot {slot}: ramp '{rname}' is not "
                    f"in this set (rename, move, or fix the slotmap)")
        if len(sm) != len(set(sm.values())):
            errors.append(f"SLOTS/{setkey}: two slots share a ramp")
        # Resolve every ramp first (mapped or not) so bad refs always
        # surface; UNUSED fills with the ramp's own darkest real shade.
        solved: Dict[str, list] = {}
        for rname, (refs, ln) in entries.items():
            vals = []
            ok = True
            for r in refs:
                if r == "UNUSED":
                    vals.append(None)
                    continue
                v = _resolve(COLORS, r, f"{PALETTE_TXT}:{ln}", errors)
                vals.append(v)
                if v is None:
                    ok = False
            if not ok:
                continue
            shades = [v for v in vals if v is not None]
            if not shades:
                errors.append(
                    f"ramp '{rname}' ({setkey}) is all UNUSED — author "
                    f"at least one real shade")
                continue
            # Darkest by luminance (tuple order is meaningless for
            # color: white would wrongly win every max()).
            dark = min(shades,
                       key=lambda c: 0.299 * c[0] + 0.587 * c[1]
                       + 0.114 * c[2])
            solved[rname] = [v if v is not None else dark for v in vals]
        # Placed ramps resolve through the slotmap; broken ones magenta.
        resolved: Dict[int, list] = {}
        broken = set()
        for slot, rname in sorted(sm.items()):
            if rname not in entries:
                continue
            if rname not in solved:
                broken.add(slot)
                continue
            resolved[slot] = solved[rname]
        # Missing consumed slots duplicate the smallest real slot (loud).
        cand = sorted(resolved)
        for slot in sorted(consumers.get(setkey, {})):
            if slot not in resolved and slot < nslots:
                if not cand:
                    errors.append(
                        f"slot {slot} ({setkey}) consumed by "
                        f"{', '.join(consumers[setkey][slot])} but no "
                        f"ramp is mapped — author one in SLOTS/{setkey.upper()}")
                    continue
                src = cand[0]
                resolved[slot] = list(resolved[src])
                srcname = next(r for s, r in sm.items() if s == src)
                dups.setdefault(setkey, {})[slot] = (srcname, consumers[
                    setkey][slot])
        # Pad the rest (magenta BG canary / neutral grey OBJ). Battle
        # OBJ ramps may pin slots past the overworld 4 (e.g. slot 4+);
        # those ship too -- only truly-mapped slots are matchable.
        full, nms, rl = [], [], []
        slot_names = dict(sm)
        for slot, (srcname, _) in dups.get(setkey, {}).items():
            slot_names.setdefault(slot, srcname)
        topslot = nslots - 1
        if resolved:
            topslot = max(topslot, max(resolved))
        for slot in range(topslot + 1):
            if slot in resolved:
                full.append(resolved[slot])
                nms.append(slot_names[slot])
                # Only truly-mapped slots are matchable; duplicated
                # placeholders serve their hard consumers alone.
                if slot in sm:
                    rl.append(slot)
            else:
                if setkey == "obj":
                    # Dev fallback grey (documented); real OBJ slots always
                    # come from the artist file.
                    full.append([(255, 255, 255), (170, 170, 170),
                                 (85, 85, 85), (0, 0, 0)])
                else:
                    full.append([MAGENTA] * 4)
                nms.append("unused")
        tables[setkey] = full
        names[setkey] = nms
        real[setkey] = rl
        for slot in sorted(broken):
            errors.append(
                f"slot {slot} ({setkey}): ramp has unresolvable refs — "
                f"renders magenta until fixed")
            tables[setkey][slot] = [MAGENTA] * 4
    # Unmapped reporting is owned by _place_slots; mirror it so the
    # UNMAPPED export stays complete.
    return tables, names, real, dups, dict(_UNMAPPED_AUTO), errors, \
        warnings


def _hx(rgb) -> str:
    return f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"


_TABLES, _NAMES, REAL_SLOTS, DUPLICATES, UNMAPPED, BUILD_ERRORS, \
    BUILD_WARNINGS = _build_tables()
# _place_slots() already computed the exhaustive map; _build_tables()
# only validates against it.
RAMPS = {s: _TABLES[s] for s in RAMP_SETS}
# OBJ keeps ramp names aligned with the ui.c OBJ order.
OBJ_RAMPS = {}
for _slot, _rname in enumerate(_NAMES["obj"]):
    OBJ_RAMPS[_rname] = _TABLES["obj"][_slot]
# Positional OBJ tables (ui.c programs OAM slots in fixed order: 0..3
# overworld, 4+ battle).
OBJ_BY_SLOT = list(_TABLES["obj"])
RAMP_NAMES = {s: list(_NAMES[s]) for s in RAMP_SETS}
# Title ramps have no slot home (title art resolves via --strict-ramp),
# but they resolve exactly like other ramps: UNUSED fills with the ramp's
# own darkest real shade (same rule as _build_tables).
TITLE_RAMPS = {}
for _n, _refs, _ln in _RAMPS_RAW.get("title", []):
    _vals, _ok = [], True
    for _r in _refs:
        if _r == "UNUSED":
            _vals.append(None)
            continue
        _v = _resolve(COLORS, _r, f"{PALETTE_TXT}:{_ln}", [])
        if _v is None:
            _ok = False
            break
        _vals.append(_v)
    if not _ok:
        raise ValueError(f"{PALETTE_TXT}:{_ln}: title ramp '{_n}' has "
                         f"unresolvable refs")
    _shades = [v for v in _vals if v is not None]
    if not _shades:
        raise ValueError(f"{PALETTE_TXT}:{_ln}: title ramp '{_n}' is all UNUSED")
    _dark = min(_shades,
                key=lambda c: 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2])
    TITLE_RAMPS[_n] = [v if v is not None else _dark for v in _vals]


def build_anchors(colors, anchors) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for key in ANCHOR_KEYS:
        value = anchors[key]
        if value.startswith("#"):
            out[key] = value.lower()
        else:
            section, name = _parse_ref(value, "ANCHORS")
            try:
                r, g, b = colors[section][name]
            except KeyError:
                LOAD_ERRORS.append(
                    f"ANCHORS '{key}': reference '{value}' is not defined")
                continue
            out[key] = f"#{r:02x}{g:02x}{b:02x}"
    return out


ANCHORS = build_anchors(COLORS, _ANCHORS_RAW)

# What each slot serves (engine-side docs for the generated tables).
RAMP_USES = {
    "base": ["card UI, spider art", "burn cards, kobold art",
             "sword/freeze cards", "slime art, heal cards",
     "poison cards", "shield cards, mimic art",
              "text (black ink)", "boss/bat art"],
    "forest": ["unused", "forest fires", "unused",
               "field/ground + canopy", "unused",
               "trunks/stumps/merchant (override)", "unused", "rocks"],
    "desolate_landscape": ["dark ground (auto)", "campfire", "unused",
                           "unused", "unused", "unused",
                           "chest (MISSING)", "slate ground"],
    "castle": ["stone", "curtains", "unused", "unused", "unused",
               "furniture (MISSING)", "gold/chest", "unused"],
    "village": ["unused", "braziers", "unused", "dirt ground (MISSING)",
                "unused", "houses/merchant", "walls/mayor", "unused"],
}

_OBJ_USES = {
    "grey": "UNUSED slot (no grey ramp shipped)",
    "more_sprites": "town braziers (OBJ 1)",
    "sprites_again": "kobolds/dogs/hero (OBJ 2)",
    "sprites": "overworld slimes (OBJ 3)",
    "battle_slime": "battle slimes OAM pilot (OBJ 4)",
    "unused": "UNUSED slot",
    "even_more_sprites": "UNMAPPED (mage blue, no slot)",
    "sprites_still": "UNMAPPED (no slot)",
}


def render_doc() -> str:
    """Render assets/palettes.md from the canonical tables."""
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
            refs = _slot_refs(ts, i)
            uses = RAMP_USES[ts][i] if i < len(RAMP_USES[ts]) else ""
            L.append(f"| {i} | {RAMP_NAMES[ts][i]} | {hexes} | "
                     f"{uses} | {refs} |")
        L.append("")
    L.append("## OBJ sprite ramps (`src/ui/ui.c`)")
    L.append("")
    L.append("| Name | S0 | S1 | S2 | S3 | Serves | Refs |")
    L.append("|------|----|----|----|----|--------|------|")
    for i, key in enumerate(_NAMES["obj"]):
        ramp = _TABLES["obj"][i]
        hexes = " | ".join(f"`{_hx(c)}`" for c in ramp)
        L.append(f"| {key} | {hexes} | {_OBJ_USES.get(key, '')} | "
                 f"{_slot_refs('obj', i)} |")
    L.append("")
    L.append("## Sheet background convention (index 0 of the tile's ramp → shade 0)")
    L.append("")
    L.append("No tool pins a color to shade 0 by guessing: every tile encodes")
    L.append("through its explicitly assigned ramp (tileset JSON `palette`,")
    L.append("combat-art `palette`/`obj_palette`, enemy `overworld.palette`),")
    L.append("and entry 0 of that ramp is shade 0. Sheet background cells use")
    L.append("the ramp's entry-0 color (`ANCHORS` below records the convention")
    L.append("per sheet for the artist). Off-ramp pixels fail loudly.")
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


def _slot_refs(setkey: str, slot: int) -> str:
    if setkey in DUPLICATES and slot in DUPLICATES[setkey]:
        src, consumers = DUPLICATES[setkey][slot]
        return (f"duplicates `{src}` (FLORENT: author a real ramp for "
                f"{', '.join(consumers)})")
    rname = _NAMES[setkey][slot]
    if rname == "unused":
        return "UNUSED slot (magenta canary)" if setkey != "obj" \
            else "UNUSED slot (grey fallback)"
    auto = "" if slot in _SLOTMAP.get(setkey, {}) else " (auto-assigned)"
    for n, refs, _ in _RAMPS_RAW.get(setkey, []):
        if n == rname:
            return ", ".join(refs) + auto
    return rname + auto


def unreferenced(colors, ramps_raw, anchors_raw) -> Dict[str, List[str]]:
    """Dictionary names no ramp/anchor references (artist visibility)."""
    used = set()
    for entries in ramps_raw.values():
        for _, refs, _ in entries:
            for r in refs:
                if r == "UNUSED":
                    continue
                used.add(_parse_ref(r, "ramps"))
    for value in anchors_raw.values():
        if not value.startswith("#"):
            used.add(_parse_ref(value, "anchors"))
    out: Dict[str, List[str]] = {}
    for section, names in colors.items():
        spare = sorted(n for n in names
                       if (section, n) not in
                       {(s, m) for s, m in used})
        if spare:
            out[section] = spare
    return out


def emit_c_tables(out_dir) -> list:
    """Emit generated C palette tables (included by tiles_content.c/ui.c).

    Returns written paths. Deterministic: identical inputs produce
    byte-identical outputs.
    """
    from pathlib import Path as _P
    out = _P(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    syms = {s: ("cgb_bg_palettes" if s == "base" else
                   f"cgb_bg_palettes_{s}") for s in RAMP_SETS}
    lines = ["/* Generated by tools/palette_txt.py --write-tables.",
             " * Battle/UI + overworld CGB BG sets. Do not edit by hand. */"]
    for setkey in RAMP_SETS:
        lines.append(f"/* {setkey}: " +
                     ", ".join(f"{i}={n}" for i, n in
                               enumerate(_NAMES[setkey])) + " */")
        body = []
        for ramp in RAMPS[setkey]:
            body.append("    { " + ", ".join(
                "RGB8(%d,%d,%d)" % tuple(c) for c in ramp) + " },")
        lines.append(f"const palette_color_t {syms[setkey]}[8][4] = {{")
        lines.extend(body)
        lines.append("};")
    bg_path = out / "cram_tables.h"
    bg_path.write_text("\n".join(lines) + "\n")
    olines = ["/* Generated by tools/palette_txt.py --write-tables.",
              " * CGB OBJ ramps (OAM slots 0..7; 0..3 overworld, 4+ battle). Do not edit by hand. */"]
    osyms = ["cgb_sprite_palette", "cgb_sprite_palette_orange",
             "cgb_sprite_palette_brown", "cgb_sprite_palette_green"]
    for _extra in range(len(osyms), len(OBJ_BY_SLOT)):
        osyms.append("cgb_sprite_palette_obj%d" % _extra)
    for i, sym in enumerate(osyms):
        ramp = OBJ_BY_SLOT[i]
        olines.append(f"/* OBJ {i}: {_NAMES['obj'][i]} */")
        olines.append(f"static const palette_color_t {sym}[4] = {{ " +
                      ", ".join("RGB8(%d,%d,%d)" % tuple(c)
                                for c in ramp) + " };")
    obj_path = out / "obj_tables.h"
    obj_path.write_text("\n".join(olines) + "\n")
    return [bg_path, obj_path]


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Render assets/palettes.md")
    ap.add_argument("--write-doc", action="store_true",
                    help="write assets/palettes.md")
    ap.add_argument("--check-doc", action="store_true",
                    help="fail if assets/palettes.md is stale")
    ap.add_argument("--unused", action="store_true",
                    help="list dictionary names no ramp/anchor references")
    ap.add_argument("--write-tables", metavar="DIR",
                    help="emit generated C tables into DIR")
    args = ap.parse_args(argv)
    if args.unused:
        spare = unreferenced(COLORS, _RAMPS_RAW, _ANCHORS_RAW)
        if not spare:
            print("palette: every dictionary name is referenced")
        for section, names in spare.items():
            print(f"[{section}] unused: {', '.join(names)}")
        if UNMAPPED:
            print("unmapped ramps (no slot, never shipped):")
            for setkey, names in UNMAPPED.items():
                print(f"  {setkey}: {', '.join(names)}")
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
    if args.write_tables:
        for p in emit_c_tables(args.write_tables):
            print(f"wrote {p}")
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
