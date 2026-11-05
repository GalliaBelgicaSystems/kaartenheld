#!/usr/bin/env python3
"""make enemy-ranks gate (docs/enemies_ranking.md §5.2/§5.3, required).

Checks the rank-owned per-enemy bands: every enemy type's hp/max_hp and
gold_reward against its rank's HP/gold band, every BattleId's deck swings
and rider count against its rank's swing/rider band, the slime-knight
split chain totals (knight battle + spawned trio) against the chain band,
and that the hardcoded slime-trio spawn constants still match the slime
content row.  Prints expected/actual and exits non-zero on violation --
same shape as validate.py and card-costs.  Stdlib only.
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FAILURES = []


def fail(msg):
    FAILURES.append(msg)


# ── Bands (docs/enemies_ranking.md §5.1) ────────────────────────────
# HP_MEMBER: normal (trio-member) encounter HP; HP_SOLO: solo builds.
# R6 has no member build (solo only); R0 is scripted (Carl 30, lesson).
BANDS = {
    0: {"hp_member": None, "hp_solo": None, "swing": (0, 0), "riders": 0, "gold": (0, 0)},
    1: {"hp_member": (6, 10), "hp_solo": (18, 24), "swing": (2, 3), "riders": 0, "gold": (3, 6)},
    2: {"hp_member": (8, 12), "hp_solo": (24, 32), "swing": (2, 4), "riders": 1, "gold": (6, 10)},
    3: {"hp_member": (14, 18), "hp_solo": (36, 48), "swing": (3, 4), "riders": 1, "gold": (8, 12)},
    4: {"hp_member": (16, 22), "hp_solo": (48, 64), "swing": (4, 5), "riders": 2, "gold": (12, 18)},
    5: {"hp_member": (30, 40), "hp_solo": (50, 65), "swing": (5, 6), "riders": 2, "gold": (15, 25)},
    6: {"hp_member": None, "hp_solo": (70, 99), "swing": (5, 8), "riders": 2, "gold": (40, 60)},
}

RANK_NAMES = {0: "LESSON", 1: "FODDER", 2: "VERMIN", 3: "GRUNT",
              4: "VETERAN", 5: "ELITE", 6: "BOSS"}

# BattleId numeric order (src/world/actor.h) -> owning enemy-type id.
# BATTLE_CARL has no owning type (rank 0, no drop); BATTLE_NONE resolves
# to the slime lord (only the Lord reaches the loot path with it).
BATTLE_RANK_ORDER = [
    ("BATTLE_NONE", "slime_lord"),
    ("BATTLE_SLIME", "slime"),
    ("BATTLE_BAT", "bat"),
    ("BATTLE_SLIME_TRIO", "slime"),
    ("BATTLE_MIMIC", "mimic"),
    ("BATTLE_KOBOLD", "kobold"),
    ("BATTLE_SPIDER", "spider"),
    ("BATTLE_CARL", None),
    ("BATTLE_SLIME_KNIGHT", "slime_knight"),
]

# BattleId numeric order -> enemy_deck_content.c table name suffix.
DECK_TABLES = {1: "slime", 2: "bat", 3: "slime_trio", 4: "mimic",
               5: "kobold", 6: "spider", 7: "carl", 8: "slime_knight"}


def load_enemy_types():
    out = {}
    for p in sorted((REPO / "screens" / "enemy_types").glob("*.json")):
        try:
            doc = json.loads(p.read_text())
            out[doc["id"]] = doc
        except (OSError, ValueError, KeyError) as e:
            fail("%s: unreadable (%s)" % (p.name, e))
    return out


def check_json_bands(types):
    for eid, doc in sorted(types.items()):
        rank = doc.get("rank")
        if not isinstance(rank, int) or rank not in BANDS:
            fail("%s: rank %r not 0-6 (docs/enemies_ranking.md §4.1)" % (eid, rank))
            continue
        band = BANDS[rank]
        if rank == 0:
            if doc.get("gold_reward", 0) != 0:
                fail("%s (R0): gold_reward %s != 0 (rank 0 earns nothing, §5.1)"
                     % (eid, doc.get("gold_reward")))
            continue
        lo, hi = band["gold"]
        if not (lo <= doc.get("gold_reward", -1) <= hi):
            fail("%s (R%d %s): gold_reward %s outside %d-%d (§5.1)"
                 % (eid, rank, RANK_NAMES[rank], doc.get("gold_reward"), lo, hi))
        hp = doc.get("hp", 0)
        ok = False
        where = ""
        if band["hp_member"] and band["hp_member"][0] <= hp <= band["hp_member"][1]:
            ok, where = True, "member"
        if band["hp_solo"] and band["hp_solo"][0] <= hp <= band["hp_solo"][1]:
            ok, where = True, "solo" if not ok else where + "/solo"
        if not ok:
            fail("%s (R%d %s): hp %s outside member %s / solo %s (§5.1)"
                 % (eid, rank, RANK_NAMES[rank], hp,
                    band["hp_member"], band["hp_solo"]))
        _ = where


def parse_enemy_decks():
    """BattleId -> (swings, rider_count): non-HEAL card values + cards
    carrying a status rider."""
    text = (REPO / "src" / "battle" / "enemy_deck_content.c").read_text()
    decks = {}
    for bid, suffix in DECK_TABLES.items():
        m = re.search(r"s_%s_deck\[\] = \{(.*?)\};" % suffix, text, re.S)
        if not m:
            fail("enemy_deck_content.c: s_%s_deck table missing" % suffix)
            continue
        rows = re.findall(
            r"\{\s*BATTLE_CARD_TYPE_(\w+)\s*,\s*(\d+)\s*,\s*STATUS_(\w+)\s*,\s*(\d+)\s*\}",
            m.group(1))
        if not rows:
            fail("enemy_deck_content.c: s_%s_deck has no parseable rows" % suffix)
            continue
        swings, riders = [], 0
        for typ, val, status, chance in rows:
            if typ == "HEAL":
                continue
            swings.append(int(val))
            if status != "NONE" and int(chance) > 0:
                riders += 1
        decks[bid] = (swings, riders)
    return decks


def check_deck_bands(types, decks):
    for bid, (swings, riders) in sorted(decks.items()):
        battle_id, et_id = BATTLE_RANK_ORDER[bid]
        rank = 0 if et_id is None else types[et_id].get("rank", 0)
        band = BANDS[rank]
        if rank == 0:
            if swings != [0]:
                fail("%s: R0 lesson deck swings %s != [0] (§5.1)" % (battle_id, swings))
            continue
        lo, hi = band["swing"]
        below = [s for s in swings if s < lo]
        above = [s for s in swings if s > hi]
        # At most one below-band setup swing (the mimic's 4 among 5/5/6);
        # two or more means the deck lives below its rank (the old
        # knight 3,3,4 / kobold 2,2,3 / mimic 4,4,5,3 all failed here).
        if len(below) > 1:
            fail("%s (R%d): %d swings below band %d-%d: %s (§5.1)"
                 % (battle_id, rank, len(below), lo, hi, swings))
        if above:
            fail("%s (R%d): swings above band %d-%d: %s (§5.1)"
                 % (battle_id, rank, lo, hi, swings))
        if swings and max(swings) < lo:
            fail("%s (R%d): deck never reaches its band %d-%d: %s (§5.1)"
                 % (battle_id, rank, lo, hi, swings))
        if riders > band["riders"]:
            fail("%s (R%d): %d rider cards > %d allowed (§5.1)"
                 % (battle_id, rank, riders, band["riders"]))


def check_cut_tables():
    """Rank cut/gate parity + shape (docs/enemies_ranking.md App. A).

    The loot body walks `mat_roll > s_rank_cut[rank][material]`, so each
    R1-R6 row must partition all 256 outcomes (last cut 255,
    non-decreasing cuts) and rows must be strictly increasing in
    expected material -- that is what makes "rank = quality" a
    comparison rather than a label.  Gates must be non-decreasing and
    even (the fire/ice half-split needs an exact gate/2).  The code
    rows must also match Appendix A exactly, so a table edit that
    the doc doesn't bless fails here, not in a playtest."""
    text = (REPO / "src" / "game" / "loot_drop_banked.c").read_text()
    m = re.search(r"s_rank_cut\[7\]\[5\] = \{(.*?)\};", text, re.S)
    if not m:
        fail("loot_drop_banked.c: s_rank_cut table not parseable")
        return
    rows = [[int(v) for v in row.split(",")]
            for row in re.findall(r"\{([0-9,\s]+)\}", m.group(1))]
    g = re.search(r"s_rank_gate\[7\] = \{([^}]*)\}", text)
    gates = [int(v) for v in g.group(1).split(",")] if g else []
    if len(rows) != 7 or any(len(r) != 5 for r in rows):
        fail("s_rank_cut: want 7x5, got %dx%s"
             % (len(rows), [len(r) for r in rows]))
        return
    if len(gates) != 7:
        fail("s_rank_gate: want 7 entries, got %d" % len(gates))
        return
    # Doc parity (Appendix A lists the exact rows).
    doc = (REPO / "docs" / "enemies_ranking.md").read_text()
    dm = re.search(r"s_rank_cut\[7\]\[5\] = \{(.*?)\};", doc, re.S)
    drows = ([[int(v) for v in row.split(",")]
              for row in re.findall(r"\{([0-9,\s]+)\}", dm.group(1))]
             if dm else [])
    if drows != rows:
        fail("s_rank_cut drifts from docs/enemies_ranking.md Appendix A "
             "(code=%s doc=%s)" % (rows, drows))
    dg = re.search(r"s_rank_gate\[7\] = \{([^}]*)\}", doc)
    dgates = [int(v) for v in dg.group(1).split(",")] if dg else []
    if dgates != gates:
        fail("s_rank_gate drifts from Appendix A (code=%s doc=%s)"
             % (gates, dgates))
    # R0 is the never-rolled lesson row.
    if rows[0] != [0, 0, 0, 0, 0]:
        fail("s_rank_cut R0 %s != [0,0,0,0,0] (never rolled, §4.3)" % rows[0])
    # Partition shape + strictly increasing expected material.
    prev_e = None
    for r in range(1, 7):
        row = rows[r]
        if row[-1] != 255:
            fail("s_rank_cut R%d last cut %d != 255 (outcomes leak past "
                 "mythril, §4.3)" % (r, row[-1]))
        if any(row[i] > row[i + 1] for i in range(4)):
            fail("s_rank_cut R%d cuts decrease: %s (§4.3)" % (r, row))
        # E[material] over the 256 realized outcomes: P(m=0) covers
        # 0..cut[0], P(m) covers cut[m-1]+1..cut[m].
        e = 0
        for i in range(1, 5):
            e += i * (row[i] - row[i - 1])
        e = e / 256.0
        if prev_e is not None and not (e > prev_e):
            fail("s_rank_cut R%d expected material %.3f not above R%d %.3f "
                 "(rank must mean quality, App. A)" % (r, e, r - 1, prev_e))
        prev_e = e
    # Gate shape: non-decreasing, even (exact gate/2 fire/ice split).
    for r in range(1, 7):
        if gates[r] < gates[r - 1]:
            fail("s_rank_gate R%d %d below R%d %d (§4.3)"
                 % (r, gates[r], r - 1, gates[r - 1]))
    for r in range(7):
        if gates[r] % 2 != 0:
            fail("s_rank_gate R%d %d is odd (fire/ice needs gate/2, §4.3/§4.5)"
                 % (r, gates[r]))


