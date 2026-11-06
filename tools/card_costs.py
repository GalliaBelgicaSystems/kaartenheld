#!/usr/bin/env python3
"""make card-costs gate (docs/card-ap-cost-formula.md §9, required).

Recomputes the Card AP formula over every legal (material, weapon,
effect) combo from the ROM's own constants (`src/rpg/loot.h`) and
fails loudly on any violation:

1. §6.1 gate: cost(material, weapon, effect) <= gate_ap, where gate_ap
   is LOOT_AP_MYTHRIL_RIDER for mythril riders and the material gate
   otherwise.  Catches loot-table edits that no scenario happens to
   roll.
2. §6.3/§7.5 ordering: pairwise no-inversion (B dominates A on all
   inputs with >= 1 strict => cost(B) >= cost(A)) plus strict riders
   (every rider pair costs exactly one AP more than the same card
   plain).  The fusion contract.
3. Table parity: the recomputed §4.1/§4.2 cells must match the
   markdown tables in docs/card-ap-cost-formula.md, and every
   catalog row's CARD_AP(...) call must evaluate to its §4.3 value.
   Tables that rot fail here, not in a playtest.

Stdlib only.  Exit 0 = green, 1 = violation (with expected/actual).
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOOT_H = REPO / "src" / "rpg" / "loot.h"
FORMULA_DOC = REPO / "docs" / "card-ap-cost-formula.md"
CARDS_C = REPO / "src" / "game" / "cards_content.c"

MATERIALS = ["wood", "bronze", "iron", "steel", "mythril"]
WEAPONS_41 = ["sword", "shield", "bow", "dagger", "ring"]  # §4.1 columns
WEAPONS_42 = [("F", "sword"), ("I", "sword"), ("P", "dagger")]  # §4.2 columns
# (effect label, weapon) for the only legal rider pairs (docs/loot.md §34.2)
RIDER_PAIRS = [("burn", "sword"), ("freeze", "sword"), ("poison", "dagger")]

FAILURES = []


def _game_card_count():
    """GAME_CARD_COUNT from src/game/game_ids.h (single source of truth
    for the curated catalog size)."""
    m = re.search(r"#define\s+GAME_CARD_COUNT\s+(\d+)",
                  (REPO / "src" / "game" / "game_ids.h").read_text(
                      encoding="utf-8"))
    if not m:
        fail("identity: GAME_CARD_COUNT not found in game_ids.h")
        return 0
    return int(m.group(1))


def fail(msg):
    FAILURES.append(msg)


def parse_define_int(text, name):
    m = re.search(r"#define\s+%s\s+(\d+)" % re.escape(name), text)
    if not m:
        fail("loot.h: #%s not found (header shape changed?)" % name)
        return None
    return int(m.group(1))


def band_power(mat, weapon):
    """Mirror of LOOT_POWER (docs/loot.md §34.1)."""
    if mat == 0:
        return 1
    return 2 * mat + (1 if weapon in ("sword", "bow") else 0)


def card_ap(power, mat, rider, pts):
    prem = pts["mythril"] if mat == 4 else 0
    return (power + pts["material"] * mat + prem + pts["rider"] * rider + 7) // 8


def gate_ap(mat, rider, gates):
    if mat == 4 and rider:
        return gates["mythril_rider"]
    if mat == 0:
        return 1
    if mat == 1:
        return gates["bronze"]
    if mat == 2:
        return gates["iron"]
    return gates["steel"]


def parse_doc_tables():
    """Parse §4.1/§4.2/§4.3 markdown tables.

    Returns (cells, catalog) where cells is a list of
    (mat, weapon, rider, ap, power) — a list, not a dict, because F SW
    and I SW share (mat, weapon, rider) — and catalog maps ROW name to
    ((power, mat, rider), expected_ap), with AMULET mapping to (None, 0).
    """
    text = FORMULA_DOC.read_text(encoding="utf-8")
    cells = []
    # §4.1 rows: | **wood** (gate AP 1) | 1 AP (p1) | ... (6 cols)
    sec41 = text.split("### 4.1", 1)[1].split("### 4.2", 1)[0]
    for m in re.finditer(
            r"\|\s*\*\*(\w+)\*\*\s*\(gate AP \d+\)((?:\s*\|\s*(?:\d+ AP \(p\d+\)|n/a))+) \|",
            sec41):
        mat_name = m.group(1)
        if mat_name not in MATERIALS:
            continue
        mat = MATERIALS.index(mat_name)
        entries = re.findall(r"(\d+) AP \(p(\d+)\)|n/a", m.group(2))
        # only §4.1 rows have 5 weapon cells; §4.2 rows have 3
        if len(entries) != 5:
            continue
        for weapon, ent in zip(WEAPONS_41, entries):
            if ent[0] == "":
                continue
            cells.append((mat, weapon, 0, int(ent[0]), int(ent[1])))
    # §4.2 rows: same shape, 3 cells
    sec42 = text.split("### 4.2", 1)[1].split("### 4.3", 1)[0]
    for m in re.finditer(
            r"\|\s*\*\*(\w+)\*\*\s*\(gate AP [\d, riders ]+\)((?:\s*\|\s*(?:\d+ AP \(p\d+\)|n/a))+) \|",
            sec42):
        mat_name = m.group(1)
        if mat_name not in MATERIALS:
            continue
        mat = MATERIALS.index(mat_name)
        entries = re.findall(r"(\d+) AP \(p(\d+)\)|n/a", m.group(2))
        if len(entries) != 3:
            continue
        for ( _fx, weapon), ent in zip(WEAPONS_42, entries):
            if ent[0] == "":
                continue
            cells.append((mat, weapon, 1, int(ent[0]), int(ent[1])))
    # §4.3 rows: | Name | `ROW` | power / mat / rider | **X** | today |
    sec43 = text.split("### 4.3", 1)[1].split("## 5.", 1)[0]
    catalog = {}
    for m in re.finditer(
            r"\|\s*[^|]+\|\s*`([A-Z ]+)`\s*\|\s*(\d+)\s*/\s*(\w+)\s*/\s*([^|]+?)\|\s*\*\*(\d+)\*\*",
            sec43):
        row, power, mat_name, rider_txt, expected = m.groups()
        row = row.strip()
        rider = 0 if rider_txt.strip() in ("—", "-", "") else 1
        catalog[row] = ((int(power), MATERIALS.index(mat_name), rider),
                        int(expected))
    # SPECIAL rows (quest items: AMULET, BONE, OLD KEY) carry no
    # power/material triple ("SPECIAL, not playable").
    for m in re.finditer(
            r"\|\s*[^|]+\|\s*`([A-Z ]+)`\s*\|\s*SPECIAL, not playable\s*\|\s*\*\*(\d+)\*\*",
            sec43):
        catalog[m.group(1).strip()] = (None, int(m.group(2)))
    return cells, catalog


def parse_loot_power_guard(loot_h_text):
    """LOOT_POWER is the band table's single source of truth; band_power()
    mirrors it in Python.  Parse-guard the macro's canonical closed form so
    a band edit that changes the shape fails loudly here instead of silently
    diverging from the mirrored function."""
    m = re.search(r"#define\s+LOOT_POWER\([^)]*\)\s*(.*?)\n\n", loot_h_text, re.S)
    if not m:
        fail("loot.h: LOOT_POWER macro not found (band table moved?)")
        return
    body = m.group(1)
    for token in ("<< 1", "WPN_SWORD", "WPN_BOW", "? 1 :"):
        if token not in body:
            fail("loot.h: LOOT_POWER no longer has the documented closed "
                 "form (missing %r) -- update band_power() in card_costs.py" % token)


def split_top_level(body):
    """Split a C initializer body on commas that are not inside ()."""
    out, depth, cur = [], 0, ""
    for ch in body:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_catalog_calls():
    """cards_content.c rows -> {ROW name: CARD_AP args or None}.

    Cross-checks the CARD_AP(...) arguments against the SAME row's struct
    fields: the macro's power argument must equal `.power`, and its rider
    flag must equal (`.status_id` != STATUS_NONE).  A future edit that
    changes one without the other now fails `make card-costs`."""
    text = CARDS_C.read_text(encoding="utf-8")
    rows = {}
    for m in re.finditer(
            r"\{\s*CARD_[A-Z0-9_]+\s*,.*?,\s*\"([A-Z ]+)\"\s*\}", text, re.S):
        name = m.group(1)
        fields = split_top_level(m.group(0).strip()[1:-1])
        if len(fields) < 12:
            fail("catalog: row %s has %d fields, expected 12" % (name, len(fields)))
            continue
        try:
            struct_power = int(fields[2])
        except ValueError:
            fail("catalog: row %s has a non-literal .power (%r)"
                 % (name, fields[2]))
            continue
        struct_rider = 0 if "STATUS_NONE" in fields[9] else 1
        cm = re.search(
            r"CARD_AP\(\s*(\d+)\s*,\s*MAT_(\w+)\s*,\s*(\d+)\s*\)", fields[3])
        if cm:
            p, mat_name, r = int(cm.group(1)), cm.group(2), int(cm.group(3))
            if p != struct_power:
                fail("catalog: row %s CARD_AP power %d but .power is %d"
                     % (name, p, struct_power))
            if r != struct_rider:
                fail("catalog: row %s CARD_AP rider %d but .status_id is %s"
                     % (name, r, fields[9]))
            rows[name] = ((p, mat_name, r), None)
        else:
            cost_txt = fields[3].strip()
            rows[name] = (None, int(cost_txt) if cost_txt.isdigit() else None)
    return rows


MERGE_C = REPO / "src" / "rpg" / "deck_merge_banked.c"

_TYPE_MAP = {"CARD_TYPE_ATTACK": 0, "CARD_TYPE_DEFENSE": 1,
             "CARD_TYPE_HEAL": 2, "CARD_TYPE_SPECIAL": 5}
_FX_MAP = {"CARD_EFFECT_NONE": 0, "CARD_EFFECT_DAMAGE_TARGET": 1,
           "CARD_EFFECT_BLOCK_DAMAGE": 2, "CARD_EFFECT_HEAL_HP": 3}
_BT_MAP = {"BATTLE_CARD_TYPE_SWORD": 0, "BATTLE_CARD_TYPE_SHIELD": 1,
           "BATTLE_CARD_TYPE_BOW": 2, "BATTLE_CARD_TYPE_HEAL": 3,
           "BATTLE_CARD_TYPE_DAGGER": 4}
_ST_MAP = {"STATUS_NONE": 0, "STATUS_POISON": 1, "STATUS_BURN": 2,
           "STATUS_FREEZE": 3}
_MAT_IDX = {"WOOD": 0, "BRONZE": 1, "IRON": 2, "STEEL": 3, "MYTHRIL": 4}


def check_identity():
    """Merge-table parity: s_ident_catalog in deck_merge_banked.c must
    match the curated catalog in cards_content.c field-for-field
    (type, power, cost, uses, effect, battle type, rider id, rider
    chance, max_copies).  The merge folds loot twins onto these
    identities, so a drift silently mis-merges.  Exit 0 = parity,
    1 = drift (with row/field expected/actual)."""
    text = CARDS_C.read_text(encoding="utf-8")
    want = []
    for m in re.finditer(
            r"\{\s*CARD_[A-Z0-9_]+\s*,.*?,\s*\"([A-Z ]+)\"\s*\}", text, re.S):
        name = m.group(1)
        fields = split_top_level(m.group(0).strip()[1:-1])
        if len(fields) < 12:
            fail("identity: row %s has %d fields, expected 12" % (name, len(fields)))
            continue
        cm = re.search(r"CARD_AP\(\s*(\d+)\s*,\s*MAT_(\w+)\s*,\s*(\d+)\s*\)",
                       fields[3])
        if cm:
            p, mat_name, r = int(cm.group(1)), cm.group(2), int(cm.group(3))
            mat = _MAT_IDX[mat_name]
            cost = (p + mat * 4 + r * 8 + (12 if mat == 4 else 0) + 7) // 8
        else:
            cost = int(fields[3].strip()) if fields[3].strip().isdigit() else 0
        try:
            want.append((name, (_TYPE_MAP[fields[1].strip()],
                                int(fields[2]), cost, int(fields[4]),
                                _FX_MAP[fields[6].strip()],
                                _BT_MAP[fields[7].strip()],
                                _ST_MAP[fields[9].strip()],
                                int(fields[10]), int(fields[5]))))
        except (KeyError, ValueError) as e:
            fail("identity: row %s has an unmapped field (%s)" % (name, e))
    mtext = MERGE_C.read_text(encoding="utf-8")
    m = re.search(r"s_ident_catalog\[GAME_CARD_COUNT\]\[9\]\s*=\s*\{(.*?)\};",
                  mtext, re.S)
    if not m:
        fail("identity: s_ident_catalog table not found in deck_merge_banked.c")
        got = []
    else:
        got = [tuple(int(x) for x in row.split(","))
               for row in re.findall(r"\{([0-9,\s]+)\}", m.group(1))]
    want_n = _game_card_count()
    if len(want) != want_n:
        fail("identity: parsed %d catalog rows, expected %d" % (len(want), want_n))
    if len(got) != want_n:
        fail("identity: parsed %d merge rows, expected %d" % (len(got), want_n))
    for k in range(min(len(want), len(got))):
        name, w = want[k]
        g = got[k]
        if len(g) != 9:
            fail("identity: merge row %d (%s) has %d cells, expected 9"
                 % (k, name, len(g)))
            continue
        if tuple(w) != tuple(g):
            labels = ("type", "power", "cost", "uses", "effect",
                      "battle", "rider", "chance", "cap")
            for idx, lbl in enumerate(labels):
                if w[idx] != g[idx]:
                    fail("identity: row %d (%s) field %s: catalog=%d merge=%d"
                         % (k, name, lbl, w[idx], g[idx]))


def main():
    # Identity parity (curated catalog vs s_ident_catalog) is part of the
    # card-cost contract, not an optional extra pass: run it on every
    # invocation so one process covers both (no separate pass needed).
    check_identity()
    loot_h = LOOT_H.read_text(encoding="utf-8")
    gates = {
        "bronze": parse_define_int(loot_h, "LOOT_AP_BRONZE"),
        "iron": parse_define_int(loot_h, "LOOT_AP_IRON"),
        "steel": parse_define_int(loot_h, "LOOT_AP_STEEL"),
        "mythril_rider": parse_define_int(loot_h, "LOOT_AP_MYTHRIL_RIDER"),
    }
    pts = {
        "material": parse_define_int(loot_h, "CARD_AP_MATERIAL_PTS"),
        "rider": parse_define_int(loot_h, "CARD_AP_RIDER_PTS"),
        "mythril": parse_define_int(loot_h, "CARD_AP_MYTHRIL_PTS"),
    }
    cap = parse_define_int(loot_h, "CARD_AP_MAX")
    if any(v is None for v in list(gates.values()) + list(pts.values()) + [cap]):
        print("FAIL: could not parse loot.h constants")
        return 1
    if cap != 6:
        fail("CARD_AP_MAX is %d, expected 6 (mythril riders cost 6)" % cap)
    parse_loot_power_guard(loot_h)

    # All 37 legal combos: 5 mats x 5 weapons plain + 4 mats x 3 pairs rider
    combos = []
    for mat in range(5):
        for weapon in WEAPONS_41:
            combos.append((band_power(mat, weapon), mat, weapon, 0))
    for mat in range(1, 5):
        for _fx, weapon in WEAPONS_42:
            combos.append((band_power(mat, weapon), mat, weapon, 1))
    if len(combos) != 37:
        fail("combo count is %d, expected 37 (band table changed?)" % len(combos))

    costs = {}
    for power, mat, weapon, rider in combos:
        ap = card_ap(power, mat, rider, pts)
        costs[(mat, weapon, rider)] = ap
        g = gate_ap(mat, rider, gates)
        if ap > g:
            fail("§6.1 gate: %s %s%s costs %d > gate %d"
                 % (MATERIALS[mat], weapon,
                    " +rider" if rider else "", ap, g))

    # Pairwise no-inversion (§6.3/§7.5): B dominates A on all inputs => cost(B) >= cost(A)
    keys = [(p, m, r) for (p, m, _w, r) in combos]
    cost_by_inputs = {}
    for power, mat, weapon, rider in combos:
        cost_by_inputs[(power, mat, rider)] = costs[(mat, weapon, rider)]
    for pa, ma, ra in keys:
        for pb, mb, rb in keys:
            if (pb >= pa and mb >= ma and rb >= ra
                    and (pb, mb, rb) != (pa, ma, ra)):
                if cost_by_inputs[(pb, mb, rb)] < cost_by_inputs[(pa, ma, ra)]:
                    fail("no-inversion: (%d,%d,%d)->%d beats (%d,%d,%d)->%d"
                         % (pb, mb, rb, cost_by_inputs[(pb, mb, rb)],
                            pa, ma, ra, cost_by_inputs[(pa, ma, ra)]))

    # Strict riders: every rider pair costs exactly one more than same card plain
    for mat in range(1, 5):
        for _fx, weapon in WEAPONS_42:
            plain = costs[(mat, weapon, 0)]
            rider = costs[(mat, weapon, 1)]
            if rider != plain + 1:
                fail("strict rider: %s %s plain %d rider %d (want +1)"
                     % (MATERIALS[mat], weapon, plain, rider))

    # Table parity with the doc
    doc_cells, doc_catalog = parse_doc_tables()
    if len(doc_cells) != 37:
        fail("parsed %d §4.1/§4.2 cells from the doc, expected 37" % len(doc_cells))
    for mat, weapon, rider, doc_ap, doc_power in doc_cells:
        key = (mat, weapon, rider)
        power = band_power(mat, weapon)
        if doc_power != power:
            fail("doc power: §4 table says p%d for %s %s%s, band says p%d"
                 % (doc_power, MATERIALS[mat], weapon,
                    " +rider" if rider else "", power))
        if doc_ap != costs[key]:
            fail("doc parity: §4 table says %d AP for %s %s%s, formula says %d"
                 % (doc_ap, MATERIALS[mat], weapon,
                    " +rider" if rider else "", costs[key]))

    # Catalog parity with §4.3
    cat_calls = parse_catalog_calls()
    if set(doc_catalog.keys()) != set(cat_calls.keys()):
        fail("catalog keys diverge from §4.3: doc=%s catalog=%s"
             % (sorted(doc_catalog.keys()), sorted(cat_calls.keys())))
    if len(doc_catalog) != _game_card_count():
        fail("parsed %d §4.3 catalog rows from the doc, expected %d"
             % (len(doc_catalog), _game_card_count()))
    for row, (args, expected) in doc_catalog.items():
        if args is None:
            # SPECIAL quest rows (AMULET, BONE, OLD KEY): no CARD_AP
            # call, cost must stay literal 0 (not playable).
            if cat_calls.get(row, (None, None))[1] != 0 or expected != 0:
                fail("catalog: %s cost must stay literal 0 (not playable)"
                     % row)
            continue
        if row not in cat_calls or cat_calls[row][0] is None:
            fail("catalog: row %s has no CARD_AP(...) call" % row)
            continue
        (p, mat_name, r) = cat_calls[row][0]
        mat = MATERIALS.index(mat_name.lower())
        got = card_ap(p, mat, r, pts)
        (ep, emat, er) = args
        if (p, mat, r) != (ep, emat, er):
            fail("catalog: row %s calls CARD_AP(%d, MAT_%s, %d) but §4.3 says %d/%s/%d"
                 % (row, p, mat_name, r, ep, MATERIALS[emat], er))
        if got != expected:
            fail("catalog: row %s evaluates to %d AP, §4.3 says %d"
                 % (row, got, expected))

    if FAILURES:
        print("card-costs FAIL (%d):" % len(FAILURES))
        for f in FAILURES:
            print("  - " + f)
        return 1
    print("card-costs OK: 37 combos, gates + ordering + strict riders + "
          "doc parity + catalog parity + identity parity")
    return 0


if __name__ == "__main__":
    sys.exit(main())
