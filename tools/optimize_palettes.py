#!/usr/bin/env python3
"""optimize_palettes.py -- Exact palette-slot optimizer (no heuristics).

Question: can every art group (overworld sprites, battle art, NPC
overlays) be assigned to one of the 8 BASE + 8 OBJ hardware ramps with
ZERO art-pixel changes? Where exactly does the math bottom out?

Model (see docs/palette_repaint_requests.md for the human version):
  - A group = tiles sharing one palette assignment (one enemy's OW
    cells, one combat-art set, one NPC overlay cell). Each group
    carries its EXACT opaque color set read from the PNGs; the entry-0
    background (yellow = transparent for OAM, sheet bg for BG art) is
    added by kind at fit time.
  - A slot holds <= 4 colors. OBJ slots pin entry 0 to SPRITES yellow;
    entries sort by luminance for DMG grayscale.
  - World tilesets + fight_text are FROZEN (already exact / load-bearing
    UI ink). BASE slots 0/4/6 contents are frozen (shop/paper/text).
    Unused/duplicate ramp entries may be filled at zero visual cost.
  - Structural toggles (each with a code cost, all banked-ROM gated by
    `make memmap`): BG per-tile battle palettes, NPC OAM-ification
    (actor pipeline). OAM per-tile needs only a data-model change
    (every sprite already carries palette bits).
  - Card chrome (frames/icons) is EXCLUDED pending the design decision
    (uniform-slot vs ink vs legacy tint) -- reported as evidence only.

Art bytes are NEVER modified by this tool. Unplaceable groups are
reported with infeasibility certificates (e.g. "union is 5 colors,
max 4"), which is exactly the proven-minimal repaint list.

Usage:
    python3 tools/optimize_palettes.py            # solve + report
    python3 tools/optimize_palettes.py --variants # also npc-oam variant
    (read-only: prints the optimum, writes nothing)
"""

import sys
import json
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "screen_compiler"))

from palette_txt import RAMPS, RAMP_NAMES, OBJ_BY_SLOT, _NAMES, FULL_SLOTMAP  # noqa: E402
from palette_txt import BATTLE_RAMPS  # noqa: E402

import compose_battle_sprites  # noqa: E402
import compose_enemy_sprites  # noqa: E402
import compose_hero_sprites  # noqa: E402
import compose_npc_tiles  # noqa: E402

YELLOW = "#f1eb03"


def lum(h):
    r, g, b = int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16)
    return 0.299 * r + 0.587 * g + 0.114 * b


def ramp_colors(setkey, name):
    if setkey == "obj":
        table, names = OBJ_BY_SLOT, _NAMES["obj"]
    else:
        table, names = RAMPS[setkey], RAMP_NAMES[setkey]
    return ["#%02x%02x%02x" % c for c in table[names.index(name)]]


def cell_used(img, tx, ty):
    px = img.load()
    return set("#%02x%02x%02x" % px[tx * 8 + x, ty * 8 + y]
               for y in range(8) for x in range(8))


# ---------------------------------------------------------------- census

def census():
    """group -> dict(kind, opaque, cells, declared).
    kind: 'obj' or 'bg:<set>'. opaque = non-background colors."""
    groups = {}
    battle = Image.open(REPO_ROOT / "assets" / "battle_sprites.png").convert("RGB")
    enemy = Image.open(REPO_ROOT / "assets" / "enemy_sprites.png").convert("RGB")
    hero = Image.open(REPO_ROOT / "assets" / "hero_sprites.png").convert("RGB")
    npc = Image.open(REPO_ROOT / "assets" / "npc_tiles.png").convert("RGB")

    art_dir = REPO_ROOT / "screens" / "combat_art"
    for path in sorted(art_dir.glob("*.json")):
        data = json.loads(path.read_text())
        sid = data.get("id", path.stem)
        oam = bool(data.get("oam"))
        bg = YELLOW if oam else "#ffffff"
        cols, cells = set(), []
        for frame in ("frame0", "frame1"):
            for cell in data.get(frame) or []:
                if cell is None or cell not in compose_battle_sprites.TILE_COORDS:
                    continue
                cx, cy = compose_battle_sprites.TILE_COORDS[cell]
                cells.append((cx, cy, cell))
                cols |= {c for c in cell_used(battle, cx, cy) if c != bg}
        groups["battle:" + sid] = {
            "kind": "obj" if oam else "bg:base", "opaque": cols,
            "cells": cells, "oam": oam,
            "declared": data.get("obj_palette") if oam else data.get("palette"),
        }

    et_dir = REPO_ROOT / "screens" / "enemy_types"
    for path in sorted(et_dir.glob("*.json")):
        data = json.loads(path.read_text())
        ow = data.get("overworld", {})
        cols, cells = set(), []
        for cell in ow.get("cells", []):
            if cell not in compose_enemy_sprites.TILE_COORDS:
                continue
            cx, cy = compose_enemy_sprites.TILE_COORDS[cell]
            cells.append((cx, cy, cell))
            cols |= {c for c in cell_used(enemy, cx, cy) if c != YELLOW}
        groups["ow:" + data.get("id")] = {
            "kind": "obj", "opaque": cols, "cells": cells,
            "oam": True, "declared": ow.get("palette"),
        }

    hero_json = json.loads((REPO_ROOT / "screens" / "hero.json").read_text())
    ow = hero_json.get("overworld", {})
    cols, cells = set(), []
    for cell in ow.get("cells", []):
        cx, cy = compose_hero_sprites.TILE_COORDS[cell]
        cells.append((cx, cy, cell))
        cols |= {c for c in cell_used(hero, cx, cy) if c != YELLOW}
    groups["ow:hero"] = {"kind": "obj", "opaque": cols, "cells": cells,
                         "oam": True, "declared": ow.get("palette")}

    pals = ["town1", "town1", "town1", "town2", "town1", "town1"]
    for x, (name, pal) in enumerate(zip(compose_npc_tiles.LAYOUT, pals)):
        cols = {c for c in cell_used(npc, x, 0) if c != "#b6a27e"}
        groups["npc:" + name] = {
            "kind": "bg:village", "opaque": cols, "cells": [(x, 0, name)],
            "oam": False, "declared": pal,
        }
    return groups