def check_lord_fallback():
    text = (REPO / "src" / "battle" / "battle_telegraph_banked.c").read_text()
    # The deckless fallback is the last assignment in the file (the Lord
    # is the only deckless battler); earlier assignments set 0.
    vals = [int(v) for v in re.findall(r"b->enemy_incoming_dmg = (\d+);", text)]
    if not vals:
        fail("battle_telegraph_banked.c: no incoming_dmg fallback found")
        return
    dmg = vals[-1]
    lo, hi = BANDS[6]["swing"]
    if not (lo <= dmg <= hi):
        fail("Lord fallback swing %d outside R6 band %d-%d (§5.2 decided: 6)"
             % (dmg, lo, hi))


def check_rank_table(types):
    p = REPO / "src" / "game" / "loot_rank_content.c"
    try:
        text = p.read_text()
    except OSError:
        fail("src/game/loot_rank_content.c missing (run make screens)")
        return
    m = re.search(r"g_enemy_rank_by_battle\[\d+\] = \{([^}]*)\}", text)
    if not m:
        fail("loot_rank_content.c: rank row not parseable")
        return
    got = [int(v) for v in re.findall(r"\d+", m.group(1))]
    want = []
    for battle_id, et_id in BATTLE_RANK_ORDER:
        want.append(0 if et_id is None else types.get(et_id, {}).get("rank", -1))
    if got != want:
        fail("loot_rank_content.c rank row %s != enemy JSON ranks %s (§4.2)" % (got, want))
    if got[7] != 0:
        fail("BATTLE_CARL rank %s != 0 (rank 0 = no drop, §8.2)" % got[7])


