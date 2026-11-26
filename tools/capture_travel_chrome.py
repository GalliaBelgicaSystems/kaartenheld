"""Capture travel-demo journey screens with the new tile chrome.

Drives: field boot -> signpost -> JOURNEY (method view shot) -> ON FOOT
-> camp IDLE (chrome buttons shot). Saves to screenshots/review/.
Usage: python3 tools/capture_travel_chrome.py (inside nix develop).
"""
import os
import sys

sys.path.insert(0, os.path.join("tools"))
from verify_travel_demo import D, boot_to_overworld, SCREEN_JOURNEY

OUT = os.path.join("screenshots", "review")


def shot(d, name):
    d.tick(30)
    path = os.path.join(OUT, name)
    d.pb.screen.image.save(path)
    print("saved %s" % path)
    for r in d.rows():
        if r.strip():
            print("  |%s|" % r)


def main():
    os.makedirs(OUT, exist_ok=True)
    d = D()
    try:
        boot_to_overworld(d)
        st = d.scene()
        d.press("down")
        st = d.scene()
        assert (st["player_x"], st["player_y"]) == (5, 8), st
        d.press("a", settle=30)
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=300), "no JOURNEY (screen=%d)" % d.screen()
        assert d.wait_for(lambda: d.text_has("TRAVEL"), ticks=300), d.rows()
        shot(d, "travel-chrome-method.png")
        d.press("a", settle=20)  # ON FOOT (default) -> camp IDLE
        assert d.wait_for(lambda: d.text_has("STEP"), ticks=300), d.rows()
        shot(d, "travel-chrome-idle.png")
    finally:
        d.close()


if __name__ == "__main__":
    main()
