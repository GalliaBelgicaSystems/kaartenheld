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
            out[json.loads(p.read_text())["id"]] = json.loads(p.read_text())
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


def check_lord_fallback():
    text = (REPO / "src" / "battle" / "battle_telegraph_banked.c").read_text()
    m = re.search(r"b->enemy_incoming_dmg = (\d+);", text)
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
    """(path, obj_id, battle, hp, max_hp, gold, solo) for every actor with
    a battle in levels/ + fixture levels/."""
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
                out.append((str(p.relative_to(REPO)), o.get("id"), pr.get("battle"),
                            pr.get("hp", 0), pr.get("max_hp", 0),
                            pr.get("gold_reward", 0),
                            bool(pr.get("solo", False))))
    return out


def check_level_placements():
    for path, oid, battle, hp, max_hp, gold, solo in level_actors():
        if battle == "BATTLE_CARL":
            if gold != 0:
                fail("%s %s: Carl gold %s != 0 (R0 earns nothing, §5.1/§8.2)"
                     % (path, oid, gold))
        elif battle == "BATTLE_MIMIC":
            if gold != 20 or hp != 50:
                fail("%s %s: mimic %d HP / %d g != 50 HP / 20 g (§5.2 decided)"
                     % (path, oid, hp, gold))
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
                if not (blo <= hp <= bhi) or not (glo <= gold <= ghi):
                    fail("%s %s: knight %d HP %d g outside R4 %d-%d HP / %d-%d g"
                         % (path, oid, hp, gold, blo, bhi, glo, ghi))


def main():
    types = load_enemy_types()
    if types:
        check_json_bands(types)
        check_deck_bands(types, parse_enemy_decks())
        check_rank_table(types)
        check_split_chain(types)
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
