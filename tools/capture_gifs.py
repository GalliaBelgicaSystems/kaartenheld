#!/usr/bin/env python3
"""README GIF captures (host-side, never CI-gated).

Produces three headless PyBoy recordings of the RELEASE ROM, assembled to GIF
with Pillow:

  screenshots/boot.gif       Gallia Belgica splash -> Kaartenheld title
  screenshots/battle.gif     kobold trio: TWO PAIR combo selection + attack
  screenshots/overworld.gif  Field -> forest gate -> wander to the chest

Regenerate with `make gifs`.  The frames are a visual aid only; the semantic
gate is `make verify-walkthrough` (docs/verify-walkthrough.md).
"""

import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "tools"))

from PIL import Image  # noqa: E402
from walkthrough.session import Session, ROM  # noqa: E402
from walkthrough.route import Planner  # noqa: E402
from walkthrough.state_reader import SymTable, BT_SWORD, BT_SHIELD  # noqa: E402
from walkthrough import walks as W  # noqa: E402

SCALE = 3
OUT = os.path.join(REPO, "screenshots")
BOOT_GIF = os.path.join(OUT, "boot.gif")
BATTLE_GIF = os.path.join(OUT, "battle.gif")
OVERWORLD_GIF = os.path.join(OUT, "overworld.gif")

# Release-ROM screen ids (src/screens/screen.h).
SCREEN_TITLE = 9
SCREEN_SPLASH = 12


def save_gif(frames, path, duration, keep=1):
    sel = frames[::keep]
    if not sel:
        raise SystemExit("no frames captured for %s" % path)
    imgs = [f.convert("RGB").resize((160 * SCALE, 144 * SCALE), Image.NEAREST)
            for f in sel]
    # Global 64-colour palette built from a sample of frames, so colours that
    # only appear later (the red logo, the blue bracket) are not lost to a
    # palette taken from the first frame alone.
    step = max(1, len(imgs) // 16)
    sample = imgs[::step] or imgs[:1]
    comp = Image.new("RGB", (imgs[0].width, imgs[0].height * len(sample)))
    for i, im in enumerate(sample):
        comp.paste(im, (0, i * im.height))
    palette = comp.quantize(colors=64, method=Image.MEDIANCUT)
    pimgs = [im.quantize(palette=palette, dither=Image.NONE) for im in imgs]
    pimgs[0].save(path, save_all=True, append_images=pimgs[1:],
                  duration=duration, loop=0, optimize=True, disposal=2)
    secs = len(pimgs) * duration / 1000.0
    print("wrote %s (%d frames, %.1fs, %d B)"
          % (os.path.relpath(path, REPO), len(pimgs), secs,
             os.path.getsize(path)))


def capture_boot():
    """Raw PyBoy boot: splash (screen 12) then title (screen 9)."""
    from pyboy import PyBoy
    gg = SymTable(os.path.splitext(ROM)[0] + ".sym").wram("g_game")
    pb = PyBoy(ROM, window="null")
    frames = []

    # Reach the splash (boot ROM + init run first).
    for _ in range(1200):
        pb.tick()
        if pb.memory[gg] == SCREEN_SPLASH:
            break
    # Let the first splash render replace the boot-ROM logo before recording.
    for _ in range(40):
        pb.tick()

    # Splash: sample every 8 ticks; cap the count so the hold stays snappy.
    splash = []
    guard = 0
    while pb.memory[gg] == SCREEN_SPLASH and guard < 1200:
        for _ in range(8):
            pb.tick()
        splash.append(pb.screen.image.copy())
        guard += 8
    frames += splash[:12]

    # Title (PRESS START): ~1.6 s at 4-tick sampling.
    for i in range(160):
        pb.tick()
        if i % 4 == 0:
            frames.append(pb.screen.image.copy())
    pb.stop(save=False)
    return frames


def capture_battle():
    """Kobold trio: select a TWO PAIR hand, then execute the attack."""
    checks = []
    planner = Planner()
    s = Session(checks, "gif-battle")
    try:
        field = W._level("field")
        kobold = next(o for o in field["objects"]
                      if (o.get("properties") or {}).get("entity_id")
                      == "ENTITY_ID_KOBOLD")
        kxy = (kobold["position"]["x"], kobold["position"]["y"])
        if not W.engage_hostile(s, planner, "field", kxy):
            raise SystemExit("kobold battle did not engage")
        # Settle into the player select phase (TARGET banner).
        s.wait_for(lambda: s.text_has("TARGET ")
                   or s.text_has("PLAYER TURN"), ticks=300)
        s.tick(30)

        # Record every frame from here on.
        frames = []

        def cap_tick(n=1):
            for _ in range(n):
                s.pb.tick()
                frames.append(s.pb.screen.image.copy())

        s.tick = cap_tick

        # Opening hand is SW SW SH SH SW: selecting every sword + shield
        # yields TWO PAIR (3,3 / 2,2 + kicker).
        W._select_cards(s, (BT_SWORD, BT_SHIELD))
        s.tick(120)                     # hold on the formed TWO PAIR
        s.press("select", settle=20)    # execute the attack
        # Capture the attack resolution only; stop before the enemy
        # telegraph / defend phase so the GIF ends on the hit.
        for _ in range(240):
            s.tick(1)
            if s.text_has("DEFEND") or s.text_has("ENEMY ATTACK"):
                break
    finally:
        s.close()
    return frames


def capture_overworld():
    """Field spawn -> forest gate -> wander to the amulet chest."""
    checks = []
    planner = Planner()
    s = Session(checks, "gif-overworld")
    try:
        field = W._level("field")
        sp = field["player"]["spawn"]
        spawn = (sp["x"], sp["y"])
        e = next(e for e in field["exits"]
                 if e["target_scene"] == "forest")
        goal = (e["target_x"], e["target_y"])

        frames = []

        def cap_tick(n=1):
            for _ in range(n):
                s.pb.tick()
                frames.append(s.pb.screen.image.copy())

        s.tick = cap_tick

        # Walk the Field to the north gate; the crossing wipes to the Forest.
        W.follow(s, planner, "field", spawn, "forest", goal)
        s.settle_scene(expected_scene=W.SCENE_FOREST)
        s.tick(20)

        # Wander east to the Lost Amulet and stop facing it.
        chest = next(o for o in W._level("forest")["objects"]
                     if (o.get("properties") or {}).get("entity_id")
                     == "ENTITY_ID_AMULET")
        cxy = (chest["position"]["x"], chest["position"]["y"])
        path = planner.path("forest", goal, (cxy[0] - 1, cxy[1]))
        if path:
            W._walk_path(s, path)
        s.tick(40)
    finally:
        s.close()
    return frames


def main():
    ap = argparse.ArgumentParser(description="README GIF captures")
    ap.add_argument("--only", choices=["boot", "battle", "overworld"],
                    default=None)
    args = ap.parse_args()

    if args.only in (None, "boot"):
        save_gif(capture_boot(), BOOT_GIF, duration=90)
    if args.only in (None, "battle"):
        save_gif(capture_battle(), BATTLE_GIF, duration=50, keep=3)
    if args.only in (None, "overworld"):
        # Smooth 30 fps for the camera scroll: record every frame, keep
        # every 2nd, 33 ms each.
        save_gif(capture_overworld(), OVERWORLD_GIF, duration=33, keep=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
