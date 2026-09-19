"""Headless walkthrough of the release ROM with semantic assertions
(docs/verify-walkthrough.md).

Runs the real content (levels/) under PyBoy, drives it with BFS-planned
routes, and asserts canonical gameplay state read from WRAM (StateReader).
Saves the committed screenshots/ set as a side effect; the PNGs stay a
non-gating visual aid, the SEMANTIC CHECKS are the gate (CI runs this via
`make verify-walkthrough`).

Exit code 0 = every check passed; 1 = failures (with expected/actual
detail per check, AGENTS.md §46 style).
"""

import argparse
import json
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "tools"))

from walkthrough.route import Planner                 # noqa: E402
from walkthrough import walks as W                    # noqa: E402
from walkthrough.session import ROM, OUT              # noqa: E402
from walkthrough.walks import WALK_SECONDS            # noqa: E402

# Authoritative top-level milestone set (committed PNGs).  sweep-* names
# are computed from the planner's scenes at runtime; every other label
# must match a Session.shoot() call in tools/walkthrough/walks.py.
CLASSIC_MILESTONES = [
    "00-boot-field", "01-field-scrolled", "02-town-arrived",
    "03-guard-dialogue", "04-dialogue-next", "05-shop",
    "06-cards-menu", "07-filter-picker", "08-quests-tab",
    "09-battle", "10-battle-attack", "11-battle-aftermath",
    "11-battle-victory", "12-wizard-save", "13-wizard-saved",
    "14-forest-arrived", "15-title-menu", "16-tutorial-slide0",
    "17-tutorial-slide1", "18-tutorial-slide2", "19-tutorial-slide3",
    "20-tutorial-slide4", "21-tutorial-slide5", "22-tutorial-slide6",
    "23-mimic-battle",
]

REVIEW_DIR = os.path.join(OUT, "review")
REVIEW_MANIFEST = os.path.join(REVIEW_DIR, "manifest.json")


def prune():
    """--clean: delete top-level PNGs that are no longer milestones
    (renamed/retired shots must not linger and confuse reviewers) and
    review/ PNGs missing from the manifest.  review/ without a readable
    manifest is left alone with a warning — never nuke blindly."""
    planner = Planner()
    expected = set(CLASSIC_MILESTONES)
    expected |= {"sweep-%s" % name for name in planner.scenes}
    removed = []
    for f in sorted(os.listdir(OUT)):
        if f.endswith(".png") and f[:-4] not in expected:
            os.remove(os.path.join(OUT, f))
            removed.append(f)
    if removed:
        print("pruned %d stale milestone(s): %s"
              % (len(removed), ", ".join(removed)))
    if not os.path.isdir(REVIEW_DIR):
        return
    try:
        with open(REVIEW_MANIFEST) as fh:
            manifest = json.load(fh)
        listed = {e["file"] for e in manifest.get("shots", [])}
    except (OSError, ValueError) as exc:
        print("warning: review manifest unreadable (%s) — review/ left "
              "untouched" % exc)
        return
    removed = []
    for f in sorted(os.listdir(REVIEW_DIR)):
        if f.endswith(".png") and f not in listed:
            os.remove(os.path.join(REVIEW_DIR, f))
            removed.append(f)
    if removed:
        print("pruned %d stale review shot(s): %s"
              % (len(removed), ", ".join(removed)))


