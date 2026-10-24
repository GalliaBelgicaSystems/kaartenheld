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
                     for the whole fight on real hardware).  Detected via
                     the hide counter staying at zero, since the
                     harness's vsync-skip hides the symptom from OAM
                     reads.
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
  correct position before and after each transition, and (via the
  DEBUG-only g_debug_hide_count execution counter) that
  ui_sprite_begin_transition ran on the transition frame but not on
  steady frames.  A breakpoint inside the banked wipe body was tried and
  removed: mGBA matches window-address (0x4000-0x7FFF) breakpoints
  bank-agnostically (false hits in fixed-bank _HOME code at the same
  address) and cannot resume past them (`c`/`frame` re-trap instantly,
  freezing the poll loop) -- see the bank-window note in
  verify_scene_transition and AGENTS.md 52.26.

Each section runs in its own EmulatorSession so debugger state (the
game_render frame-sync breakpoint connect() arms, watchpoints) cannot
contaminate the next section's frame stepping.

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


def bg_cram(sess):
    """All 64 CGB background CRAM bytes via BCPS/BCPD (paused core: safe).
    Returned as a list of 8 palettes x 4 RGB555 words, for asserting a
    transition loaded the new scene's palettes without Mode-3 drops."""
    raw = []
    for i in range(64):
        sess._memwrite(0xFF68, i)
        raw.append(sess._memread(0xFF69))
    return [[raw[p * 8 + c * 2] | (raw[p * 8 + c * 2 + 1] << 8)
             for c in range(4)] for p in range(8)]


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


def hide_count(sess):
    """Transition-hide execution count (g_debug_hide_count, DEBUG-only
    WRAM incremented by ui_sprite_begin_transition).  None if
    unreadable.  Reset with reset_hide_count() before driving, then
    assert the delta: execution proof without halting inside a banked
    ROM window (see bank-window note in verify_scene_transition)."""
    return sess._memread(sess.get_symbol("g_debug_hide_count"))


def reset_hide_count(sess):
    sess._memwrite(sess.get_symbol("g_debug_hide_count"), 0)


