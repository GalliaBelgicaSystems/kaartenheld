"""Bow hand-card capture shots (review aids, never CI-gated).

Stages a bow into the live battle hand — the same real render path the
game uses (equivalent to the debug harness's set_hand_card, but against
the release ROM): boot -> field slime engage -> TARGET phase ->
write hand[0] as a 2-use bow -> DIRTY_HAND redraw -> shoot; then a
depleted (uses=0) variant.

Outputs land in screenshots/review/ and are registered in
review/manifest.json; the --clean prune keeps that directory honest.
"""
import io
import json
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "tools"))

from walkthrough.route import Planner                 # noqa: E402
from walkthrough import walks as W                    # noqa: E402
from walkthrough.session import Session, ROM          # noqa: E402
from walkthrough.state_reader import (BATTLE_HAND, CARD_SIZE,  # noqa: E402
                                      BATTLE_CURSOR, CARD_USES)
from capture_walkthrough import REVIEW_DIR, REVIEW_MANIFEST  # noqa: E402

# Mirrors of the C enums (src/battle/card.h, src/battle/battle.h,
# src/rpg/cards.h).
BT_BOW = 2
CARD_EFFECT_DAMAGE_TARGET = 1
# Battle struct: player(14) + enemies[3](42) + enemy_count + target_idx,
# then the dirty byte.
DIRTY_OFF = 14 + 3 * 14 + 2
BATTLE_DIRTY_HAND = 0x10

SHOTS = [
    ("bow-card-uses2.png", 2,
     "Bow hand card, 2 uses left: power '10' row + 2-arrows counter"),
    ("bow-card-depleted.png", 0,
     "Bow hand card depleted: zero-arrows glyph + grey tint"),
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
    s = Session(checks, "bow-shot")
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

        def write_hand(uses):
            card = bytes([BT_BOW, 10, uses, 2,
                          CARD_EFFECT_DAMAGE_TARGET, 0, 0, 0])
            for i, b in enumerate(card):
                s.pb.memory[base + i] = b
            dirty = base - BATTLE_HAND + DIRTY_OFF
            s.pb.memory[dirty] = s.pb.memory[dirty] | BATTLE_DIRTY_HAND

        os.makedirs(REVIEW_DIR, exist_ok=True)
        manifest = {"shots": []}
        for fname, uses, desc in SHOTS:
            write_hand(uses)
            s.tick(20)
            # Verify the staged state reads back before shooting.
            hand = r.battle_hand()
            staged = (hand[0][0], hand[0][1],
                      r.rd(r._resolve_battle_base() + BATTLE_HAND
                           + CARD_USES, 1)[0])
            s.check("hand[0] staged (uses=%d)" % uses,
                    staged == (BT_BOW, 10, uses),
                    expected=str((BT_BOW, 10, uses)),
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
            json.dump(manifest, fh, indent=2)
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