def check_split_chain(types):
    knight = types.get("slime_knight", {})
    slime = types.get("slime", {})
    khp = knight.get("hp", 0) * 3
    shp = slime.get("hp", 0) * 3
    total_hp = khp + shp
    total_gold = knight.get("gold_reward", 0) + slime.get("gold_reward", 0)
    # R4 band + R1 band summed (docs/enemies_ranking.md §5.1: split
    # enemies are audited as a chain, not a single battle).
    hp_lo = BANDS[4]["hp_member"][0] * 3 + BANDS[1]["hp_member"][0] * 3
    hp_hi = BANDS[4]["hp_member"][1] * 3 + BANDS[1]["hp_member"][1] * 3
    g_lo = BANDS[4]["gold"][0] + BANDS[1]["gold"][0]
    g_hi = BANDS[4]["gold"][1] + BANDS[1]["gold"][1]
    if not (hp_lo <= total_hp <= hp_hi):
        fail("knight chain HP %d (3x%d + 3x%d) outside %d-%d (§5.1)"
             % (total_hp, knight.get("hp"), slime.get("hp"), hp_lo, hp_hi))
    if not (g_lo <= total_gold <= g_hi):
        fail("knight chain gold %d (%d + %d) outside %d-%d (§5.1)"
             % (total_gold, knight.get("gold_reward"), slime.get("gold_reward"),
                g_lo, g_hi))
    # Spawn constants must match the slime content row (§1.6/§5.3).
    text = (REPO / "src" / "game" / "slime_trio_spawn_banked.c").read_text()
    for field, want in (("a->hp = ", slime.get("hp")),
                        ("a->max_hp = ", slime.get("max_hp")),
                        ("a->gold_reward = ", slime.get("gold_reward"))):
        ms = [int(v) for v in re.findall(re.escape(field) + r"(\d+)", text)]
        if not ms or any(v != want for v in ms):
            fail("slime_trio_spawn_banked.c: %s%s != slime.json %s (§1.6)"
                 % (field, ms, want))
    if "BATTLE_SLIME" not in text:
        fail("slime_trio_spawn_banked.c: spawned trio must carry BATTLE_SLIME (§1.6)")
    ev = (REPO / "src" / "game" / "events_content.c").read_text()
    if "EVENT_ACTION_SPAWN_SLIME_TRIO" not in ev:
        fail("events_content.c: no EVENT_ACTION_SPAWN_SLIME_TRIO row (§1.6)")


