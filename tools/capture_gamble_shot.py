"""Chuck-a-luck table capture shots (review aids, never CI-gated).

Real-path staging on the release ROM: boot -> town -> walk to the
friendly kobold at (16,14) -> bump down into the table -> shoot the
CHOOSE screen; then roll once and shoot the result screen.  Exercises
the same render path a player uses (no memory pokes).

Outputs land in screenshots/review/ and merge into review/manifest.json;
--clean keeps that directory honest (see capture_walkthrough.py).
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
from walkthrough.state_reader import SCENE_TOWN       # noqa: E402
from capture_walkthrough import REVIEW_DIR, REVIEW_MANIFEST  # noqa: E402

SHOTS = [
    ("gamble-table.png", "CHUCK-A-LUCK",
     "Chuck-a-luck table (kobold art, G OWNED / G BET rows, dice boxes)"),
    ("gamble-roll.png", "MATCH",
     "After one roll: kobold dice, match line, settled gold"),
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
    s = Session(checks, "gamble-shot")
    try:
        W.god_mode(s)
        field_pos = W._boot_to_field(s, planner)
        W.follow(s, planner, "field", field_pos, "town", (15, 7))
        s.settle_scene(expected_scene=SCENE_TOWN)
        # Bump right into the gambler kobold (16,7).
        s.check("gamble table opened",
                s.press_until("right", lambda: s.text_has("CHUCK-A-LUCK"),
                              tries=8, settle=30, label="CHUCK-A-LUCK"),
                expected="CHUCK-A-LUCK", actual="none")
        os.makedirs(REVIEW_DIR, exist_ok=True)
        try:
            manifest = json.load(open(REVIEW_MANIFEST))
        except (OSError, ValueError):
            manifest = {"shots": []}
        manifest["shots"] = [sh for sh in manifest.get("shots", [])
                              if sh.get("file") not in dict(
                                  (f, None) for f, _, _ in SHOTS)]
        s.wait_for(s.screen_rendered, ticks=360)
        s.tick(8)
        path = os.path.join(REVIEW_DIR, SHOTS[0][0])
        s.pb.screen.image.save(path)
        print("saved", os.path.relpath(path, REPO))
        manifest["shots"].append({
            "file": SHOTS[0][0],
            "description": SHOTS[0][2],
            "taken_from": commit_hash(),
        })
        # Roll once (defaults NUM=1 BET=1) and capture the result.
        s.press("a", settle=30)
        s.check("roll resolved",
                s.wait_for(lambda: s.text_has("MATCH"), ticks=300),
                expected="MATCH", actual="none")
        s.wait_for(s.screen_rendered, ticks=360)
        s.tick(8)
        path = os.path.join(REVIEW_DIR, SHOTS[1][0])
        s.pb.screen.image.save(path)
        print("saved", os.path.relpath(path, REPO))
        manifest["shots"].append({
            "file": SHOTS[1][0],
            "description": SHOTS[1][2],
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