def verify_battle_spider_oam(sess):
    """Spider trio renders as OAM (BG footprint blank): walk into the
    castle spider like castle_spider_encounter (the patrol bump needs a
    bounded wait), then set-match entries 1-18 for blob tiles.  The eye
    cell (frame-relative index 1 of each 6-tile stride: tiles 129/135/141)
    rides the fightbat alt ramp in scratch OBJ palette 6
    (BATTLE_OBJ_SCRATCH2, screens/combat_art/spider.json obj_alt_cells);
    every other spider cell uses scratch palette 7."""
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
    eye = {(128 + 6 * s + 1, 6) for s in range(3)}
    rest = {(128 + t, 7) for t in range(18) if t % 6 != 1}
    check("spider eye cells render on alt OBJ palette 6",
          True, eye <= found)
    check("spider non-eye cells render on OBJ palette 7",
          True, rest <= found)


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
    (sprites9), heal mage OBJ 7 (sprites8) on the wizard's shared blob
    slot.  Blob offsets append-only: guard 116, mayor 117, merchant
    118, wizard + heal mage 119.  Order-insensitive set match over
    entries 1-12 (spawn order may vary; ASCII shopkeeper takes no OAM
    entry)."""
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
                             ("merchant", 118, 6), ("heal_mage", 119, 7),
                             ("wizard", 119, 5)):
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
    # Player on MAP_FOREST renders the standing-hero idle sprite (96|97),
    # same as south_field; moving heroes show walk frames 98|99.
    check("forest player renders as hero idle OAM tile (96|97)",
          1, (96 <= shadow_sprite_tile(sess) <= 97))
    attr_mirror = sess.get_symbol("g_tilemap_attr_mirror")
    check("forest treetop (3, 3) has field2 canopy palette (2)",
          2, mirror_at(sess, attr_mirror, 3, 3))
    check("forest treetrunk (3, 4) has field4 trunk palette (6)",
          6, mirror_at(sess, attr_mirror, 3, 4))

    print("== Chest OAM sprite (forest amulet closed, then open after loot) ==")
    forest_amulet = load_scenario(sess, "forest_boot.json")
    sess.load_scenario(forest_amulet)
    sess.step(2)
    # Amulet is the first static -> OAM entry 1 + MAX_WORLD_ACTORS (5).
    # Closed-chest frame (shared-blob tile 122: base 100 + offset 22, after
    # the slime-knight walk row); the floor cell stays plain floor (VRAM
    # tile 128 + 62 = 190), never chest paint.
    check("forest amulet owns closed-chest OAM sprite (tile 122)",
          122, shadow_oam_slot_tile(sess, 5))
    # The background cell itself stays plain floor (the chest is a
    # SPRITE_KIND_CHEST OAM sprite, not floor paint).
    mirror = sess.get_symbol("g_tilemap_mirror")
    check("amulet cell paints plain floor (190), not chest art",
          190, mirror_at(sess, mirror, 16, 10))
    # Loot it through the real interact path (amulet_open_sprite spawns
    # the player beside the amulet facing it): A opens the pickup
    # dialogue, A closes it.  The sprite must flip to the open frame
    # (123) and stay there across redraws.
    loot = load_scenario(sess, "amulet_open_sprite.json")
    sess.load_scenario(loot)
    sess.step(2)
    sess.press("A")
    sess.step(10)
    sess.press("A")
    sess.step(20)
    check("looted amulet shows open-chest OAM sprite (tile 123)",
          123, shadow_oam_slot_tile(sess, 5))
    check("looted amulet cell still plain floor (190)",
          190, mirror_at(sess, mirror, 16, 10))

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


def verify_boss_glow_overlay(sess):
    """Boss eye-glow overlay (BG stamp + OAM eye sprites): the stamp alone
    cannot show the red/orange eyes (one 4-shade BG palette for a 6-colour
    set), so the two eye cells (frame0 indices 3,4) redraw as OAM sprites
    over the stamp, flipping OBJ palettes 6 (unlit, fightboss2) and 7
    (lit, fightboss3) on the battle clock. The harness cannot see this:
    SameBoy OAM reads are blind to sprite rendering and CRAM is not
    inspectable, so this execution check is the gate (AGENTS.md 52.15).

    Expects entries 4,5 (solo slot stride base 1 + cells 3,4) at the
    stamp-aligned pixels carrying the stamp's own tile ids (base 128 ->
    131,132), with the palette bit seen on BOTH 6 and 7 across flips.
    Negative tests: on a ROM without the overlay the entries stay hidden
    (y == 0); in a slime-trio battle no entry carries a boss tile on
    palette 6 (proves boss-specificity, no leak into other battles)."""
    print("== Boss glow overlay (eye sprites over BG stamp) ==")
    sess.load_scenario(load_scenario(sess, "boss_battle_ending.json"))
    for _ in range(10):
        sess.step(30)
        if sess.snapshot().get("game_state") == "BATTLE":
            break
    check("boss glow: reached battle", "BATTLE",
          sess.snapshot().get("game_state"))

    def eye_state(slot):
        base = 0xC000 + 4 * slot
        return (sess._memread(base), sess._memread(base + 1),
                sess._memread(base + 2), sess._memread(base + 3))

    seen = set()
    tiles_ok = False
    for _ in range(40):
        sess.step(2)
        states = (eye_state(4), eye_state(5))
        if states == ((48, 80, 131, states[0][3]),
                      (48, 88, 132, states[1][3])):
            tiles_ok = True
        for y, _x, _t, attr in states:
            if y == 48 and attr is not None:
                seen.add(attr & 0x07)
    check("boss glow: eye sprites at stamp cells (entries 4,5 = tiles 131,132 at (48,80)/(48,88))",
          True, tiles_ok)
    check("boss glow: palette flips 6 (unlit) <-> 7 (lit)",
          True, {6, 7} <= seen)

    # No leak: a slime-trio battle must never show boss tiles on palette 6.
    sess.load_scenario(load_scenario(sess, "battle_slime_sprite.json"))
    sess.step(2)
    leak = False
    for slot in range(1, 19):
        tile = shadow_oam_slot_tile(sess, slot)
        pal = shadow_oam_slot_pal(sess, slot)
        if tile in (131, 132) and pal == 6:
            leak = True
    check("boss glow: no overlay leak into slime battle", False, leak)


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
    #    OAM x = 13*8+8-24 = 88, y = 8*8+0+16 = 80.  Standing hero shows
    #    the idle frames (96|97).
    check("overworld @ sprite position (shadow OAM)", (80, 88), shadow_oam(sess))
    check("overworld sprite tile (shadow OAM)", 1, (96 <= shadow_sprite_tile(sess) <= 97))

    # 2. Walk right into the slime at (14,8): Carl teaches first, so the
    #    bump opens his lesson (dialogue); closing it auto-starts the spar
    #    from the stashed encounter.  The sprite is hidden for the battle
    #    redraw (no sprite on the card battle screen).
    sess.hold("RIGHT", 10)
    for _ in range(12):
        sess.press("A")
        sess.step(20)
        if sess.snapshot().get("screen") == "BATTLE":
            break
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
    # Walk-in mirrors verify_battle_transition: Carl teaches first, so the
    # bump opens his lesson (dialogue) and closing it auto-starts the spar
    # -- without the A presses this section lands in Carl's dialogue (hide
    # correctly skipped by the overlay path) and the assertion is vacuous.
    sess.load_scenario(load_scenario(sess, "first_encounter.json"))
    sess.step(1)
    sess.hold("RIGHT", 10)
    reached = False
    for _ in range(12):
        sess.press("A")
        sess.step(20)
        if sess.snapshot().get("screen") == "BATTLE":
            reached = True
            break
    check("steady battle frame: reached battle", True, reached)

    # Settle: the entry transition's render (and its hide) may still be
    # pending when BATTLE first observes -- those hides pre-date the
    # reset below and must not leak into the 3-frame window, or a late
    # entry hide fails the check spuriously.
    sess.step(10)

    # Execution proof via the hide counter (no window breakpoint: see
    # the bank-window note in verify_scene_transition).  A spurious
    # re-hide on steady battle frames increments it; three clean frames
    # must leave it at zero.  Drive with step(1): raw `frame` does not
    # advance the guest from a step-paused state (40 frames, zero
    # movement observed) while step() crosses in ~8.
    reset_hide_count(sess)
    for i in range(3):
        sess.step(1)
    check("steady battle frame: no spurious re-hide over 3 frames",
          0, hide_count(sess))


def verify_dialogue_transition(sess):
    """Dialogue entry is an overlay: the map stays on screen (no LCD-off
    wipe, no full world redraw), the box paints over rows 14-17.  The Mayor
    sits at (10,5); the scenario boots the player at (9,5) facing RIGHT,
    so a single A press engages him and starts the dialogue within that
    frame.  Asserts: dialogue activates, speaker bar is painted, CGB attrs
    stay applied (no grayscale wipe), sprite stays visible behind the box.
    NOTE: these prove the overlay is correct, NOT that no LCD-off happened --
    a full LCD-off redraw would pass all four.  The no-flash property itself
    is timing (LCD never blanks) and needs a VBlank/LCDC execution check
    or walkthrough-frame evidence, not state reads (same family as
    AGENTS.md 52.15).
    (The old breakpoint-at-ui_draw_world_full form asserted the previous
    architecture -- full redraw behind the box -- and its CPU-address break
    collides with banked code at the same offset, firing spuriously.)"""
    print("== Dialogue entry (mayor): overlay box, colors kept ==")
    initial = load_scenario(sess, "mayor_dialogue.json")["initial_state"]
    sess.load_scenario(initial)
    sess.step(1)

    sess.press("A")
    sess.step(1)
    if not sess.snapshot().get("dialogue_active"):
        # Edge-triggered input (AGENTS.md 52.10): the A edge can land a
        # frame late (mask-write vs step host race), so a bare extra step
        # is not enough -- the step is the reset frame, then re-press.
        sess.step(1)
        sess.press("A")
        sess.step(1)

    snap = sess.snapshot()
    check("dialogue entry: dialogue is active", True, snap.get("dialogue_active"))

    # Box painted over the live map: row 14 is the speaker top bar
    # ("+--SPEAKER-----+"), so '-' must be present.  A plain non-blank
    # test would pass vacuously (world glyphs fill rows 14-17 anyway).
    buf = sess.get_screen_buf().split("\n")
    box = "".join(buf[14:18]) if len(buf) >= 18 else ""
    check("dialogue entry: speaker bar painted (row 14 has '-')",
          True, "-" in box)

    # CGB attributes must stay applied, not wiped to grayscale.
    # Gated on VBlank: mid-scanout every attr reads back 0xFF (nonzero)
    # and the check would pass vacuously.
    check("dialogue VRAM read in VBlank", True, wait_vblank(sess))
    nonzero = 0
    for y in range(18):
        for x in range(20):
            if vram_attr_at(sess, x, y) not in (None, 0):
                nonzero += 1
    check("dialogue entry: CGB attrs kept (nonzero count)",
          True, nonzero > 20)

    # Paper ramp integrity: slot 4 must equal the dialogue paper ramp
    # (white/grey/dark/black).  The paper loop runs LCD-on with no PPU
    # wait in the old code, so a Mode-3 drop splices a stale byte into
    # the ramp and the whole box background changes color (caught live
    # as black/tan patches depending on slot-4 history).  Already in
    # VBlank from the gate above: CRAM reads are exact here.
    paper = bg_cram(sess)[4]
    check("dialogue entry: paper ramp intact (slot 4)",
          [0x7FFF, 0x6739, 0x294A, 0x0000], paper)

    # Sprite stays visible behind the box at the player's position (9,5),
    # camera (0,0) -> OAM y=5*8+16=56, x=9*8+8=80.
    sess.step(1)
    check("dialogue steady: @ sprite visible (shadow OAM)",
          (56, 80), shadow_oam(sess))

    # Exit: closing the dialogue restores just the 4 box rows (no LCD-off
    # wipe).  The speaker bar ('-'/'+"'+'" row 14) must be gone -- world
    # glyphs never contain those -- while the map and sprite are intact.
    # One A press per page (mayor intro is 3 pages); the step(3) settle
    # after each press doubles as the reset frame consecutive same-button
    # presses need (edge-triggered input, AGENTS.md 52.10), and the loop
    # is bounded so a genuinely stuck box still fails loudly.
    for _ in range(5):
        sess.press("A")
        sess.step(3)
        if not sess.snapshot().get("dialogue_active"):
            break
    sess.step(2)
    check("dialogue exit: back to overworld", "OVERWORLD",
          sess.snapshot().get("screen"))
    buf = sess.get_screen_buf().split("\n")
    row14 = buf[14] if len(buf) >= 15 else ""
    check("dialogue exit: box rows restored ('+' bar gone from row 14)",
          True, "+" not in row14 and "-" not in row14)
    check("dialogue exit: @ sprite visible (shadow OAM)",
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

    # 3. Stale-prev regression (prior dialogue closed on the overworld):
    #    g->prev_screen stays DIALOGUE across the whole OW session, and the
    #    old game_render() misclassified the next gate crossing as a
    #    dialogue overlay -- skipping ui_sprite_begin_transition() so
    #    Screen A's sprites floated over the sweep until the tail
    #    re-resolved OAM.  Reload for a clean session, poke the exact
    #    post-dialogue staleness (g_game+1, the prev_screen byte that
    #    screen_change() leaves behind on dialogue close), and drive
    #    through the gate asserting the hide RAN (counter delta == 1).
    #
    #    Bank-window note: this used to halt at the wipe body via a CPU
    #    breakpoint, but mGBA matches those bank-agnostically (false hits
    #    in fixed-bank _HOME code at the same address) AND cannot resume
    #    past them (`c`/`frame` re-trap instantly at a 0x4000-0x7FFF halt,
    #    freezing the poll loop).  A fixed-layout shift once parked
    #    ui_invalidate_tileset on the wipe address and failed the check
    #    spuriously with live OAM.  The counter proves execution with no
    #    window halt at all (AGENTS.md 52.26).
    sess.load_scenario(initial)
    sess.step(1)
    sess._memwrite(sess.get_symbol("g_game") + 1, 1)  # SCREEN_DIALOGUE
    reset_hide_count(sess)
    import time as _time
    mask_addr = sess.get_symbol("g_inp_mask")
    right = sess.button_masks["RIGHT"]
    # Drive with step(1) (hold-style mask rewrite per frame, like
    # hold()): a raw `frame` loop does NOT advance the guest here (40
    # frames, zero movement observed) while step() crosses in ~8.
    # The scene flips in game_update, one render behind the halt.
    crossed = False
    lcd_off_frames = 0
    for _ in range(30):
        sess._memwrite(mask_addr, right)
        _time.sleep(0.005)
        sess.step(1)
        # Sample LCDC on the driven frames: the converse stale-prev bug
        # blanked the display via ui_lcd_off, and a white flash would
        # span dozens of frames, so all-on samples prove it never
        # blanked.  (A separate post-arrival loop cannot work: it would
        # start settled in TOWN and exit after ~1 frame.)
        if not (sess._memread(0xFF40) & 0x80):
            lcd_off_frames += 1
        if sess.snapshot().get("scene") == "TEST_TOWN":
            crossed = True
            break
    check("gate crossing reached TOWN", True, crossed)
    # Step past the crossing frame's render (hide + wipe + settle) --
    # extra steady frames add no hides -- then assert exactly one hide.
    sess.step(2)
    check("gate after dialogue: transition hide ran exactly once",
          1, hide_count(sess))
    check("gate crossing: LCD never blanks (no white flash)", 0,
          lcd_off_frames)

    # 4. Palette integrity across the LCD-on load: TOWN CRAM after the
    #    gate crossing must equal a fresh TOWN boot on every slot except
    #    4 (held black by the wipe; free in every world tileset), proving
    #    zero Mode-3 palette drops.  VBlank-gated: mid-scanout CRAM reads
    #    back 0xFF and pass vacuously.
    check("town CRAM read in VBlank", True, wait_vblank(sess))
    sess.load_scenario(load_scenario(sess, "town_boot.json"))
    sess.step(2)
    check("fresh town boot read in VBlank", True, wait_vblank(sess))
    ref_cram = bg_cram(sess)
    sess.load_scenario(initial)
    sess.step(1)
    sess.hold("RIGHT", 14)
    check("town CRAM read in VBlank (arrival)", True, wait_vblank(sess))
    arr_cram = bg_cram(sess)
    check("gate crossing: slot 4 held black",
          [0, 0, 0, 0], arr_cram[4])
    same = all(ref_cram[p] == arr_cram[p]
               for p in (0, 1, 2, 3, 5, 6, 7))
    check("gate crossing: TOWN CRAM matches fresh boot (slots 0-3,5-7)",
          True, same)


def verify_restart_clears_boss_oam(sess):
    """Regression (boss ghost after new game): the actor loader clears
    only each runtime slot's `active` flag, leaving sprite_kind/ow_type
    stale.  The sprite body's multi-tile ENEMY grid block used to ignore
    `active`, so an inactive slot keeping the castle boss's 2x2 kind
    redrew the boss every frame on maps without one -- visible at the
    wake grove (here TEST_FIELD) after killing the end boss and starting
    a new game, until some map overwrote the slot.  Fixed by gating the
    grid block on active; stale kinds are now inert.
    Drives the REAL game_restart() through the debugger (pause at the
    game_render breakpoint connect() arms, where de already holds &g_game
    from the main loop, then jump straight into game_restart): no
    test-only shortcuts, the same code the ending screen runs."""
    print("== Restart (boss castle -> new game): no boss ghost in OAM ==")
    initial = load_scenario(sess, "boss_appears.json")["initial_state"]
    sess.load_scenario(initial)
    sess.step(2)

    # Precondition (52.16: never assert on a state we did not reach): the
    # castle boss must own entries 4-7 visibly, or the post-restart reads
    # below would pass vacuously on a broken setup.
    pre_tiles = [shadow_oam_slot_tile(sess, i) for i in range(4, 8)]
    check("restart setup: castle boss owns OAM entries 4-7",
          [108, 109, 110, 111], pre_tiles)
    pre_y = [sess._memread(0xC000 + 4 * i) for i in range(4, 8)]
    check("restart setup: boss entries visible", True,
          all(y not in (None, 0) for y in pre_y))

    # Call game_restart(&g_game) in place: `c` halts at the armed
    # game_render breakpoint (de == &g_game from the main loop's call),
    # so only the PC needs redirecting; the trailing `ret` lands back
    # in the main loop and subsequent frames render the new game.
    sess._cmd("c", timeout=15.0)
    sess._cmd(f"w/r pc 0x{sess.get_symbol('game_restart'):04X}")
    sess._cmd("c", timeout=15.0)
    for _ in range(8):
        sess.step(2)
    check("restart lands in a boss-less map", "TEST_FIELD",
          sess.snapshot().get("scene"))

    # The boss footprint must be hidden.  Pin entry 4 precisely: with
    # slot 3 inactive the body hides exactly that entry, so a visible
    # entry 4 here is the resurrected boss grid (entries 5+ legitimately
    # hold field statics -- see below -- so they are not pinned)...
    post_y4 = sess._memread(0xC000 + 4 * 4)
    check("restart: ex-boss OAM entry 4 hidden", 0, post_y4)
    # ...and sweep the whole table: no boss tile (108-111) may be
    # visible anywhere (stale tile bytes in hidden entries are harmless:
    # y == 0 means the PPU skips them).
    ghosts = [(i, sess._memread(0xC000 + 4 * i),
               sess._memread(0xC000 + 4 * i + 2))
              for i in range(27)
              if (sess._memread(0xC000 + 4 * i + 2) or 0) in (108, 109, 110, 111)
              and (sess._memread(0xC000 + 4 * i) or 0) != 0]
    check("restart: no visible boss sprite anywhere in OAM", [], ghosts)


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
    # Re-sync to the game_render-breakpoint phase: wait_vblank halts at
    # VBlank via raw `frame`, and stepped inputs after a `frame`-halt
    # slip (see AGENTS.md 52.26 -- `frame` does not reliably advance
    # gameplay the way step() does).  One sacrificial step() re-anchors
    # the phase so the walk below covers real frames.
    sess.step(1)
    sess.hold("RIGHT", 10)
    # Carl teaches first: close the lesson so the spar (which clobbers
    # the world tiles with enemy art) actually starts.
    for _ in range(12):
        sess.press("A")
        sess.step(20)
        if sess.snapshot().get("screen") == "BATTLE":
            break
    sess.step(2)
    check("battle VRAM read in VBlank", True, wait_vblank(sess))
    during = vram_tiles()
    check("battle clobbers world VRAM tiles (test exercises the bug)",
          True, during != before)
    # Re-sync again after the VBlank reads (same 52.26 hazard): the flee
    # inputs below must land on whole game_render-stepped frames.
    sess.step(1)

    # Flee (B with an empty hand) and leave the result screen, then let
    # the overworld redraw + tileset reload settle.  Two B presses per
    # round: if a press lands while a card is selected (e.g. an A-loop
    # over-press selected into the new battle, or a B landed outside the
    # SELECT phase), the first B only unselects -- the second B then
    # flees.  A single-B schedule toggle-traps forever in that state
    # (B unselects, A reselects, B never sees an empty combo).
    for _ in range(8):
        sess.press("B")
        sess.step(2)
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
    # Each section uses its own session so debugger state (the game_render
    # frame-sync breakpoint, watchpoints) never contaminates another
    # section's frame stepping or VBlank reads.
    for label, fn in (("battle", verify_battle_transition),
                      ("battle steady frame", verify_steady_battle_frame),
                      ("battle VRAM restore", verify_battle_vram_restore),
                      ("scene", verify_scene_transition),
                      ("restart", verify_restart_clears_boss_oam),
                      ("portal step", verify_portal_step_oam),
                      ("dialogue", verify_dialogue_transition),
                      ("hostile sprites", verify_hostile_sprites),
                      ("npc sprites", verify_npc_sprites),
                       ("battle sprites", verify_battle_oam),
                       ("battle spider", verify_battle_spider_oam),
                       ("boss glow overlay", verify_boss_glow_overlay),
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