def level_actors():
    """(path, obj_id, entity_id, battle, hp, max_hp, gold, solo) for every
    actor with a battle in levels/ + fixture levels/."""
    out = []
    roots = [REPO / "levels", REPO / "tools" / "scenarios" / "fixtures" / "levels"]
    for root in roots:
        for p in sorted(root.glob("*.json")):
            if p.name == "registry.json":
                continue
            try:
                doc = json.loads(p.read_text())
            except (OSError, ValueError):
                continue
            for o in doc.get("objects", []):
                pr = o.get("properties", {})
                if not pr.get("battle"):
                    continue
                out.append((str(p.relative_to(REPO)), o.get("id"),
                            pr.get("entity_id"), pr.get("battle"),
                            pr.get("hp", 0), pr.get("max_hp", 0),
                            pr.get("gold_reward", 0),
                            bool(pr.get("solo", False))))
    return out


# Placement battle id -> rank.  BATTLE_MIMIC is NOT here: the mimic branch
# below pins its decided 50 HP / 20 g row before the band lookup (it would
# make this entry unreachable), and the Lord of Slimes names no BattleId at
# all -- BATTLE_NONE resolves through the loot rank fallback, so its
# placement is checked by entity id instead (check_level_placements).
PLACEMENT_RANK = {
    "BATTLE_SLIME": 1, "BATTLE_SLIME_TRIO": 1, "BATTLE_BAT": 2,
    "BATTLE_SPIDER": 2, "BATTLE_KOBOLD": 3,
}


