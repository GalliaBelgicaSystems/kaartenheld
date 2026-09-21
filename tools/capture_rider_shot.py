"""Rider hand-card capture shot (review aid, never CI-gated).

Stages burn/freeze riders into the live battle hand -- the same real render
path the game uses (equivalent to the debug harness's set_hand_card_status,
but against the release ROM): boot -> field slime engage -> TARGET phase ->
write hand[0] SW3+BURN and hand[1] SW4+FREEZE -> DIRTY_HAND redraw -> shoot.
Proves rider OAM icons sit inset over their own cards' top-right corners.

Outputs land in screenshots/review/ and merge into review/manifest.json;
the --clean prune keeps that directory honest.
"""
import json
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "tools"))

from walkthrough.route import Planner                 # noqa: E402
from walkthrough import walks as W                    # noqa: E402
from walkthrough.session import Session               # noqa: E402
from walkthrough.state_reader import (BATTLE_HAND, CARD_SIZE)  # noqa: E402
from capture_walkthrough import REVIEW_DIR, REVIEW_MANIFEST  # noqa: E402

# Mirrors of the C enums (src/battle/card.h, src/rpg/status.h).
BT_SWORD = 0
CARD_EFFECT_DAMAGE_TARGET = 1
STATUS_POISON = 1
STATUS_BURN = 2
STATUS_FREEZE = 3
# Battle struct: player(14) + enemies[3](42) + enemy_count + target_idx,
# then the dirty byte.
DIRTY_OFF = 14 + 3 * 14 + 2
BATTLE_DIRTY_HAND = 0x10
CARD_STATUS = 5
CARD_CHANCE = 6

SHOTS = [
    ("rider-hand-fire-ice.png",
     [(0, 3, STATUS_BURN), (1, 4, STATUS_FREEZE)],
     "Rider icons inset over their cards' top-right corners (SW3+BURN, SW4+FREEZE)"),
]


def commit_hash():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              cwd=REPO, capture_output=True,
                              text=True).stdout.strip() or "unknown"
    except OSError:
        return "unknown"


def main():
    checks = []
    planner = Planner()
    s = Session(checks, "rider-shot")
    try:
        field = W._level("field")
        spawn = (field["player"]["spawn"]["x"],
                 field["player"]["spawn"]["y"])
        slime = next((o for o in field["objects"]
                      if (o.get("properties") or {}).get("entity_id")
                      == "ENTITY_ID_SLIME"), None)
        slime_xy = (slime["position"]["x"], slime["position"]["y"])
        s.check("slime engaged",
                W.engage_hostile(s, planner, "field", slime_xy),
                expected="DECK:", actual="none")

        # Select phase: the hand is drawn and the select banner is up.
        s.wait_for(lambda: s.text_has("TARGET ")
                   or s.text_has("PLAYER TURN"), ticks=300)
        s.tick(40)
        r = s.reader
        base = r._resolve_battle_base() + BATTLE_HAND

        def write_riders(specs):
            for idx, value, status in specs:
                addr = base + idx * CARD_SIZE
                s.pb.memory[addr + 0] = BT_SWORD
                s.pb.memory[addr + 1] = value
                s.pb.memory[addr + CARD_STATUS] = status
                s.pb.memory[addr + CARD_CHANCE] = 255
            dirty = base - BATTLE_HAND + DIRTY_OFF
            s.pb.memory[dirty] = s.pb.memory[dirty] | BATTLE_DIRTY_HAND

        try:
            with open(REVIEW_MANIFEST) as fh:
                manifest = json.load(fh)
            manifest["shots"] = [sh for sh in manifest.get("shots", [])
                                 if sh.get("file") not in
                                 [f for f, _, _ in SHOTS]]
        except (OSError, ValueError):
            manifest = {"shots": []}
        for fname, specs, desc in SHOTS:
            write_riders(specs)
            s.tick(20)
            # Verify the staged state reads back before shooting.
            hand = r.battle_hand()
            staged = [(hand[i][0], r.rd(
                r._resolve_battle_base() + BATTLE_HAND + i * CARD_SIZE
                + CARD_STATUS, 1)[0]) for i, _, _ in specs]
            s.check("riders staged",
                    staged == [(BT_SWORD, st) for _, _, st in specs],
                    expected=str([(BT_SWORD, st) for _, _, st in specs]),
                    actual=str(staged))
            path = os.path.join(REVIEW_DIR, fname)
            s.wait_for(s.screen_rendered, ticks=360)
            s.tick(8)
            s.pb.screen.image.save(path)
            print("saved", os.path.relpath(path, REPO))
            manifest["shots"].append({
                "file": fname,
                "description": desc,
                "taken_from": commit_hash(),
            })
        with open(REVIEW_MANIFEST, "w") as fh:
            json.dump(manifest, fh, indent=1)
            fh.write("\n")
    finally:
        s.close()

    bad = [c for c in checks if not c[2]]
    for label, name, _ok, expected, actual in bad:
        print("  [%s] %s\n      expected: %s\n      actual:   %s"
              % (label, name, expected, actual))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