def needed_colors(g):
    """Full color set incl. entry-0 background by kind."""
    if g["kind"] == "obj":
        return set(g["opaque"]) | {YELLOW}
    if g["kind"] == "bg:village":
        return set(g["opaque"]) | {"#b6a27e"}
    return set(g["opaque"]) | {"#ffffff"}


# ---------------------------------------------------------------- slots

def current_slots():
    """(setkey, slot) -> list-of-4-hex, skipping placeholder slots."""
    out = {}
    for setkey in ("base", "village"):
        for slot, name in sorted(FULL_SLOTMAP.get(setkey, {}).items()):
            try:
                out[(setkey, slot)] = ramp_colors(setkey, name)
            except ValueError:
                continue  # placeholder (e.g. village 'unused' canary)
    for slot, name in sorted(FULL_SLOTMAP.get("obj", {}).items()):
        out[("obj", slot)] = ramp_colors("obj", name)
    return out


# Frozen contents: shop/paper/text UI semantics (all of BASE -- card
# tints couple art to UI irrevocably), every village ramp (world tiles),
# and every OBJ ramp with a declared consumer (redefining would change a
# live look). Fills into TRULY dead entries (no passing tile uses them)
# are free; brand-new ramps go only to consumer-free slots.
FROZEN = {("base", s) for s in range(8)}
# Consumer-free slots (verified: zero declared consumers in JSONs).
FREE_SLOTS = [("obj", 2), ("village", 7)]
# Dead-entry fills (verified unused AND color-duplicated elsewhere in
# the ramp): currently none live (kobold moved to battle-time values).
FILLABLE = set()


def slot_entry_usage(groups, slots):
    """slotkey -> set of entry indices used by currently-passing groups."""
    use = {}
    for gname, g in groups.items():
        key = declared_slot(g, slots)
        if key is None:
            continue
        ramp = slots[key]
        need = needed_colors(g)
        if not need <= set(ramp):
            continue
        for c in need:
            use.setdefault(key, set()).add(ramp.index(c))
    return use


def declared_slot(g, slots):
    setkey = {"obj": "obj", "bg:base": "base",
              "bg:village": "village"}[g["kind"]]
    table = FULL_SLOTMAP.get(setkey, {})
    for slot, name in sorted(table.items()):
        if name == g["declared"] and (setkey, slot) in slots:
            return (setkey, slot)
    return None


def order_ramp(colors, entry0):
    """Deterministic ramp order: entry 0 = bg, rest by luminance."""
    rest = sorted([c for c in colors if c != entry0], key=lum, reverse=True)
    return [entry0] + rest


# ---------------------------------------------------------------- search