def check_level_placements():
    for path, oid, eid, battle, hp, max_hp, gold, solo in level_actors():
        if eid == "ENTITY_ID_SLIME_LORD":
            # The Lord is the only deckless battler: its placement names
            # BATTLE_NONE (the loot rank table falls back to its type), so
            # band it by entity id -- R6 boss, solo, 70 HP / 50 g decided
            # (§5.2).  Without this branch the placement was never checked.
            blo, bhi = BANDS[6]["hp_solo"]
            glo, ghi = BANDS[6]["gold"]
            if battle != "BATTLE_NONE":
                fail("%s %s: Lord battle %s != BATTLE_NONE (§1.6)"
                     % (path, oid, battle))
            if not solo:
                fail("%s %s: Lord placement is not solo (§5.2)" % (path, oid))
            if not (blo <= hp <= bhi) or not (blo <= max_hp <= bhi):
                fail("%s %s: Lord %d/%d HP outside R6 %d-%d (§5.1)"
                     % (path, oid, hp, max_hp, blo, bhi))
            if hp > max_hp:
                fail("%s %s: Lord hp %d > max_hp %d" % (path, oid, hp, max_hp))
            if not (glo <= gold <= ghi):
                fail("%s %s: Lord %d g outside R6 %d-%d (§5.1)"
                     % (path, oid, gold, glo, ghi))
        elif battle == "BATTLE_CARL":
            if gold != 0:
                fail("%s %s: Carl gold %s != 0 (R0 earns nothing, §5.1/§8.2)"
                     % (path, oid, gold))
        elif battle == "BATTLE_MIMIC":
            if gold != 20 or hp != 50 or max_hp != 50:
                fail("%s %s: mimic %d/%d HP / %d g != 50/50 HP / 20 g (§5.2 decided)"
                     % (path, oid, hp, max_hp, gold))
        elif battle == "BATTLE_SLIME_KNIGHT":
            if solo:
                # Documented exception (decided §8.5): the solo castle
                # knights stay 15 HP and split into threes -- do not "fix".
                if (hp, max_hp, gold) != (15, 15, 12):
                    fail("%s %s: solo knight %d/%d HP %d g != the 15/15/12 "
                         "exception (§8.5)" % (path, oid, hp, max_hp, gold))
            else:
                blo, bhi = BANDS[4]["hp_member"]
                glo, ghi = BANDS[4]["gold"]
                if not (blo <= hp <= bhi) or not (blo <= max_hp <= bhi):
                    fail("%s %s: knight %d/%d HP outside R4 %d-%d HP"
                         % (path, oid, hp, max_hp, blo, bhi))
                if hp > max_hp:
                    fail("%s %s: knight hp %d > max_hp %d"
                         % (path, oid, hp, max_hp))
                if not (glo <= gold <= ghi):
                    fail("%s %s: knight %d g outside R4 %d-%d g"
                         % (path, oid, gold, glo, ghi))
        elif battle in PLACEMENT_RANK:
            # Every ranked trash placement carries its band (the doc §5.2
            # "passes" claims cover actor rows, not just the JSON/SQL
            # defaults -- the battle pays the row).  wall_slime is the
            # one exempt test double: wall-embedded, walk-in bumps never
            # encounter, and its scenarios never assert its purse.
            if (path, oid) == ("tools/scenarios/fixtures/levels/test_mountain_pass.json",
                               "wall_slime"):
                continue
            rank = PLACEMENT_RANK[battle]
            band = BANDS[rank]
            hp_lo, hp_hi = band["hp_solo"] if solo else band["hp_member"]
            glo, ghi = band["gold"]
            if hp_lo is None:
                fail("%s %s: %s has no R%d %s band (§5.1)"
                     % (path, oid, battle, rank,
                        "solo" if solo else "member"))
                continue
            if not (hp_lo <= hp <= hp_hi):
                fail("%s %s: %s %d HP outside R%d %s %d-%d (§5.1)"
                     % (path, oid, battle, hp, rank,
                        "solo" if solo else "member", hp_lo, hp_hi))
            if not (hp_lo <= max_hp <= hp_hi):
                fail("%s %s: %s max_hp %d outside R%d %s %d-%d (§5.1)"
                     % (path, oid, battle, max_hp, rank,
                        "solo" if solo else "member", hp_lo, hp_hi))
            if hp > max_hp:
                fail("%s %s: %s hp %d > max_hp %d"
                     % (path, oid, battle, hp, max_hp))
            if not (glo <= gold <= ghi):
                fail("%s %s: %s %d g outside R%d %d-%d (§5.1)"
                     % (path, oid, battle, gold, rank, glo, ghi))


def main():
    types = load_enemy_types()
    if types:
        check_json_bands(types)
        check_deck_bands(types, parse_enemy_decks())
        check_rank_table(types)
        check_split_chain(types)
        check_cut_tables()
    check_lord_fallback()
    check_level_placements()
    if FAILURES:
        print("enemy-ranks: %d violation(s) (docs/enemies_ranking.md §5):"
              % len(FAILURES))
        for f in FAILURES:
            print("  FAIL: %s" % f)
        return 1
    print("enemy-ranks: all rank bands hold (docs/enemies_ranking.md §5)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
