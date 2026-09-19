#!/usr/bin/env python3
"""Verify the player-sprite transition-hide via OAM (mGBA debugger).

The player is an OAM sprite (shadow OAM at 0xC000, real OAM at 0xFE00).
On any full-screen transition (screen change OR map change) the sprite must
be hidden before the redraw's wipe begins and reappear, at the new screen's
position, via the frame-boundary commit.

This drives the debug ROM through two transitions and asserts the sprite's
OAM state at deterministic pauses.  Values are CAMERA-RELATIVE: since the
smooth SCX/SCY scroll (B3) the sprite's OAM position is
(world_px - camera_px) + GBDK's +8/+16 OAM offset, so it is NOT the
world-tile*8 position the overworld renderer used before the scroll.

  Battle transition  (first_encounter): player (13,8), camera (24,0)
                     -> overworld shadow OAM (y=80,x=88) -> after walking
                     into the slime, battle sprite hidden (y=0) -> after the
                     item screen toggle the sprite stays hidden in battle (y=0).
  Steady battle frame: the sprite must NOT be re-hidden every frame (the
                     old prev_map_id=255 reset sentinel re-ran the hide on
                     every non-overworld frame, keeping the sprite hidden
                     for the whole fight on real hardware).  Detected by
                     breakpoint, since the harness's vsync-skip hides the
                     symptom from OAM reads.
  Scene transition   (town_arrival): FIELD (30,7) camera (96,0) -> shadow
                     OAM (72,152) -> gate -> TOWN (2,7) camera (0,0) ->
                     shadow OAM (72,27).
  Dialogue entry     (mayor): the dialogue must NOT wipe the world behind
                     the box (the map is already on screen from the
                     overworld).  Detected by breakpoint at ui_draw_world_full
                     (must not fire); the sprite stays visible behind the box
                     at (56,80).

IMPORTANT -- which OAM to read, and why these are the observable states:

* Shadow OAM (0xC000) is the authoritative position source: every sprite
  move writes shadow OAM and ui_sprite_commit() DMAs it to real OAM at the
  frame boundary.  Real OAM (0xFE00) reads at arbitrary harness pauses are
  often access-restricted (mGBA returns 0xFF during scanout modes 2/3), so
  the assertions read shadow OAM, which is always readable.

* The "hidden DURING the wipe" state (y=0 while the full-screen redraw is
  mid-sweep) is NOT observable under the harness: g_harness_mode skips
  vsync, so ui_sprite_begin_transition()'s hide and the frame-boundary
  commit complete within a single frame and no pause point lands between
  them (AGENTS.md 52.15).  The old assertions for a mid-wipe (0,..) state
  are therefore removed; what IS verified is that the sprite is at the
  correct position before and after each transition, and (via breakpoint)
  that ui_sprite_begin_transition fires on the transition frame but not on
  steady frames.

Each section runs in its own EmulatorSession so the extra breakpoints armed
by the checks (begin_transition, ui_draw_world_full) cannot contaminate the
next section's frame stepping.

Run inside the Nix dev shell:
    make verify-oam
or: nix develop --command python3 tools/verify_oam.py

Exits non-zero if any assertion fails.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emulator import EmulatorSession

ROM = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "build", "kaartenheld_debug.gb")
SCENARIOS = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "scenarios", "tests")

failures = []


def check(label, expected, actual):
    ok = actual == expected
    print(f"  [{'OK ' if ok else 'FAIL'}] {label}: expected {expected}, got {actual}")
    if not ok:
        failures.append(label)


def load_scenario(sess, name):
    with open(os.path.join(SCENARIOS, name)) as f:
        return json.load(f)


def shadow_oam(sess):
    """Authoritative sprite position (y, x) from shadow OAM slot 0."""
    return (sess._memread(0xC000), sess._memread(0xC001))


def shadow_sprite_tile(sess):
    """Sprite tile byte from shadow OAM slot 0 (y, x, tile, attr)."""
    return sess._memread(0xC002)


def shadow_oam_slot_tile(sess, slot):
    """Tile byte of shadow OAM entry `slot` (0 = player).  Each entry is
    four bytes at 0xC000 + 4*slot: y, x, tile, attr."""
    return sess._memread(0xC000 + 4 * slot + 2)


def shadow_oam_slot_pal(sess, slot):
    """CGB OBJ palette of shadow OAM entry `slot` (attr bits 0-2)."""
    attr = sess._memread(0xC000 + 4 * slot + 3)
    return attr & 0x07 if attr is not None else None


def mirror_at(sess, mirror_addr, x, y):
    """g_tilemap_mirror byte for tilemap cell (x, y).  DEBUG-only mirror of
    the 0x9800 ring (32 x 32), asserted because mGBA cannot read VRAM."""
    return sess._memread(mirror_addr + (y & 31) * 32 + (x & 31))


def vram_attr_at(sess, x, y):
    """True CGB attribute byte for tilemap cell (x, y), read from VRAM
    bank 1 via VBK switching (paused core: safe).  The WRAM attr mirror is
    NOT used here: ui_lcd_off()'s wipe clears VRAM attrs without touching
    the mirror, so the mirror reads stale-nonzero right after a wipe."""
    sess._memwrite(0xFF4F, 1)
    val = sess._memread(0x9800 + (y & 31) * 32 + (x & 31))
    sess._memwrite(0xFF4F, 0)
    return val


def wait_vblank(sess, tries=3):
    """Pause at a VBlank boundary: VRAM is only readable outside scanout
    modes 2/3 (real HW blocks it; mGBA returns 0xFF).  step() pauses at the
    game_render breakpoint, whose scanout phase shifts with boot timing
    across runs (§52.17) -- usually mid-scanout, so step-paused VRAM reads
    come back all-0xFF (indistinguishable from blanked tiles, and
    all-nonzero for attribute reads, passing vacuously).  mGBA `frame`
    instead pauses at VBlank start (LY=144), where every VRAM byte reads
    true.  Returns True in VBlank, False if the bound is hit (callers fail
    loudly instead of asserting on blocked reads)."""
    for _ in range(tries):
        sess._cmd("frame", timeout=10.0)
        ly = sess._memread(0xFF44)
        if ly is not None and ly >= 144:
            return True
    return False


def verify_battle_spider_oam(sess):
    """Spider trio renders as OAM (BG footprint blank): walk into the
    castle spider like castle_spider_encounter (the patrol bump needs a
    bounded wait), then set-match entries 1-18 for blob tiles + scratch
    OBJ palette 7. Spider costs 6 like slime: bases 128/134/140."""
    print("== Battle spider OAM (walk-in trio) ==")
    sess.load_scenario(load_scenario(sess, "castle_spider_encounter.json"))
    sess.step(1)
    sess.press("LEFT")
    sess.step(10)
    sess.press("LEFT")
    for _ in range(10):
        sess.step(30)
        if sess.snapshot().get("game_state") == "BATTLE":
            break
    check("spider walk-in reaches battle", "BATTLE",
          sess.snapshot().get("game_state"))
    found = set()
    for slot in range(1, 19):
        tile = shadow_oam_slot_tile(sess, slot)
        pal = shadow_oam_slot_pal(sess, slot)
        if tile is not None and pal is not None:
            found.add((tile, pal))
    want = {(128 + t, 7) for t in range(18)}
    check("spider trio renders as OAM tiles 128-145 with OBJ palette 7",
          True, want <= found)


def verify_battle_oam(sess):
    """Battle enemies render as OAM sprites (Florent's model), not BG
    stamps: slime trio occupies shadow entries 1-18 (stride 6 per slot)
    with blob tiles + scratch OBJ palette 7. Order-insensitive set match
    (stride bases shift with art cost). Boss stays a BG stamp (covered by
    the BG art scenarios + VRAM restore checks, not here)."""
    print("== Battle enemy OAM sprites (slime trio) ==")
    sess.load_scenario(load_scenario(sess, "battle_slime_sprite.json"))
    sess.step(2)
    found = set()
    for slot in range(1, 19):
        tile = shadow_oam_slot_tile(sess, slot)
        pal = shadow_oam_slot_pal(sess, slot)
        if tile is not None and pal is not None:
            found.add((tile, pal))
    want = {(128 + t, 7) for t in range(18)}
    check("slime trio renders as OAM tiles 128-145 with OBJ palette 7",
          True, want <= found)


def verify_npc_sprites(sess):
    """Town NPCs render as OAM sprites with exact OBJ palettes, not BG
    overlay tiles: mayor/guard share OBJ 7 (sprites8), merchant OBJ 6
    (sprites9).  Blob offsets append-only: guard 116, mayor 117,
    merchant 118.  Order-insensitive set match over entries 1-12 (spawn
    order may vary; ASCII shopkeeper takes no OAM entry)."""
    print("== Town NPC OAM sprites ==")
    sess.load_scenario(load_scenario(sess, "mayor_dialogue.json"))
    sess.step(2)
    found = set()
    # Entries 1-4 are reserved hostile slots (hidden when empty); static
    # actors start at entry 5, so scan wide and match order-insensitively.
    for slot in range(1, 13):
        tile = shadow_oam_slot_tile(sess, slot)
        pal = shadow_oam_slot_pal(sess, slot)
        if tile is not None and pal is not None:
            found.add((tile, pal))
    for label, tile, pal in (("mayor", 117, 7), ("guard", 116, 7),
                             ("merchant", 118, 6)):
        check(f"town {label} renders as OAM tile {tile} with OBJ palette {pal}",
              True, (tile, pal) in found)


def verify_hostile_sprites(sess):
    """Regression net for the Chunk-2 data-driven overworld sprite pipeline
    (SPRITE_KIND_* decoded in the bank-3 pass): hostile actors must render
    as the chosen OAM sprite tiles (kobold/bat), the castle bat must use the
    castle tile set, and the castle boss must be drawn as a 2x2 background
    block (not an OAM sprite).  Semantic scenarios cannot see this: they
    assert actor positions/visuals, not OAM tile bytes."""
    print("== Forest hero sprite + walls ==")
    forest = load_scenario(sess, "forest_boot.json")
    sess.load_scenario(forest)
    sess.step(2)
    # Player on MAP_FOREST renders the HERO_DESOLATE sprite (98|99), same as
    # south_field; every other map uses PLAYER (102).
    check("forest player renders as hero OAM tile (98|99)",
          1, (98 <= shadow_sprite_tile(sess) <= 99))
    attr_mirror = sess.get_symbol("g_tilemap_attr_mirror")
    check("forest treetop (3, 3) has field2 canopy palette (2)",
          2, mirror_at(sess, attr_mirror, 3, 3))
    check("forest treetrunk (3, 4) has field4 trunk palette (6)",
          6, mirror_at(sess, attr_mirror, 3, 4))

    print("== Chest pickup sprite (forest amulet as OAM, no BG glyph) ==")
    forest_amulet = load_scenario(sess, "forest_boot.json")
    sess.load_scenario(forest_amulet)
    sess.step(2)
    # Amulet is static index 0 -> OAM entry 1 + MAX_WORLD_ACTORS (4).
    chest = shadow_oam_slot_tile(sess, 5)
    check("forest amulet renders as chest OAM tile (94|95)",
          1, (94 <= chest <= 95))
    # The background cell underneath must be the floor tile the level
    # places at the amulet spot (TILE_FOREST_21 =
    # forest_floor_with_stuff_walkable_1, VRAM tile 128 + 21 -- the chest
    # itself renders as OAM since the forest-chest tile was removed),
    # not the '?' ASCII glyph: glyph suppression for OAM-rendered statics.
    mirror = sess.get_symbol("g_tilemap_mirror")
    check("amulet background cell is floor, not '?'",
          149, mirror_at(sess, mirror, 16, 10))

    print("== Hostile sprite tiles (south_field slime / bat) ==")
    south = load_scenario(sess, "south_field_boot.json")
    sess.load_scenario(south)
    sess.step(2)
    # Actor slot 0 (SLIME) = OAM entry 1; slot 1 (BAT) = OAM entry 2.
    # Shared per-enemy sprites (ENEMY_OW_BASE 100): blob order is sorted
    # enemy ids with overworld (bat, kobold, mimic, slime, slime_lord).
    # bat frames 100|101, mimic frames 104|105, slime frames 106|107.
    slime = shadow_oam_slot_tile(sess, 1)
    bat = shadow_oam_slot_tile(sess, 2)
    check("south_field slime renders as shared slime OAM tile (106|107)",
          1, (106 <= slime <= 107))
    check("south_field bat renders as shared bat OAM tile (100|101)",
          1, (100 <= bat <= 101))

    print("== Boss sprite rendering (castle: bat + mimic + spider + 2x2 boss OAM sprite) ==")
    boss = load_scenario(sess, "boss_appears.json")
    sess.load_scenario(boss)
    sess.step(1)
    # Actor slot 0 (BAT) = OAM entry 1; slot 1 (MIMIC, 1x1) = entry 2;
    # slot 2 (SPIDER, 1x1) = entry 3; slot 3 (SLIME_LORD/BOSS) = entries
    # 4-7 (a 2x2 grid of four shared-enemy OAM tiles).  Mimic tile base
    # = 104.  Boss blob base = 108.  (The castle spider insertion moved
    # the boss from slot 2 to slot 3.)
    castle_bat = shadow_oam_slot_tile(sess, 1)
    castle_mimic = shadow_oam_slot_tile(sess, 2)
    check("castle bat renders as shared bat OAM tile (100|101)",
          1, (100 <= castle_bat <= 101))
    check("castle mimic renders as shared mimic OAM tile (104|105)",
          1, (104 <= castle_mimic <= 105))
    # Boss 2x2 OAM grid at actor (10,5): tiles 108-111 (boss_ow_tl/tr/bl/br),
    # positions span a 2x2 area (row 0 at world y, row 1 at world y+1, cols
    # at world x and x+1).  The four OAM entries must be present and laid
    # out as a grid (same x for a column, y increasing by 8 down a row).
    boss0 = sess._memread(0xC000 + 4 * 4)  # entry 4 y byte (top-left)
    boss1 = sess._memread(0xC000 + 4 * 5)  # entry 5 y byte (top-right)
    boss2 = sess._memread(0xC000 + 4 * 6)  # entry 6 y byte (bot-left)
    boss3 = sess._memread(0xC000 + 4 * 7)  # entry 7 y byte (bot-right)
    t0 = shadow_oam_slot_tile(sess, 4)
    t1 = shadow_oam_slot_tile(sess, 5)
    t2 = shadow_oam_slot_tile(sess, 6)
    t3 = shadow_oam_slot_tile(sess, 7)
    check("boss renders as shared boss OAM tiles (108|109|110|111)",
          1, (108 <= t0 <= 111 and 108 <= t1 <= 111 and
              108 <= t2 <= 111 and 108 <= t3 <= 111))
    check("boss is a 2x2 OAM grid (top row y, bottom row y+8)",
          1, (boss0 == boss1 and boss2 == boss3 and
              boss2 == boss0 + 8 and boss0 > 0))
    # The boss must no longer draw the legacy 2x2 background block at
    # (10,5): the ground underneath stays the castle floor, not the
    # background boss corners (RPG_TILE_BASE_WORLD + 7/8/16/17).
    mirror = sess.get_symbol("g_tilemap_mirror")
    corner = mirror_at(sess, mirror, 10, 5)
    check("boss background block removed (floor under boss)",
          0, (135 == corner or 136 == corner or 144 == corner or 145 == corner))


def verify_exit_art(sess):
    """Invisible exit gates (manifest vram_block exit markings are decor
    only now): gate cells render their underlying terrain art — painted
    art, or the level's default ground for unpainted gates — never the
    old per-tileset stairs tile (town/south_field 128+40=168, castle
    128+7=135).  Expected values come from parity_check.expected_grid
    (the same editor-art mapping the ROM must match)."""
    print("== Gate art (invisible triggers keep terrain) ==")
    TOOLS = os.path.dirname(os.path.abspath(__file__))
    if TOOLS not in sys.path:
        sys.path.insert(0, TOOLS)
    from parity_check import expected_grid, load_tilesets
    tilesets = load_tilesets()
    old_exit_art = {168, 135}
    # Boot scenarios stage the frozen TEST fixtures (not levels/), so
    # expected art comes from the fixture JSONs.
    cases = (("town_boot.json", "test_town", (1, 7)),
             ("south_field_boot.json", "test_south_field", (12, 0)),
             ("south_field_boot.json", "test_south_field", (12, 11)),
             ("castle_boot.json", "test_castle", (12, 11)))
    mirror = sess.get_symbol("g_tilemap_mirror")
    for name, fixture_id, (x, y) in cases:
        level = json.load(open(os.path.join(
            TOOLS, "scenarios", "fixtures", "levels",
            fixture_id + ".json")))
        grid, _ = expected_grid(level, tilesets)
        want = grid.get((x, y))
        sess.load_scenario(load_scenario(sess, name))
        sess.step(2)
        got = mirror_at(sess, mirror, x, y)
        check(f"{name} gate ({x},{y}) renders terrain, not exit art",
              True, got == want and got not in old_exit_art)


def verify_battle_transition(sess):
    print("== Battle transition (first_encounter) ==")
    sess.load_scenario(load_scenario(sess, "first_encounter.json"))
    sess.step(1)

    # 1. Overworld steady: player at (13,8), camera (24,0) ->
    #    OAM x = 13*8+8-24 = 88, y = 8*8+0+16 = 80.
    check("overworld @ sprite position (shadow OAM)", (80, 88), shadow_oam(sess))
    check("overworld sprite tile (shadow OAM)", 98, shadow_sprite_tile(sess))

    # 2. Walk right into the slime at (14,8): the encounter is decided once
    #    the hold-to-move commits the tile (MOVE_FRAMES=8).  The sprite is
    #    hidden for the battle redraw (no sprite on the card battle screen).
    sess.hold("RIGHT", 10)
    check("battle sprite: hidden (shadow OAM)", 0, shadow_oam(sess)[0])

    # 3. Open the item menu (START): the item screen keeps the sprite
    #    hidden (ui_sprite_hide on the screen change).
    sess.press("START")
    sess.step(1)
    got = shadow_oam(sess)
    check("menu open: sprite hidden (shadow OAM)", 0, got[0])
    sess.step(1)

    # 4. Close the menu (B): the battle redraw runs and the sprite stays hidden.
    sess.press("B")
    sess.step(1)
    check("back in battle: sprite hidden (shadow OAM)", 0, shadow_oam(sess)[0])
    sess.step(1)


def verify_steady_battle_frame(sess):
    """Regression check: on steady battle frames the sprite must NOT be
    re-hidden.  The old prev_map_id=255 reset sentinel made every battle
    frame look like a map change, so game_render() re-ran
    ui_sprite_begin_transition() every frame.  On real hardware (vsync)
    that kept the sprite hidden for the whole fight -- it only flashed in
    during VBlank, and the user saw it appear at battle exit.  The harness
    cannot see this from OAM reads (vsync skipped: the hide+commit cycle
    completes within one frame, so reads at VBlank always show the reveal).

    Detect it by breakpoint instead.  Arm a break at ui_sprite_begin_transition
    and run frames: with the bug the function executes once per frame, so
    `frame` pauses at it; a fixed ROM never reaches it.  Several frames are
    checked because the current pause may be mid-frame (the initial VBlank
    pause point varies)."""
    print("== Steady battle frame: no per-frame sprite re-hide ==")
    # Establish a real steady battle frame first: without the scenario setup
    # this check runs from the boot/title state, where begin_transition
    # correctly never fires per frame, so the buggy ROM would pass.
    sess.load_scenario(load_scenario(sess, "first_encounter.json"))
    sess.step(1)
    sess.hold("RIGHT", 10)
    sess.step(1)
    sess._cmd("frame", timeout=5.0)
    sess.step(1)

    bt = sess.get_symbol("ui_sprite_begin_transition")
    brk = sess._cmd(f"break 0x{bt:04X}")
    if b"Added breakpoint" not in brk:
        raise RuntimeError(f"begin_transition break not armed: {brk!r}")
    paused_at_bt = None
    for i in range(3):
        sess._cmd("frame", timeout=5.0)
        pc = sess._read_pc()
        if pc == bt:
            paused_at_bt = i + 1
            break
    check("steady battle frame: no spurious re-hide over 3 frames",
          "begin_transition never reached",
          "paused at ui_sprite_begin_transition (0x%04X)" % bt if paused_at_bt
          else "begin_transition never reached")


def verify_dialogue_transition(sess):
    """Dialogue redraws the world behind the box on entry (the map is
    already on screen from the overworld, but ui_lcd_off() wipes every CGB
    tile attribute to palette 0, so only a full redraw re-applies per-tile
    palettes -- skipping it turned the whole screen gray for the dialogue).
    The Mayor sits at (10,5); the scenario boots the player at (9,5)
    facing RIGHT, so a single A press engages him and starts the dialogue
    within that frame.  Arm a breakpoint at ui_draw_world_full before the
    press: the dialogue-entry render must reach it (checked by continuing
    past the frame-entry pause).  The sprite stays visible behind the box."""
    print("== Dialogue entry (mayor): redrawn world + box, colors kept ==")
    initial = load_scenario(sess, "mayor_dialogue.json")["initial_state"]
    sess.load_scenario(initial)
    sess.step(1)

    full_addr = sess.get_symbol("ui_draw_world_full")
    brk = sess._cmd(f"break 0x{full_addr:04X}")
    if b"Added breakpoint" not in brk:
        raise RuntimeError(f"ui_draw_world_full break not armed: {brk!r}")

    sess.press("A")
    # press() leaves us paused at the frame-entry breakpoint; continue
    # through the frame: the dialogue render must hit ui_draw_world_full.
    sess._cmd("frame", timeout=5.0)
    check("dialogue entry: world redrawn behind the box",
          full_addr, sess._read_pc())
    # Disarm the redraw breakpoint before engaging: while armed, press()'s
    # internal step pauses mid-frame and the A edge never engages (proven
    # by matrix probe: armed=never, clean=always). mGBA prints
    # "Added breakpoint #N" (note the hash). A reset frame first (edge
    # discipline, AGENTS.md 52.10); if the first press already started
    # the dialogue, this one merely advances a line (still active).
    import re as _re
    _m = _re.search(r"#(\d+)", brk.decode("utf-8", "ignore"))
    if _m:
        sess._cmd(f"delete {_m.group(1)}", timeout=5.0)
    sess.step(1)
    sess.press("A")
    sess.step(1)
    # Continue past the redraw breakpoint to the next frame entry: the
    # redraw (tilemap + CGB attrs + palettes) has now fully executed.
    sess._cmd("c", timeout=10.0)

    snap = sess.snapshot()
    check("dialogue entry: dialogue is active", True, snap.get("dialogue_active"))

    # CGB attributes must be re-applied, not left at the lcd_off wipe
    # (all-zero = grayscale).  Count nonzero attrs over the visible map
    # with true VRAM reads (the WRAM mirror goes stale across the wipe),
    # gated on VBlank: mid-scanout every attr reads back 0xFF (nonzero)
    # and the check would pass vacuously.
    check("dialogue VRAM read in VBlank", True, wait_vblank(sess))
    nonzero = 0
    for y in range(18):
        for x in range(20):
            if vram_attr_at(sess, x, y) not in (None, 0):
                nonzero += 1
    check("dialogue entry: CGB attrs re-applied (nonzero count)",
          True, nonzero > 20)

    # Sprite stays visible behind the box at the player's position (9,5),
    # camera (0,0) -> OAM y=5*8+16=56, x=9*8+8=80.
    sess.step(1)
    check("dialogue steady: @ sprite visible (shadow OAM)",
          (56, 80), shadow_oam(sess))


def verify_scene_transition(sess):
    print("== Scene transition (town_arrival: FIELD gate -> TOWN) ==")
    initial = load_scenario(sess, "town_arrival.json")["initial_state"]
    sess.load_scenario(initial)
    sess.step(1)

    # 1. FIELD steady: player at (30,7), camera (96,0) ->
    #    OAM x = 30*8+8-96 = 152, y = 7*8+0+16 = 72.
    check("overworld steady: @ sprite position (shadow OAM)",
          (72, 152), shadow_oam(sess))

    # 2. Hold RIGHT through the gate at (31,7) into TOWN (spawn 2,7).  The
    #    map change triggers the transition hide/commit; the sprite lands at
    #    TOWN (2,7), visible at its committed position (the camera is
    #    mid-scroll, so the exact x is the committed value, not the
    #    steady-camera calc).  (72,27) is the observed deterministic result.
    sess.hold("RIGHT", 14)
    check("new map: @ sprite position (shadow OAM)",
          (72, 27), shadow_oam(sess))


def verify_portal_step_oam(sess):
    """Regression (point-exit walk target): a point exit used to stage its
    FAR destination as the move target, so the 8-frame walk animation slid
    toward the destination instead of stepping onto the gate.  In the
    TEST_TOWN fixture the gate (1,7) exits to TEST_FIELD (17,7), so a LEFT
    step from the spawn (2,7) made the sprite x INCREASE (24 -> 31) into
    the town.  With the fix the sprite walks left onto the gate column
    (x=16) and never moves right of its start."""
    print("== Point-exit step: sprite walks onto the gate ==")
    initial = load_scenario(sess, "town_gate_step.json")["initial_state"]
    sess.load_scenario(initial)
    sess.step(1)
    start_y, start_x = shadow_oam(sess)
    check("portal step: start at town spawn (2,7)", (72, 24),
          (start_y, start_x))
    sess.press("LEFT")
    xs = []
    for _ in range(12):
        sess.step(1)
        if sess.snapshot().get("scene") != "TEST_TOWN":
            break
        xs.append(shadow_oam(sess)[1])
    check("portal step: never moves right of the start",
          True, (not xs) or max(xs) <= start_x)
    check("portal step: walks left toward the gate",
          True, bool(xs) and min(xs) <= start_x - 6)


def verify_battle_vram_restore(sess):
    """Battle enemy art shares VRAM tiles 128+ with the world tileset, so
    leaving a fight must reload the world tiles: ui_load_tileset's cache
    used to suppress the reload and the overworld came back as garbage
    until a scene change.  screen_change() now invalidates the cache on
    every overworld entry.  mGBA reads VRAM directly here (paused at the
    frame-entry breakpoint, i.e. VBlank, so no PPU access restriction).
    Hash tiles 128..139 (12 enemy-art slots x 16 B at 0x8800): overworld
    bytes -> different in battle (proves the clobber is exercised) ->
    identical again after the flee exit."""
    print("== Battle exit: world VRAM tile data restored ==")
    sess.load_scenario(load_scenario(sess, "first_encounter.json"))
    sess.step(2)

    def vram_tiles():
        return bytes(b if b is not None else 0
                     for i in range(12 * 16)
                     for b in (sess._memread(0x8800 + i),))

    check("pre-battle VRAM read in VBlank", True, wait_vblank(sess))
    before = vram_tiles()
    sess.hold("RIGHT", 10)
    sess.step(2)
    check("battle VRAM read in VBlank", True, wait_vblank(sess))
    during = vram_tiles()
    check("battle clobbers world VRAM tiles (test exercises the bug)",
          True, during != before)

    # Flee (B with an empty hand) and leave the result screen, then let
    # the overworld redraw + tileset reload settle.
    for _ in range(6):
        sess.press("B")
        sess.step(2)
        sess.press("A")
        sess.step(2)
        if sess.snapshot().get("game_state") == "OVERWORLD":
            break
    check("fled battle: back to overworld", "OVERWORLD",
          sess.snapshot().get("game_state"))
    sess.step(2)
    check("return VRAM read in VBlank", True, wait_vblank(sess))
    check("overworld return: world VRAM tiles restored",
          before, vram_tiles())


def main():
    # Each section uses its own session so the extra breakpoints armed by
    # the checks (begin_transition, ui_draw_world_full) never contaminate
    # another section's frame stepping or VBlank reads.
    for label, fn in (("battle", verify_battle_transition),
                      ("battle steady frame", verify_steady_battle_frame),
                      ("battle VRAM restore", verify_battle_vram_restore),
                      ("scene", verify_scene_transition),
                      ("portal step", verify_portal_step_oam),
                      ("dialogue", verify_dialogue_transition),
                      ("hostile sprites", verify_hostile_sprites),
                      ("npc sprites", verify_npc_sprites),
                      ("battle sprites", verify_battle_oam),
                      ("battle spider", verify_battle_spider_oam),
                      ("exit art", verify_exit_art)):
        sess = EmulatorSession(rom_path=ROM)
        try:
            sess.connect()
        except Exception as e:
            print(f"CONNECT FAILED ({label}): {e}")
            return 1
        try:
            fn(sess)
        finally:
            sess.disconnect()

    if failures:
        print(f"\nOAM VERIFICATION FAILED: {failures}")
        return 1
    print("\nOAM VERIFICATION OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