def solve_family(family, slots, free_keys, forced=None):
    """Exact B&B over group->slot partitions. forced: {name: slotkey}
    pins a group (for tie enumeration). Returns best dict with
    placed {name: (slotkey, how, detail)} plus exact/nodes flags."""
    names = sorted(family)
    names.sort(key=lambda n: -len(needed_colors(family[n])))
    best = {"placed": {}, "score": None}
    nodes = [0]
    NODE_CAP = 20000000
    exact = [True]

    def dead_entries(key):
        # Fillable only: same color elsewhere in the ramp (provably
        # redundant) AND unused by every passing tile. Entry 0 is
        # never fillable (transparency anchor / DMG lightest).
        ramp = slots[key]
        used = slot_used.get(key, set())
        return [i for i in range(1, 4)
                if ramp[i] in ramp[:i] + ramp[i + 1:]
                and i not in used]

    def cands(n):
        need = needed_colors(family[n])
        if len(need) > 4:
            return []  # pigeonhole: no 4-color slot can hold it
        out = []
        for key in sorted(slots):
            if need <= set(slots[key]):
                out.append((key, "keep", []))
            elif key in FILLABLE:
                missing = sorted(need - set(slots[key]))
                dead = dead_entries(key)
                if missing and len(missing) <= len(dead):
                    out.append((key, "fill", dead[:len(missing)]))
        for key in sorted(free_keys):
            out.append((key, "new", []))
        return out

    cand_map = {n: cands(n) for n in names}
    if forced:
        for n, key in forced.items():
            if n in cand_map:
                cand_map[n] = [c for c in cand_map[n] if c[0] == key]

    def score_of(placed):
        fills = sum(len(f) for _, h, f in placed.values() if h == "fill")
        new = sum(1 for _, h, _ in placed.values() if h == "new")
        moves = sum(1 for n, (k, _, _) in placed.items()
                    if declared_slot(family[n], slots) != k)
        return (len(placed), -fills, -new, -moves)

    def dfs(i, placed, occupied, filled_state):
        nodes[0] += 1
        if nodes[0] > NODE_CAP:
            exact[0] = False
            return
        if best["score"] is not None and \
                len(placed) + (len(names) - i) < best["score"][0]:
            return
        if i == len(names):
            s = score_of(placed)
            if best["score"] is None or s > best["score"]:
                best["score"] = s
                best["placed"] = dict(placed)
            return
        n = names[i]
        need = needed_colors(family[n])

        def try_one(key, how, detail):
            if how == "new" and key in occupied:
                return False
            if how == "keep":
                if not (occupied.get(key, set()) | need) <= set(slots[key]):
                    return False
            elif how == "fill":
                ramp = list(slots[key])
                state = filled_state.get(key, {})
                ok = True
                rep = dict(state)
                for idx, c in zip(detail, sorted(need - set(slots[key]))):
                    if idx in rep and rep[idx] != c:
                        ok = False
                        break
                    rep[idx] = c
                    ramp[idx] = c
                if not ok:
                    return False
                if not (occupied.get(key, set()) | need) <= set(ramp):
                    return False
            placed[n] = (key, how, detail)
            prev_occ = occupied.get(key, set())
            occupied[key] = prev_occ | need
            if how == "fill":
                prev_fill = filled_state.get(key, {})
                new_fill = dict(prev_fill)
                for idx, c in zip(detail, sorted(need - set(slots[key]))):
                    new_fill[idx] = c
                filled_state[key] = new_fill
                dfs(i + 1, placed, occupied, filled_state)
                if prev_fill:
                    filled_state[key] = prev_fill
                else:
                    filled_state.pop(key, None)
            else:
                dfs(i + 1, placed, occupied, filled_state)
            del placed[n]
            if prev_occ:
                occupied[key] = prev_occ
            else:
                occupied.pop(key, None)
            return True

        if forced and n in forced:
            # Pinned group: try the pin first so ties surface it.
            for key, how, detail in cand_map[n]:
                if key == forced[n]:
                    try_one(key, how, detail)
            dfs(i + 1, placed, occupied, filled_state)  # + skip branch
            return
        dfs(i + 1, placed, occupied, filled_state)  # leave unplaced
        for key, how, detail in cand_map[n]:
            try_one(key, how, detail)

    dfs(0, {}, {}, {})
    best["exact"] = exact[0]
    best["nodes"] = nodes[0]
    return best


