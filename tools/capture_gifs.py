#!/usr/bin/env python3
"""README GIF captures (host-side, never CI-gated).

Produces four headless PyBoy recordings of the RELEASE ROM, assembled to GIF
with Pillow:

  screenshots/boot.gif       Gallia Belgica splash -> Kaartenheld title
  screenshots/battle.gif     kobold trio: combo selection + attack
                             (the hand is whatever battle start deals from
                             the shuffled pile, not a fixed opening hand)
  screenshots/overworld.gif  Field -> forest gate -> wander to the chest
  screenshots/boss.gif       Lord of Slimes: glowing eyes blink in battle

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
from walkthrough.state_reader import (SymTable, BT_SWORD, BT_SHIELD,  # noqa: E402
                                      BT_HEAL, BATTLE_HAND, CARD_SIZE,
                                      BATTLE_CURSOR, CARD_USES,
                                      OFF_VARIABLES, VARIABLE_ID_ENERGY_POOL,
                                      OFF_PARTY, CHARACTER_STATE_HP,
                                      CHARACTER_STATE_MAX_HP, HERO_START_HP)
from walkthrough import walks as W  # noqa: E402

SCALE = 3
OUT = os.path.join(REPO, "screenshots")
BOOT_GIF = os.path.join(OUT, "boot.gif")
BATTLE_GIF = os.path.join(OUT, "battle.gif")
OVERWORLD_GIF = os.path.join(OUT, "overworld.gif")
BOSS_GIF = os.path.join(OUT, "boss.gif")
HEAL_GIF = os.path.join(OUT, "heal.gif")

# The Lord of Slimes always spawns in the throne room (ungated since the
# mayor-quest rework: 5 kills grant +1 AP, no spawn gating anywhere).

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


def stage_energy_pool(s, amount=5):
    """Capture-only AP poke (god_mode precedent): a fresh file starts with
    1 AP (VARIABLE_ID_ENERGY_POOL, src/game/content.c), which stages a
    single card -- not enough to show a combo, which is the point of the
    battle clip.  Raise the pool before the fight so battle_start opens the
    hand with energy for every card.  Never a gameplay claim: the walkthrough
    and scenarios assert the real 1-2 AP economy."""
    base = s.reader.state + OFF_VARIABLES + 2 * (VARIABLE_ID_ENERGY_POOL - 1)
    s.pb.memory[base] = amount
    s.pb.memory[base + 1] = 0


def capture_battle():
    """Kobold trio: select the combo the dealt hand offers, then attack."""
    checks = []
    planner = Planner()
    s = Session(checks, "gif-battle")
    try:
        # Transit god-mode (walkthrough convention): 99 HP so a patrol
        # ambush on the way to the forest can't derail the scripted
        # fight.  The kobold lives in the forest now (the field is
        # Carl's solo spar); walk the walk-c gate first, then engage.
        W.god_mode(s)
        stage_energy_pool(s)
        field = W._level("field")
        spawn = W._boot_to_field(s, planner)
        forest = W._level("forest")
        fspawn = (forest["player"]["spawn"]["x"],
                  forest["player"]["spawn"]["y"])
        W.follow(s, planner, "field", spawn, "forest", fspawn)
        s.settle_scene(expected_scene=W.SCENE_FOREST)
        kobold = next(o for o in forest["objects"]
                      if (o.get("properties") or {}).get("entity_id")
                      == "ENTITY_ID_KOBOLD")
        kxy = (kobold["position"]["x"], kobold["position"]["y"])
        # Walk toward the kobold one press at a time and stop the instant it
        # engages: W.engage_hostile() routes with _walk_path, which resolves a
        # transit ambush itself (auto-win), leaving nothing to record.
        engaged = False
        for _ in range(14):
            if s.text_has("DECK:"):
                engaged = True
                break
            s.press("left" if s.pos()[0] > kxy[0] else "right", settle=24)
        if not engaged:
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

        # The opening hand is a random five of the starter deck, so select
        # every sword + shield the deal contains: with only three distinct
        # (type, value) cards in the starter deck, five cards always hold a
        # duplicate, so some combo always forms whichever five are dealt.
        hand = s.reader.battle_hand()[:5]
        picked = W._select_cards(s, (BT_SWORD, BT_SHIELD))
        print("  battle.gif: dealt %s, selected %d card(s)" % (hand, picked))
        s.tick(120)                     # hold on the formed combo
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


def capture_heal():
    """Heal-flash GIF: take a kobold hit on base HP, then play a staged
    HE5 card and record the 50-frame slime-light HP pulse."""
    # Battle hand card layout (src/battle/card.h + battle.c discard
    # accounting): type, value, uses, cost, effect, status, ring, pad.
    # Effect ids mirror src/rpg/cards.h (same convention as the bow-shot
    # script's hardcoded damage effect).
    CARD_EFFECT_HEAL_HP = 3
    BATTLE_DIRTY_HAND = 0x10
    DIRTY_OFF = 14 + 3 * 14 + 2
    checks = []
    planner = Planner()
    s = Session(checks, "gif-heal")
    try:
        # Transit god-mode for the walk to the forest (walkthrough
        # convention), then restore base HP: god-mode's 99/99 would cap
        # the heal (no HP rise, no flash).
        W.god_mode(s)
        stage_energy_pool(s)
        field = W._level("field")
        spawn = W._boot_to_field(s, planner)
        forest = W._level("forest")
        fspawn = (forest["player"]["spawn"]["x"],
                  forest["player"]["spawn"]["y"])
        W.follow(s, planner, "field", spawn, "forest", fspawn)
        s.settle_scene(expected_scene=W.SCENE_FOREST)
        r = s.reader
        pbase = r.state + OFF_PARTY
        s.pb.memory[pbase + CHARACTER_STATE_HP] = HERO_START_HP
        s.pb.memory[pbase + CHARACTER_STATE_MAX_HP] = HERO_START_HP
        kobold = next(o for o in forest["objects"]
                      if (o.get("properties") or {}).get("entity_id")
                      == "ENTITY_ID_KOBOLD")
        kxy = (kobold["position"]["x"], kobold["position"]["y"])
        engaged = False
        for _ in range(14):
            if s.text_has("DECK:"):
                engaged = True
                break
            s.press("left" if s.pos()[0] > kxy[0] else "right", settle=24)
        if not engaged:
            raise SystemExit("kobold battle did not engage")
        s.wait_for(lambda: s.text_has("TARGET ")
                   or s.text_has("PLAYER TURN"), ticks=300)
        s.tick(30)
        # Take a hit: attack with the dealt swords/shields, then defend
        # with an empty combo so the telegraphed swing lands full.
        W._select_cards(s, (BT_SWORD, BT_SHIELD))
        s.press("select", settle=20)
        s.wait_for(lambda: s.text_has("DEFEND"), ticks=900)
        s.tick(30)
        s.press("select", settle=20)
        s.wait_for(lambda: s.text_has("PLAYER TURN")
                   or s.text_has("TARGET "), ticks=900)
        hp = r.battle_player_hp()
        print("  heal.gif: hero at %d HP after the swing" % hp)
        if hp >= HERO_START_HP:
            raise SystemExit("swing did not land (HP %d)" % hp)
        # Stage the heal card under the cursor and redraw the hand.
        base = r._resolve_battle_base() + BATTLE_HAND
        card = bytes([BT_HEAL, 5, 0xFF, 1,
                      CARD_EFFECT_HEAL_HP, 0, 0, 0])
        for i, b in enumerate(card):
            s.pb.memory[base + i] = b
        s.pb.memory[r._resolve_battle_base() + BATTLE_CURSOR] = 0
        daddr = r._resolve_battle_base() + DIRTY_OFF
        s.pb.memory[daddr] = s.pb.memory[daddr] | BATTLE_DIRTY_HAND
        s.tick(20)
        # Select, execute, and record the flash (plus its tails).
        frames = []
        s.press("a", settle=8)
        for _ in range(8):
            s.pb.tick()
            frames.append(s.pb.screen.image.copy())
        s.press("select", settle=2)
        for _ in range(72):
            s.pb.tick()
            frames.append(s.pb.screen.image.copy())
    finally:
        s.close()
    return frames


def capture_overworld():
    """Field spawn -> forest gate -> wander east through the trees."""
    checks = []
    planner = Planner()
    s = Session(checks, "gif-overworld")
    try:
        # Transit god-mode (walkthrough convention): 99 HP so a patrol
        # ambush can't derail the scripted walk.
        W.god_mode(s)
        field_pos = W._boot_to_field(s, planner)
        forest = W._level("forest")
        goal = (forest["player"]["spawn"]["x"],
                forest["player"]["spawn"]["y"])

        frames = []

        def cap_tick(n=1):
            for _ in range(n):
                s.pb.tick()
                frames.append(s.pb.screen.image.copy())

        s.tick = cap_tick

        # Walk the Field to the north gate; the crossing wipes to the Forest.
        W.follow(s, planner, "field", field_pos, "forest", goal)
        s.settle_scene(expected_scene=W.SCENE_FOREST)
        s.tick(20)

        # Wander east a few tiles and stop facing the trees (short and
        # far from the kobold's chase range so no battle interrupts the
        # recording; the amulet chest this used to visit moved to the
        # mountain pass).
        path = planner.path("forest", goal, (16, 10), avoid_exits=True)
        if path:
            W._walk_path(s, path)
        s.tick(40)
    finally:
        s.close()
    return frames


def capture_boss():
    """Lord of Slimes: walk to the castle, stage the completed Monster
    Hunt (memory write, bow-shot precedent -- the quest chain itself is
    covered by scenarios), enter the throne room so the boss spawns, and
    record ~3 s of idle battle: the eye-glow overlay blinks red/orange
    every 16 battle-clock ticks."""
    checks = []
    planner = Planner()
    s = Session(checks, "gif-boss")
    try:
        # Transit god-mode + boot out of the grove first (walkthrough
        # convention): follow() trusts its start position, and a fresh
        # boot lands in the grove, not the field.
        W.god_mode(s)
        field_entry = W._boot_to_field(s, planner)
        field = W._level("field")
        spawn = (field["player"]["spawn"]["x"],
                 field["player"]["spawn"]["y"])
        # Castle via the walk-E route, stopping at the interior arrival
        # tile (NOT the throne portal edge: stepping onto it would cross
        # into the throne room early).
        castle_arrival = planner.arrival_pos("castle") or spawn
        W.follow(s, planner, "field", field_entry, "castle", castle_arrival)
        # Cross into the throne room and stop one tile south of the boss
        # (position from the level JSON, so it follows content moves).
        throne = W._level("throne_room")
        lord = next(o for o in throne["objects"]
                    if (o.get("properties") or {}).get("entity_id")
                    == "ENTITY_ID_SLIME_LORD")
        lxy = (lord["position"]["x"], lord["position"]["y"])
        W.follow(s, planner, "castle", s.pos(), "throne_room",
                 (lxy[0], lxy[1] + 1))
        throne_id = planner.scenes["throne_room"].scene_id
        s.settle_scene(expected_scene=throne_id)
        if not s.text_has("DECK:"):
            s.check("boss engaged",
                    s.press_until("up", lambda: s.text_has("DECK:"),
                                  tries=10, settle=24, label="bump boss"),
                    expected="DECK:", actual="none")
        s.wait_for(lambda: s.text_has("TARGET ")
                   or s.text_has("PLAYER TURN"), ticks=300)
        s.tick(40)

        frames = []

        def cap_tick(n=1):
            for _ in range(n):
                s.pb.tick()
                frames.append(s.pb.screen.image.copy())

        s.tick = cap_tick
        # Idle battle: no input, so neither side acts; the timer drains
        # and the eyes blink (~11 flips in 180 ticks).
        s.tick(180)
    finally:
        s.close()
    bad = [c for c in checks if not c[2]]
    for label, name, _ok, expected, actual in bad:
        print("  [%s] %s\n      expected: %s\n      actual:   %s"
              % (label, name, expected, actual))
    if bad:
        raise SystemExit("boss capture checks failed")
    return frames


def main():
    ap = argparse.ArgumentParser(description="README GIF captures")
    ap.add_argument("--only", choices=["boot", "battle", "overworld", "boss",
                                     "heal"],
                    default=None)
    args = ap.parse_args()

    if args.only in (None, "boot"):
        save_gif(capture_boot(), BOOT_GIF, duration=90)
    if args.only in (None, "battle"):
        save_gif(capture_battle(), BATTLE_GIF, duration=50, keep=3)
    if args.only in (None, "heal"):
        # ~1.5 s at 30 fps: the white base, two slime-light pulses,
        # and the snap back to steady green.
        save_gif(capture_heal(), HEAL_GIF, duration=33, keep=2)
    if args.only in (None, "overworld"):
        # Smooth 30 fps for the camera scroll: record every frame, keep
        # every 2nd, 33 ms each.
        save_gif(capture_overworld(), OVERWORLD_GIF, duration=33, keep=2)
    if args.only in (None, "boss"):
        # ~3 s idle boss battle at 20 fps (every 3rd tick, 50 ms each).
        save_gif(capture_boss(), BOSS_GIF, duration=50, keep=3)
    return 0


if __name__ == "__main__":
    sys.exit(main())
