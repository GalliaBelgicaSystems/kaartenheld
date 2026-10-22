"""Capture Nis's farewell box on the release ROM (review aid)."""
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
from walkthrough.state_reader import SCENE_VILLAGE_AREA  # noqa: E402
from capture_walkthrough import REVIEW_DIR, REVIEW_MANIFEST  # noqa: E402

FNAME = "gamble-farewell.png"
DESC = "Nis says goodbye ('Come back anytime!') after leaving the table"


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
    s = Session(checks, "gamble-farewell")
    try:
        W.god_mode(s)
        field_pos = W._boot_to_field(s, planner)
        W.follow(s, planner, "field", field_pos, "village_area", (15, 10))
        s.settle_scene(expected_scene=SCENE_VILLAGE_AREA)
        s.check("Nis dialogue opened",
                s.press_until("up", lambda: s.text_has("Nis:"),
                              tries=8, settle=30, label="Nis:"),
                expected="Nis:", actual="none")
        for label, needle in (("page 2", "2:1 OR 3:1"), ("choice", "> YES")):
            s.check(label,
                    s.press_until("a", lambda: s.text_has(needle),
                                  tries=6, settle=30, label=label),
                    expected=needle, actual="none")
        s.check("table opened",
                s.press_until("a", lambda: s.text_has("CHOOSE BET"),
                              tries=6, settle=30, label="CHOOSE BET"),
                expected="CHOOSE BET", actual="none")
        s.check("left the table",
                s.press_until("b", lambda: s.text_has("Come back anytime!"),
                              tries=8, settle=30, label="farewell"),
                expected="Come back anytime!", actual="none")
        s.wait_for(s.screen_rendered, ticks=360)
        s.tick(8)
        os.makedirs(REVIEW_DIR, exist_ok=True)
        try:
            manifest = json.load(open(REVIEW_MANIFEST))
        except (OSError, ValueError):
            manifest = {"shots": []}
        manifest["shots"] = [sh for sh in manifest.get("shots", [])
                             if sh.get("file") != FNAME]
        path = os.path.join(REVIEW_DIR, FNAME)
        s.pb.screen.image.save(path)
        print("saved", os.path.relpath(path, REPO))
        manifest["shots"].append({
            "file": FNAME,
            "description": DESC,
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