def report_solution(label, family, slots, best, entry0, show_ramps=False):
    placed = best["placed"]
    print("== %s: placed %d/%d (%s, %d nodes) ==" %
          (label, len(placed), len(family),
           "EXACT optimum" if best.get("exact") else "BEST-EFFORT (cap hit)",
           best.get("nodes", 0)))
    for n in sorted(placed):
        key, how, detail = placed[n]
        extra = ""
        if how == "fill":
            need = needed_colors(family[n])
            missing = sorted(need - set(slots[key]))
            extra = " (fill entries %s with %s)" % (detail, missing)
        print("    %-18s -> %s slot %d [%s]%s" % (n, key[0], key[1], how, extra))
        if show_ramps and how in ("redefine", "new"):
            print("      ramp: %s" % order_ramp(needed_colors(family[n]), entry0))
    for n in sorted(set(family) - set(placed)):
        need = needed_colors(family[n])
        print("    %-18s UNPLACED union=%d %s" % (n, len(need), sorted(need)))
    print()


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.parse_args()

    groups = census()
    slots = current_slots()
    global slot_used
    slot_used = slot_entry_usage(groups, slots)

    print("== census: %d groups ==" % len(groups))
    for n in sorted(groups):
        g = groups[n]
        need = needed_colors(g)
        print("  %-18s %-9s union=%d declared=%s" %
              (n, g["kind"], len(need), g["declared"]))
    print()
    for setkey in ("obj", "base", "village"):
        have = sorted(s for (sk, s) in slots if sk == setkey)
        print("  %-7s slots %s (frozen contents: %s)" %
              (setkey, have,
               sorted(s for (sk, s) in FROZEN if sk == setkey)))
    print()

    obj_all = {n: g for n, g in groups.items() if g["kind"] == "obj"}
    # Battle-time pool: groups whose declared ramp lives in RAMPS/BATTLE
    # are programmed per battle entry (no static slot consumed). Placed
    # iff the union fits 4 (mimic/spider unions are 5 pre-fix: honest
    # unplaced with certificates).
    battle_fam = {n: g for n, g in obj_all.items()
                  if g["declared"] in BATTLE_RAMPS}
    obj_fam = {n: g for n, g in obj_all.items()
               if g["declared"] not in BATTLE_RAMPS}
    base_fam = {n: g for n, g in groups.items() if g["kind"] == "bg:base"}
    vil_fam = {n: g for n, g in groups.items() if g["kind"] == "bg:village"}
    obj_slots = {k: v for k, v in slots.items() if k[0] == "obj"}
    base_slots = {k: v for k, v in slots.items() if k[0] == "base"}
    vil_slots = {k: v for k, v in slots.items() if k[0] == "village"}

    print("== BATTLE-TIME (per-battle programming, no static slots) ==")
    for n in sorted(battle_fam):
        need = needed_colors(battle_fam[n])
        vals = BATTLE_RAMPS[battle_fam[n]["declared"]]
        ok = need <= set("#%02x%02x%02x" % c for c in vals)
        print("    %-18s union=%d %s" %
              (n, len(need), "EXACT" if ok else "UNPLACED (union fits no 4-color row)"))
    print()

    free_obj = [("obj", 2)]  # reassignable: sole consumer is dog-OW
    free_vil = [("village", 7)] if ("village", 7) not in slots else []

    b1 = solve_family(obj_fam, obj_slots, free_obj)
    report_solution("OBJ static (ow)", obj_fam, obj_slots, b1,
                    YELLOW, show_ramps=True)
    # NOTE: no tie remains. Dog@2-keep is the unique optimum (8 placed);
    # fire@2-new would evict dog's exact ramp for 7 placed. Mayor-as-OAM
    # needs the same slot 2 plus actor-pipeline code. The `forced`
    # parameter stays for manual what-if runs.
    b2 = solve_family(base_fam, base_slots, [])
    report_solution("BASE (battle BG art)", base_fam, base_slots, b2,
                    "#ffffff", show_ramps=True)
    b3 = solve_family(vil_fam, vil_slots, free_vil)
    report_solution("VILLAGE (npc overlays)", vil_fam, vil_slots, b3,
                    "#b6a27e", show_ramps=True)

    card_evidence()


def card_evidence():
    """Card chrome unions per skin slot (evidence for the design
    decision; chrome is excluded from the solve)."""
    import emit_sheet_sidecars as E
    img = Image.open(REPO_ROOT / "assets" / "card_frames.png").convert("RGB")
    names = {}
    for y, row in enumerate(E.compose_card_frames.LAYOUT):
        for x, name in enumerate(row):
            if name is not None:
                names[(x, y)] = name
    per_slot = {}
    slot_of = {v: k for k, v in FULL_SLOTMAP.get("base", {}).items()}
    for coord, rampname in sorted(E.CARD_RAMPS.items()):
        if coord not in names:
            continue
        tx, ty = coord
        slot = slot_of.get(rampname)
        if slot is None:
            continue
        per_slot.setdefault(slot, set()).update(
            cell_used(img, tx, ty) - {"#ffffff"})
    print("== card-chrome evidence (excluded from solve) ==")
    print("  (fit vs the skin slot's current ramp; HUD icons fixed-slot,")
    print("   hand icons inherit the card box tint = any of 8 slots)")
    for slot in sorted(per_slot):
        rampname = [k for k, v in slot_of.items() if v == slot][0]
        ramp = set(ramp_colors("base", rampname))
        cols = sorted(per_slot[slot])
        outside = [c for c in cols if c not in ramp]
        print("    BASE slot %d (%s): %s %s" %
              (slot, rampname,
               "FITS" if not outside else "misfit %s" % outside,
               cols))
    print()


slot_used = {}

if __name__ == "__main__":
    sys.exit(main() or 0)