def preflight():
    """ROM/sym/levels coherence gate (triage for uniform world-mirror -1s).

    A uniform world_hostile_count -1 with passing boot anchors is NOT a
    ROM/.sym mismatch — the expected tileset kind comes from the
    working-tree levels/, so levels/ edited after the ROM was built (or
    World-tail struct drift) are the suspects (docs/verify-walkthrough.md
    §4.2).  Report whether the inputs under test match HEAD so a failure
    can be attributed before reading results.  Dirty state is
    INFORMATIONAL only: `make verify-walkthrough` rebuilds from working
    sources by design."""
    sym = os.path.splitext(ROM)[0] + ".sym"
    if not os.path.isfile(sym):
        print("error: release .sym not found — build it first (make "
              "release)", file=sys.stderr)
        return 1
    # Stale-.sym tripwire: .sym and .noi are both pure link-time products
    # (rgbfix stamps the .gb AFTER make_sym.py, and `make test` stamps it
    # again, so the .gb is routinely newer than both — comparing against
    # the .gb would false-positive on every healthy tree).  A .sym older
    # than the .noi predates the latest link: swapped-in or interrupted.
    # The .noi is untracked, so a fresh clone has none and the tripwire
    # is inactive there — say so instead of silently skipping.
    noi = os.path.splitext(ROM)[0] + ".noi"
    if not os.path.isfile(noi):
        print("note: no %s (fresh clone?) — stale-.sym tripwire "
              "inactive" % noi)
    elif os.path.getmtime(sym) < os.path.getmtime(noi):
        print("error: %s is OLDER than %s — stale .sym from another "
              "tree state or an interrupted build; `make clean && make "
              "release` and re-run (see docs/verify-walkthrough.md "
              "§4.2)" % (sym, noi), file=sys.stderr)
        return 1
    try:
        # git diff --quiet: 0 = clean, 1 = differences, >1 = error
        # (not a repo, no git).  Only 1 means dirty; anything else is
        # "unknown", never a false dirty report.
        rc = subprocess.run(
            ["git", "diff", "--quiet", "HEAD", "--", ROM, sym,
             os.path.join(REPO, "levels")],
            cwd=REPO).returncode
        dirty = True if rc == 1 else False if rc == 0 else None
    except OSError:
        dirty = None
    if dirty is True:
        print("note: build/kaartenheld.gb + .sym and/or levels/ differ "
              "from HEAD — testing the working tree, not the committed "
              "inputs")
    elif dirty is False:
        print("note: build/kaartenheld.gb + .sym and levels/ match HEAD")
    return 0


def run(clean=False):
    if not os.path.isfile(ROM):
        print("error: release ROM not found — build it first (make "
              "release)", file=sys.stderr)
        return 1
    if preflight():
        return 1

    os.makedirs(OUT, exist_ok=True)
    if clean:
        prune()

    failures = []
    planner = Planner()
    saved = []   # (sram_bytes, expected) for the load roundtrip

    walk_specs = [
        ("walk-a", lambda: W.walk_a(planner, failures, sram_out=saved)),
        ("walk-b", lambda: W.walk_b(planner, failures)),
        ("walk-c", lambda: W.walk_c(planner, failures)),
        ("walk-e", lambda: W.walk_e(planner, failures)),
        ("walk-sweep", lambda: W.walk_sweep(planner, failures)),
        ("walk-d", lambda: W.walk_d(failures)),
        ("walk-l", lambda: W.walk_l(failures, saved[0])),
    ]

    for name, fn in walk_specs:
        if name == "walk-l" and not saved:
            failures.append(("walk-l", "skipped: no saved state", False,
                             "walk-a save", "missing"))
            continue
        t0 = time.time()
        try:
            fn()
        except Exception as exc:   # noqa: BLE001 — isolate per walk
            import traceback
            traceback.print_exc()
            failures.append((name, "walk crashed", False,
                             "clean run", "%s: %s" % (type(exc).__name__,
                                                      exc)))
        dt = time.time() - t0
        print("[%s] %s in %.1fs" % ("OK " if dt < WALK_SECONDS else "SLOW",
                                    name, dt))
        if dt > WALK_SECONDS:
            failures.append((name, "walk exceeded wall-clock cap", False,
                             "<= %ds" % WALK_SECONDS, "%.1fs" % dt))

    print()
    bad = [c for c in failures if not c[2]]
    if bad:
        print("%d/%d check(s) failed:" % (len(bad), len(failures)))
        for label, name, _ok, expected, actual in bad:
            print("  [%s] %s\n      expected: %s\n      actual:   %s"
                  % (label, name, expected, actual))
        return 1
    print("walkthrough: %d check(s) passed" % len(failures))
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--clean", action="store_true",
                    help="prune stale PNGs: top-level shots no longer in "
                         "the milestone set and review/ shots missing "
                         "from the manifest (CI mode)")
    args = ap.parse_args()
    sys.exit(run(clean=args.clean))
