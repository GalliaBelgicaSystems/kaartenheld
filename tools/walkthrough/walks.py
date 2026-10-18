"""Declarative walkthrough definitions (docs/verify-walkthrough.md §3 Phase 4).

Every walk boots its own Session (fresh state) and drives the real
content with BFS-planned routes.  Expected values are read from
levels/*.json and the ROM's own const tables (via StateReader) — no
hardcoded gameplay numbers.

Milestones (screenshots) keep the committed names; semantic asserts are
additive.
"""

import io
import json
import os
import sys

from walkthrough.session import Session, LEVELS_DIR, REPO
from walkthrough.state_reader import (SCENE_TOWN, SCENE_FOREST,
                                      SCENE_MOUNTAIN_PASS, SCENE_CASTLE,
                                      SCENE_SOUTH_FIELD, SCENE_FIELD,
                                      SCENE_VILLAGE_AREA, SCENE_DESOLATE_FIELD,
                                      STORY_FLAG_ID_ARRIVED_TOWN,
                                      STORY_FLAG_ID_MET_MAYOR,
                                      VARIABLE_ID_QUEST_MONSTER_HUNT,
                                      VARIABLE_ID_MERCHANT_QUEST,
                                      VARIABLE_ID_ENERGY_POOL,
                                      CARD_WOOD_RING, CARD_POISON_DAGGER,
                                      CARD_AMULET,
                                      HERO_START_GOLD,
                                      HERO_START_HP, MUSIC_FOREST,
                                      MUSIC_DUNGEON, MUSIC_TOWN,
                                      MUSIC_CHEST)

WALK_SECONDS = 300        # per-walk wall-clock cap
MAX_BATTLE_ROUNDS = 26
from walkthrough.state_reader import (ATTACK_TYPES, BT_SHIELD, BT_EMPTY,
                                      OFF_PARTY, CHARACTER_STATE_HP,
                                      CHARACTER_STATE_MAX_HP, ENTITY_SIZE,
                                      BATTLE_HAND, CARD_SIZE, CARD_TYPE,
                                      CARD_VALUE)
# collision lives in tools/level_compiler; set the path here rather than
# borrowing route.py's namespace, so a future refactor of route.py's import
# list cannot silently break this module.
sys.path.insert(0, os.path.join(REPO, "tools", "level_compiler"))
from collision import effective_actor_flags   # noqa: E402

_BTNS = {(0, -1): "up", (0, 1): "down", (-1, 0): "left", (1, 0): "right"}


def _level(name):
    return json.load(open(os.path.join(LEVELS_DIR, name + ".json")))


# Wake-grove boot: every session starts in the grove clearing; the only
# way out is the east corridor at row 8, landing one cell inside the
# field's west edge at the crossed row (edge_banked.c mirror).
FIELD_WEST_ENTRY = (1, 8)


def _grove_spawn():
    sp = _level("wake_grove")["player"]["spawn"]
    return (sp["x"], sp["y"])


def _boot_to_field(s, planner):
    """Route a session out of the grove into the field; returns the
    field entry position."""
    return follow(s, planner, "wake_grove", s.pos(), "field",
                  FIELD_WEST_ENTRY)


# ── shared step helpers ──────────────────────────────────────────────

def god_mode(s):
    """Transit god-mode (host-side RAM poke, release ROM untouched):
    99 HP so any ambush on long layout routes is survivable. Called
    only on transit walks (walk-a/c/e/sweep), never on walk-b which
    asserts base HP/energy rewards."""
    r = s.reader
    pbase = r.state + OFF_PARTY
    s.pb.memory[pbase + CHARACTER_STATE_HP] = 99
    s.pb.memory[pbase + CHARACTER_STATE_MAX_HP] = 99
    wbase = r.world + 5
    s.pb.memory[wbase + 2] = 99
    s.pb.memory[wbase + 3] = 99


def ungod_mode(s):
    """Restore base hero HP before saving (walk-a only): god-mode 99/99
    must not persist into SRAM or walk-l would load a mutated save.
    Transit loot gold stays (walk-a asserts >= for exactly this)."""
    from walkthrough.state_reader import HERO_START_HP
    r = s.reader
    pbase = r.state + OFF_PARTY
    s.pb.memory[pbase + CHARACTER_STATE_HP] = HERO_START_HP
    s.pb.memory[pbase + CHARACTER_STATE_MAX_HP] = HERO_START_HP
    wbase = r.world + 5
    s.pb.memory[wbase + 2] = HERO_START_HP
    s.pb.memory[wbase + 3] = HERO_START_HP


def poke_hand_99(s):
    """Set every attack card in the live battle hand to 99 (one-shot).
    Transit fights only; intentional fights (walk-b Carl) never call
    this so base damage/rounds stay asserted."""
    r = s.reader
    try:
        base = r._resolve_battle_base() + BATTLE_HAND
    except Exception:
        return
    for i in range(5):
        t = r.rd(base + i * CARD_SIZE + CARD_TYPE, 1)[0]
        if t in ATTACK_TYPES:
            s.pb.memory[base + i * CARD_SIZE + CARD_VALUE] = 99


def transit_fight(s):
    """Win an unexpected transit ambush (DECK: open) with single
    99-attack cards, then return to the overworld. True on VICTORY.
    Loops for multi-enemy packs (single-target kills one per round).
    Single-card so energy 1 suffices; intentional fights (walk-b)
    never call this so base damage/rounds stay asserted."""
    for _round in range(5):
        # The DECK: row redraws over several frames after a phase change
        # (full battle redraws span multiple emulator ticks); a tick-halt
        # can land mid-redraw with TARGET already up but the deck row not
        # yet repainted.  Wait briefly for the deck row instead of
        # breaking on the first torn read, or a redraw-phase check
        # abandons a healthy fight (same class as verify_oam's
        # frame-halt hazard, AGENTS.md 52.26).
        if not s.wait_for(lambda: s.text_has("DECK:"), ticks=30):
            break
        if s.text_has("VICTORY") or s.text_has("DEFEATED"):
            break
        poke_hand_99(s)
        s.wait_for(lambda: s.text_has("TARGET ")
                   or s.text_has("PICK CARDS")
                   or s.text_has("PLAYER TURN"), ticks=180)
        poke_hand_99(s)
        # Select exactly one attack card (first attack slot). If the
        # 5-card draw holds no attack (all shields/heals), execute the
        # empty turn anyway: the round advances and a fresh hand draws
        # next round instead of stalling here forever.
        for idx in range(5):
            if not _cursor_to(s, idx):
                continue
            if s.reader.battle_hand()[idx][0] not in ATTACK_TYPES:
                continue
            for _ in range(3):
                before = s.reader.battle_combo_count()
                s.press("a", settle=12)
                if s.reader.battle_combo_count() > before:
                    break
                s.tick(2)
            break
        for _ in range(4):
            s.press("select", settle=40)
            if s.text_has("VICTORY") or s.text_has("DEFEATED"):
                break
            # Synchronize phases like _battle_round: the attack execute
            # runs an animation before DEFEND opens (~240 ticks). Firing
            # blind selects into DEFEND (or into the next round before a
            # card is picked) desyncs the whole fight.
            if s.wait_for(lambda: s.text_has("DEFEND"), ticks=240):
                break
        if s.text_has("VICTORY") or s.text_has("DEFEATED"):
            break
        # Defense phase: execute (empty or shield) to advance the round,
        # then wait for the next select banner before redrawing cards.
        for _ in range(4):
            s.press("select", settle=40)
            if s.text_has("VICTORY") or s.text_has("DEFEATED"):
                break
            if s.wait_for(lambda: s.text_has("TARGET ")
                           or s.text_has("PICK CARDS")
                           or s.text_has("PLAYER TURN"), ticks=240):
                break
    if s.text_has("VICTORY"):
        # Exit the result screen with exactly-one-press discipline: a
        # second A would land in a just-opened single-page dialogue and
        # close it before any reader sees it (the knight's 3-line
        # briefing is one page).  One press, a long settle for the
        # LCD-off transition redraw, then verify -- retry only if the
        # result screen is STILL up (eaten press), never blindly.  The
        # loop also stops for an open KNIGHT: box (never press into
        # it); route followers drain aftermath dialogues themselves.
        for _ in range(6):
            if ((not s.text_has("DECK:") and not s.text_has("VICTORY"))
                    or s.text_has("KNIGHT:")):
                break
            s.press("a", settle=30)
            s.tick(120)
        s.tick(20)
        return True
    return False


def drain_postvictory_dialogue(s, tries=8):
    """Close a dialogue left open by a transit win (today: the knight's
    armor-break briefing, which fires from its defeat event on the way
    out of VICTORY).  Only follow()/_walk_path() call this, and only
    right after transit_fight() wins -- so any dialogue open here is
    causally the fight's aftermath, never an objective dialogue (those
    open via bump_actor, never during a route step).  Bounded: never
    spins a walk that is genuinely stuck.  A future aftermath dialogue
    extends the marker tuple below."""
    for _ in range(tries):
        if not s.stable(lambda: s.text_has("KNIGHT:"), frames=10):
            return True
        s.press("a", settle=30)
    return not s.text_has("KNIGHT:")


def _walk_path(s, path):
    """Execute a BFS cell path as discrete one-tile presses."""
    for dx, dy in path:
        btn = _BTNS[(dx, dy)]
        fx, fy = s.pos()
        ok = s.walk_btn(btn, lambda: s.pos() == (fx + dx, fy + dy))
        if not ok and s.text_has("DECK:"):
            win = transit_fight(s)
            s.check("transit fight victory", win,
                    expected="VICTORY", actual="defeat" if not win else "win")
            if win:
                s.check("transit aftermath drained",
                        drain_postvictory_dialogue(s),
                        expected="no KNIGHT: box", actual="stuck open")
                ok = s.walk_btn(btn, lambda: s.pos() == (fx + dx, fy + dy))
        banner = (s.bg_text()[0].strip() if s.text_has("DECK:") else "")
        s.check("route move %s" % btn, ok,
                expected="tile commit",
                actual="pos %s scene %d deck=%s banner=%r" % (
                    s.pos(), s.reader.scene_state()["scene_id"],
                    s.text_has("DECK:"), banner))


def follow(s, planner, from_scene, start, to_scene, goal,
           capture_when=None, capture_label=None):
    """Execute a cross-scene BFS route; optional mid-route screenshot."""
    steps = planner.route(from_scene, start, to_scene, goal)
    for step in steps:
        if step[0] == "move":
            _walk_path(s, [step[1:]])
        else:
            _, direction, e = step
            # Ambush on the exit step overrides the transition
            # (MOVE_OUTCOME_ENCOUNTER wins over EXIT/EDGE): fight, retry
            # the crossing, and repeat while still ambushed (bounded, so
            # back-to-back patrols can't spin forever). The check logs
            # only the final outcome, never an intermediate ambush.
            res = False
            for _ in range(3):
                res = s.cross_exit(
                    direction, planner.scenes[e["target_scene"]].scene_id)
                if res or not s.text_has("DECK:"):
                    break
                win = transit_fight(s)
                s.check("transit fight victory", win,
                        expected="VICTORY", actual="defeat" if not win else "win")
                if not win:
                    break
                s.check("transit aftermath drained",
                        drain_postvictory_dialogue(s),
                        expected="no KNIGHT: box", actual="stuck open")
            s.check("exit %s -> %s" % (direction, e["target_scene"]), res,
                    expected=e["target_scene"],
                    actual="scene %d" % s.reader.scene_state()["scene_id"])
        if capture_when and capture_label and capture_when():
            s.shoot(capture_label)
            capture_when, capture_label = None, None
    return s.pos()


def bump_actor(s, planner, scene_name, adjacent, direction, marker,
               tries=8):
    """Walk to `adjacent`, press `direction` into the actor until the
    dialogue marker appears (retries: dropped presses, transition eats)."""
    if s.pos() != adjacent:
        path = planner.path(scene_name, s.pos(), adjacent)
        if path is None:
            s.check("path to %s" % (adjacent,), False,
                    expected="bfs path", actual="none")
            return False
        _walk_path(s, path)
    return s.press_until(direction, lambda: s.text_has(marker),
                         tries=tries, settle=30, label=marker)


def close_dialogue(s, marker, frames=45):
    return s.press_until("a",
                         lambda: s.stable(
                             lambda: not s.text_has(marker), frames=frames),
                         tries=12, settle=30, timeout=120,
                         label="close " + marker)


def _cursor_to(s, idx):
    """Move the battle hand cursor to slot idx (WRAM-read, bounded)."""
    for _ in range(8):
        if s.reader.battle_cursor() == idx:
            return True
        s.press("right", settle=10)
    return s.reader.battle_cursor() == idx


def _select_cards(s, wanted_types):
    """Toggle-select every hand card whose type is in wanted_types.
    Each A press is verified against combo_count (an A can be eaten by
    input edge timing, AGENTS.md §52.10 — retry with a reset frame)."""
    picked = 0
    for idx in range(5):
        if not _cursor_to(s, idx):
            continue
        ctype = s.reader.battle_hand()[idx][0]
        if ctype not in wanted_types or ctype == BT_EMPTY:
            continue
        for _attempt in range(3):
            before = s.reader.battle_combo_count()
            s.press("a", settle=12)
            after = s.reader.battle_combo_count()
            if after > before:
                picked += 1
                break
            if after < before:
                # was already selected; A toggled it off — re-select
                continue
            s.tick(2)    # reset frame between same-button edges (52.10)
    return picked


def _battle_round(s):
    """One full round: select attack cards + execute, then select shield
    cards + execute.  True when the battle reached a result banner.
    Phase waits are input-free (the game advances automatically after
    each execute).  Both executes are retried like the opening round
    below: a press eaten by edge timing leaves the phase open instead
    of advancing, and over a long fight (Carl's 10 HP still needs
    10+ single-card rounds at the flat-1 opening pool) a single desync
    cascades into rounds
    of phase-mismatched presses dealing nothing."""
    # Select phase: TARGET banner (Carl: PICK CARDS headline instead;
    # fallback literal PLAYER TURN).
    s.wait_for(lambda: s.text_has("TARGET ")
               or s.text_has("PICK CARDS")
               or s.text_has("PLAYER TURN"), ticks=180)
    _select_cards(s, ATTACK_TYPES)
    for _ in range(4):
        s.press("select", settle=40)     # execute attack
        if s.text_has("VICTORY") or s.text_has("DEFEATED"):
            return True
        if s.wait_for(lambda: s.text_has("DEFEND"), ticks=240):
            break
    if s.text_has("VICTORY") or s.text_has("DEFEATED"):
        return True
    # Defense phase: <name>: DEFEND! banner.
    _select_cards(s, (BT_SHIELD,))
    for _ in range(4):
        s.press("select", settle=40)     # execute defense
        if s.text_has("VICTORY") or s.text_has("DEFEATED"):
            return True
        if s.wait_for(lambda: s.text_has("TARGET ")
                      or s.text_has("PICK CARDS")
                      or s.text_has("PLAYER TURN"), ticks=240):
            break
    return s.text_has("VICTORY") or s.text_has("DEFEATED")


def engage_hostile(s, planner, scene_name, hostile_xy):
    """Reach a patrol-box edge, then sweep until the battle engages
    (DECK: on the BG tilemap)."""
    edge = planner.edge_of(scene_name, s.pos(), hostile_xy)
    if edge is not None:
        path = planner.path(scene_name, s.pos(), edge)
        if path:
            _walk_path(s, path)
    if s.text_has("DECK:"):
        return True
    hx = hostile_xy[0]
    for _ in range(60):
        if s.text_has("DECK:"):
            return True
        s.press("left" if s.pos()[0] > hx else "right", settle=24)
    return s.text_has("DECK:")


# ── Walk A: town — dialogue, mayor, shop purchase, quick screen, save ──

def walk_a(planner, checks, sram_out=None):
    """Town walkthrough.  With sram_out (a list), the cartridge RAM is
    captured on exit for the load-roundtrip walk (walk_l)."""
    s = Session(checks, "walk-a")
    god_mode(s)
    s.shoot("00-boot-grove")

    # Staircase refusal: the stairs stand north of the wake clearing;
    # bumping them opens the refusal line (dialogue, no map change).
    # Three-line pages show both refusal lines at once plus a blank third
    # row, so the bump keys on line 1 ("not going") and line 2
    # ("back down there") is already visible: the page-2 press returns
    # instantly and the close shuts the single-page box.  Kept two-phase
    # so a future multi-page refusal still walks correctly.
    s.check("stairs refusal",
            bump_actor(s, planner, "wake_grove", (10, 7), "up",
                       "not going"),
            expected="not going", actual="none")
    s.shoot("00-stairs-refusal", need="not going")
    s.check("refusal page 2",
            s.press_until("a", lambda: s.text_has("back down there"),
                          tries=6, label="page 2"),
            expected="back down there", actual="none")
    s.check("refusal closed", close_dialogue(s, "back down there"),
            expected="closed", actual="open")

    # Route into town via the grove corridor; capture the
    # camera-scrolled field shot on the way.
    # Town entry is the field-east mirrored spawn (1,7): one inside the
    # west edge at the crossed row.
    field_pos = _boot_to_field(s, planner)
    follow(s, planner, "field", field_pos, "town", (1, 7),
           capture_when=lambda: s.pos()[0] >= 22,
           capture_label="01-field-scrolled")
    s.settle_scene(expected_scene=SCENE_TOWN)
    s.shoot("02-town-arrived")
    st = s.reader.scene_state()
    s.check_eq("town arrival scene", st["scene_id"], SCENE_TOWN)
    s.check_eq("ARRIVED_TOWN flag",
               s.reader.flag(STORY_FLAG_ID_ARRIVED_TOWN), True)
    s.check_eq("scene pos synced", (st["player_x"], st["player_y"]),
               (1, 7))

    # Guard dialogue (guard at (10,8); bump from (9,8) rightwards).
    # Three-line pages show both guard lines at once plus a blank third
    # row (single page, no continue chevron on the last page), so one
    # close shuts the box.
    s.check("guard dialogue opened",
            bump_actor(s, planner, "town", (9, 8), "right", "GUARD:"),
            expected="GUARD:", actual="none")
    s.shoot("03-guard-dialogue", need="GUARD:")
    # The box is background tiles at rows 14-17; no actor OAM entry (the
    # town fire/dog use enemy-kind OAM art) may draw over it.  Box top
    # row 14 -> stored OAM y 128.  Regression for the fire/dog overlay.
    s.tick(4)
    _over = [y for y in s.oam_actor_ys() if y >= 128]
    s.check("guard dialogue: no actor sprite over the box", not _over,
            expected="actor OAM y < 128 (box top row 14)",
            actual=str(_over))
    close_dialogue(s, "GUARD:")

    # Mayor: interact event sets MET_MAYOR + starts the quest.
    # The 3-line INTRO fits on one page (all three lines, no chevron);
    # milestone 04 captures that page (the next dialogue after the guard).
    s.check("mayor dialogue opened",
            bump_actor(s, planner, "town", (9, 5), "right", "MAYOR:",
                       tries=6),
            expected="MAYOR:", actual="none")
    s.shoot("04-dialogue-next", need="MAYOR:")
    close_dialogue(s, "MAYOR:")
    s.check_eq("MET_MAYOR flag", s.reader.flag(STORY_FLAG_ID_MET_MAYOR),
               True)
    s.check_eq("quest monster hunt active",
               s.reader.variable(VARIABLE_ID_QUEST_MONSTER_HUNT), 1)

    # Heal Mage: red robe at (12,10), wizard sprite.  Hurt the hero via
    # host poke (same pattern as god_mode: release ROM untouched), talk,
    # confirm YES on the ask (default cursor), verify full restore +
    # chest jingle + town-track restore.  Exercises the NPC on the
    # release ROM (the harness covers the debug ROM in heal_mage.json).
    r = s.reader
    pbase = r.state + OFF_PARTY
    s.pb.memory[pbase + CHARACTER_STATE_HP] = 5
    s.check("heal mage dialogue opened",
            bump_actor(s, planner, "town", (12, 9), "down", "HEAL MAGE:"),
            expected="HEAL MAGE:", actual="none")
    s.check("heal mage YES confirmed",
            s.press_until("a",
                          lambda: s.reader.music_track() == MUSIC_CHEST,
                          tries=6, settle=30, label="mage YES"),
            expected="MUSIC_CHEST", actual="confirm")
    s.check_eq("chest jingle playing",
               s.reader.music_track(), MUSIC_CHEST)
    close_dialogue(s, "HEAL MAGE:")
    _hero = s.reader.hero()
    s.check_eq("heal mage restored HP", _hero["hp"], _hero["max_hp"])
    s.check("town music restored",
            s.wait_for(lambda: s.reader.music_track() == MUSIC_TOWN,
                       ticks=400),
            expected="MUSIC_TOWN", actual="restore")

    # Lost Merchant quest: the briefing tracks it (MERCHANT_QUEST=1),
    # then the amulet is fetched from the mountain pass.  Delivering it
    # completes the quest for +15g and opens the merchant's merged shop
    # (ring included).  The merchant
    # lives in the village area (east of town); the INTRO is one page,
    # like the mayor's.
    follow(s, planner, "town", s.pos(), "village_area", (10, 4))
    s.settle_scene(expected_scene=SCENE_VILLAGE_AREA)
    s.check("merchant briefing opened",
            bump_actor(s, planner, "village_area", (10, 4), "down", "MERCHANT:"),
            expected="MERCHANT:", actual="none")
    close_dialogue(s, "MERCHANT:")
    s.check_eq("amulet quest active",
               s.reader.variable(VARIABLE_ID_MERCHANT_QUEST), 1)

    # Mountain detour for the amulet (position from the level JSON; the
    # planner routes around patrol boxes and blocking actors, goal
    # exempt).  The amulet chest is blocking: bump from the west.
    mountain = _level("mountain_pass")
    amulet = next(o for o in mountain["objects"]
                  if (o.get("properties") or {}).get("entity_id")
                  == "ENTITY_ID_AMULET")
    ax, ay = amulet["position"]["x"], amulet["position"]["y"]
    follow(s, planner, "village_area", s.pos(), "mountain_pass", (ax - 1, ay))
    s.settle_scene(expected_scene=SCENE_MOUNTAIN_PASS)
    s.check("amulet found",
            bump_actor(s, planner, "mountain_pass", (ax - 1, ay), "right",
                       "FOUND AMULET"),
            expected="FOUND AMULET", actual="none")
    close_dialogue(s, "FOUND AMULET")
    s.check_eq("amulet in collection",
               s.reader.collection_count(CARD_AMULET), 1)

    # Back to the village: the talk holding the amulet delivers it
    # (thanks + 15g, amulet removed, quest COMPLETE), which opens the
    # shop on the talk after.
    follow(s, planner, "mountain_pass", s.pos(), "village_area", (10, 4))
    s.settle_scene(expected_scene=SCENE_VILLAGE_AREA)
    s.check("deliver dialogue opened",
            bump_actor(s, planner, "village_area", (10, 4), "down",
                       "You found it!"),
            expected="You found it!", actual="none")
    close_dialogue(s, "You found it!")
    s.check_eq("amulet quest complete",
               s.reader.variable(VARIABLE_ID_MERCHANT_QUEST), 2)
    # Transit fights (god-mode) add loot gold, so assert at least the
    # delivery reward on top of the starter pool, never an exact total.
    s.check("delivery gold", s.reader.gold() >= HERO_START_GOLD + 15,
            expected=">= %d" % (HERO_START_GOLD + 15),
            actual=str(s.reader.gold()))

    # Merged shop (village merchant, opened by delivering the amulet):
    # one-copy ring + bow stock.  The ring
    # is the first row; buying hides it and the bow slides up.
    s.check("shop opened",
            bump_actor(s, planner, "village_area", (10, 4), "down", "SHOP"),
            expected="SHOP", actual="none")
    s.shoot("05-shop", need="SHOP")
    s.check("ring row caret", s.bg_text()[5][:1] == ">",
            expected="> at row 5", actual=s.bg_text()[5])
    gold0 = s.reader.gold()
    price = s.reader.card_price(CARD_WOOD_RING)
    s.check("gold before purchase", gold0 >= HERO_START_GOLD + 15,
            expected=">= %d" % (HERO_START_GOLD + 15), actual=str(gold0))
    s.press("a", settle=30)
    s.check_eq("gold after purchase", s.reader.gold(), gold0 - price)
    s.check_eq("ring in collection", s.reader.collection_count(
        CARD_WOOD_RING), 1)
    s.check("bow row slides up", "BO9" in s.bg_text()[5],
            expected="BO9 at row 5", actual=s.bg_text()[5])
    s.press_until("b", lambda: s.stable(
        lambda: not s.text_has("SHOP"), frames=30), label="shop close")
    s.check("shop closed", not s.text_has("SHOP"), expected="closed",
            actual=s.bg_text()[0].strip())

    # Quick screen: CARDS tab, toggle the first card's deck membership,
    # open the filter picker from the top row, then the QUEST tab.
    # NOTE: the starter deck holds every owned copy, so A on a fully
    # decked card CLEARS it (docs/deck-management.md) — the assert is
    # "membership toggled", never "+1".
    s.press_until("start", lambda: s.text_has("CARDS QUEST"),
                  label="quick screen")
    s.shoot("06-cards-menu", need="CARDS QUEST")
    deck0 = s.reader.deck_count()
    s.press("down", settle=20)      # FILTER/SORT row -> first card row
    s.press("a", settle=30)         # toggle deck membership
    deck1 = s.reader.deck_count()
    s.check("deck membership toggled", deck1 != deck0,
            expected="deck != %d" % deck0, actual=str(deck1))
    s.press("up", settle=20)        # back to the FILTER/SORT row
    s.press_until("a", lambda: s.text_has("LR CYCLE"),
                  label="filter picker")
    s.shoot("07-filter-picker", need="LR CYCLE")
    s.press_until("b", lambda: not s.text_has("LR CYCLE"),
                  label="picker close")
    s.check("picker closed", not s.text_has("LR CYCLE"),
            expected="closed", actual="open")
    tabbed = False
    for _ in range(8):
        if s.bg_text()[3][6] == "^":
            tabbed = True
            break
        s.press("right", settle=30)
    s.check("quest tab caret", tabbed, expected="^ at col 6",
            actual=s.bg_text()[3])
    s.shoot("08-quests-tab")
    s.press_until("b", lambda: s.stable(
        lambda: not s.text_has("CARDS QUEST"), frames=30),
        label="quick close")
    s.check("quick screen closed", not s.text_has("CARDS QUEST"),
            expected="closed", actual="open")

    # Wizard save (slot 1).  Capture only on a stable menu/message —
    # removes the known-unstable 12-wizard-save frame (§56.2).
    # The shop/quick screen ran in the village; return to town for the
    # wizard (he never moved). Drop god-mode first so SRAM keeps base
    # 10/10 HP (town/village legs are ambush-free).
    ungod_mode(s)
    follow(s, planner, "village_area", s.pos(), "town", (6, 11))
    s.settle_scene(expected_scene=SCENE_TOWN)
    _walk_path(s, planner.path("town", s.pos(), (6, 11)) or [])
    s.check("wizard save menu", s.press_until(
        "up", lambda: s.text_has("SAVE GAME"), label="save menu"),
        expected="SAVE GAME", actual="none")
    s.tick(20)
    s.shoot("12-wizard-save", need="SAVE GAME")
    s.press("a", settle=30)
    s.check("SAVED message", s.wait_for(lambda: s.text_has("SAVED"),
                                        ticks=120),
            expected="SAVED", actual="none")
    # Let the transient TTL clear so the capture is byte-stable.
    s.press_until("a", lambda: s.stable(
        lambda: not s.text_has("SAVED"), frames=45), tries=2,
        settle=20, timeout=90, label="message ttl")
    s.shoot("13-wizard-saved")

    if sram_out is not None:
        sram_out.append((s.stop_save_ram(), dict(
            scene=SCENE_TOWN, pos=(6, 11),
            gold=s.reader.gold(), deck=s.reader.deck_count(),
            ring=s.reader.collection_count(CARD_WOOD_RING))))
    else:
        s.close()


# ── Walk B: Carl practice spar -> victory + AP unlock ─────────────────

def walk_b(planner, checks):
    s = Session(checks, "walk-b")
    _boot_to_field(s, planner)
    field = _level("field")
    carl = next((o for o in field["objects"] if o.get("type") == "enemy"),
                None)
    carl_xy = (carl["position"]["x"], carl["position"]["y"])
    props = carl["properties"]
    # Field cache east of Carl first: Carl withholds the spar until it
    # is opened ("Open chest, then come back!"), so the hero opens it
    # (first poison dagger plus the AP unlock: pool 1 -> 2) and only
    # then walks to the lesson.
    s.check("chest bump",
            bump_actor(s, planner, "field", (20, 8), "right", "CHEST:"),
            expected="CHEST:", actual="none")
    s.shoot("11-chest", need="CHEST:")
    s.check("cache closed",
            close_dialogue(s, "CHEST:"),
            expected="closed", actual="open")
    s.check_eq("first poison dagger",
               s.reader.collection_count(CARD_POISON_DAGGER), 1)
    s.check_eq("energy pool 1 -> 2",
               s.reader.variable(VARIABLE_ID_ENERGY_POOL), 2)
    # Carl spars on bump (he is stationary, so walk adjacent and step
    # in; the planner routes around his tile).  Bumps are retried: screen
    # transitions eat input for a few dozen frames.  Carl is a single
    # sprite now: bumping him opens the lesson first, and closing it
    # auto-starts the spar from the stashed encounter (no second bump).
    # Carl waits on his hut doorstep (15,12); the hero bumps from (14,12).
    s.check("carl lesson bump",
            bump_actor(s, planner, "field", (14, 12), "right", "CARL:"),
            expected="CARL:", actual="none")
    # Carl himself, one tile east of the hero: the field's single slime.
    s.shoot("08-carl-field")
    s.shoot("08-carl-tutor", need="CARL:")
    # Real-VRAM exact-row regression (Oct 2026): a tree trunk (world
    # glyph 't', tile 157) underlaps the 't' of "fight" here, and the
    # redraw skip once left trunk art on screen while RAM claimed text
    # (harness text asserts are blind to it).  Row 16 must decode the
    # font 't', not '?' (non-font tile); '?' at col 19 of row 17 is the
    # expected chevron tile.
    rows = s.bg_text()
    s.check("carl tutor rows exact",
            rows[14] == "+--CARL:-----------+"
            and rows[15] == "Human? Here danger  "
            and rows[16] == "I teach you fight!  "
            and rows[17] == "U/D choose target  ?",
            expected="exact box text",
            actual=" / ".join(rows[14:18]))
    # Closing by battle-open (not by CARL: gone): the spar banner itself
    # reads "CARL: PICK CARDS", so a marker-gone wait would spam A's into
    # the fight.  Three advances close the 8-line lesson; press_until
    # self-heals dropped presses and stops at the first DECK: sighting.
    s.check("lesson closed into spar",
            s.press_until("a", lambda: s.text_has("DECK:"),
                          tries=8, settle=30, label="DECK:"),
            expected="DECK:", actual="none")
    s.tick(40)
    s.shoot("09-battle", need="DECK:")
    s.check_eq("hero hp at battle start", s.reader.battle_player_hp(),
               HERO_START_HP)
    s.check_eq("carl hp at battle start", s.reader.battle_enemy_hp(0),
               props["hp"])
    s.check_eq("spar starts at 2 AP (chest opened first)",
               s.reader.variable(VARIABLE_ID_ENERGY_POOL), 2)

    # Fight with hand-aware selection until the solo spar drops
    # (variance-safe: enemy HP strictly decreases per landed hit; never
    # assert exact damage).  Bounded rounds + defeat detection.  Carl's
    # practice deck deals 0, so the hero must still be full afterwards.
    # His stepwise sparring hints show per phase: the card-pick lesson
    # while choosing the attack, the shield lesson while defending.
    prev_total = s.reader.battle_enemy_hp(0)
    start_total = prev_total
    victory = False
    defeated = False
    # Select-stage banner and hint are up while choosing the attack.
    s.check("select hint", s.text_has("PICK CARDS"),
            expected="PICK CARDS", actual="none")
    _select_cards(s, ATTACK_TYPES)
    # The execute press is retried: a press eaten by edge timing leaves
    # the SELECT phase open instead of advancing to DEFEND.
    for _ in range(4):
        s.press("select", settle=40)     # execute attack
        if s.text_has("VICTORY") or s.text_has("DEFEATED"):
            break
        if s.wait_for(lambda: s.text_has("DEFEND"), ticks=240):
            break
    s.check("defend banner", s.text_has("DEFEND"),
            expected="DEFEND", actual="none")
    # The hint shares row 9 with transient messages (a selection
    # rejection's 45-frame TTL freezes through attack animations, so it
    # can still own the row when DEFEND opens): wait for the lesson.
    s.check("defend hint",
            s.wait_for(lambda: s.text_has("SHIELD"), ticks=150),
            expected="SHIELD", actual="none")
    s.shoot("10-battle-defend", need="DEFEND")
    _select_cards(s, (BT_SHIELD,))
    s.press("select", settle=40)     # execute defense
    s.shoot("10-battle-attack")
    prev_total = s.reader.battle_enemy_hp(0)
    for round_no in range(1, MAX_BATTLE_ROUNDS):
        done = _battle_round(s)
        if done:
            victory = s.text_has("VICTORY")
            defeated = s.text_has("DEFEATED")
            break
        if s.reader.battle_player_hp() == 0:
            defeated = True
            break
        total = s.reader.battle_enemy_hp(0)
        if total < prev_total:
            s.check("damage dealt round %d" % round_no, True)
            prev_total = total
    s.check("battle victory", victory, expected="VICTORY",
            actual="enemy hp %d/%d after %d rounds%s"
            % (prev_total, start_total, MAX_BATTLE_ROUNDS,
               " (hero defeated)" if defeated else ""))
    s.shoot("11-battle-victory", need="VICTORY")

    # Leave the result screen; Carl's good-luck speech shows deferred
    # before the overworld return.  The loot hook credits gold on battle
    # end, so assert gold AFTER the exit.  The exit press is retried: screen
    # transitions eat input for a few dozen frames.
    s.check("victory speech",
            s.press_until("a", lambda: s.text_has("CARL:"), tries=6),
            expected="CARL:", actual="none")
    s.shoot("11-victory-speech", need="CARL:")
    s.check("speech closed",
            close_dialogue(s, "CARL:"),
            expected="closed", actual="open")
    s.check("aftermath overworld",
            s.wait_for(lambda: s.reader.scene_state()["scene_id"]
                       == SCENE_FIELD, ticks=180),
            expected="SCENE_FIELD",
            actual="scene %d" % s.reader.scene_state()["scene_id"])
    s.check_eq("victory gold", s.reader.gold(),
               HERO_START_GOLD + props.get("gold_reward", 0))
    s.check_eq("hero hp untouched (0-damage spar)", s.reader.hero()["hp"],
               HERO_START_HP)
    s.check_eq("energy pool 2 -> 3",
               s.reader.variable(VARIABLE_ID_ENERGY_POOL), 3)
    s.walk_btn("up", lambda: s.pos()[1] == 7)
    s.walk_btn("down", lambda: s.pos()[1] == 8)
    s.tick(30)
    s.shoot("11-battle-aftermath")
    s.close()


# ── Walk C: forest gate ──────────────────────────────────────────────

def walk_c(planner, checks):
    s = Session(checks, "walk-c")
    god_mode(s)
    field = _level("field")
    spawn = _boot_to_field(s, planner)
    # Forest arrival = the field->forest stairs tunnel (content-coupled).
    assert any(e.get("target_scene") == "forest"
               for e in (field.get("exits", []) or [])), \
        "walk_c expects a field->forest tunnel exit"
    forest = _level("forest")
    goal = (forest["player"]["spawn"]["x"], forest["player"]["spawn"]["y"])
    follow(s, planner, "field", spawn, "forest", goal)
    s.settle_scene(expected_scene=SCENE_FOREST)
    st = s.reader.scene_state()
    s.check_eq("forest scene", st["scene_id"], SCENE_FOREST)
    s.check_eq("forest music", s.reader.music_track(), MUSIC_FOREST)
    s.shoot("14-forest-arrived")
    s.close()


# ── Walk E: castle mimic (solo boss screen) ──────────────────────────

def walk_e(planner, checks):
    s = Session(checks, "walk-e")
    god_mode(s)
    field = _level("field")
    spawn = _boot_to_field(s, planner)
    castle = _level("castle")
    mimic = next((o for o in castle["objects"]
                  if (o.get("properties") or {}).get("battle")
                  == "BATTLE_MIMIC"), None)
    mimic_xy = (mimic["position"]["x"], mimic["position"]["y"])
    # One multi-hop BFS route: field -> forest -> grassy -> desolate ->
    # south_ridge -> south_field -> castle_entry -> castle_hall -> castle;
    # per-exit scene asserts run inside follow().
    follow(s, planner, "field", spawn, "castle",
           (mimic_xy[0] - 1, mimic_xy[1]))
    s.check_eq("castle scene", s.reader.scene_state()["scene_id"],
               SCENE_CASTLE)
    s.check_eq("castle music", s.reader.music_track(), MUSIC_DUNGEON)
    # Slime-knight interlude (real-content regression for the armor
    # split): the knight chases anywhere in the castle, so it may have
    # ambushed en route already (transit win + drained briefing).  Either
    # way, engaging at its tile now must open a battle, and the banner
    # tells which encounter it is (armor or trio).
    knight = next((o for o in castle["objects"]
                   if (o.get("properties") or {}).get("entity_id")
                   == "ENTITY_ID_SLIME_KNIGHT"), None)
    knight_xy = (knight["position"]["x"], knight["position"]["y"])
    s.check("knight engaged",
            engage_hostile(s, planner, "castle", knight_xy),
            expected="DECK:", actual="none")
    # Settle for the select banner, then branch on WHO engaged: armor
    # (knight intact) or slimes (trio already spawned by an en-route
    # ambush).  Never fight blind: an unrecognized banner fails loudly.
    s.check("banner readable",
            s.wait_for(lambda: s.text_has("TARGET "), ticks=120),
            expected="TARGET banner", actual=s.bg_text()[0].strip())
    if s.text_has("TARGET SLIME KNIGHT"):
        # Armor intact: one-shot it, read the briefing, close it.
        # No blind A presses here: the victory exit inside transit_fight
        # already opened the box, and an extra edge would close the
        # single-page dialogue before it renders.  Settle first, then
        # read once.
        s.check("knight armor fight",
                transit_fight(s),
                expected="VICTORY", actual="defeat")
        s.tick(90)
        s.check("armor reveal opened",
                s.text_has("KNIGHT:"),
                expected="KNIGHT:", actual=s.bg_text()[0].strip())
        s.check("reveal closed",
                close_dialogue(s, "KNIGHT:"),
                expected="closed", actual="open")
        # The tile now holds the trio: engage it, expect slimes.
        # The trio chases (AI_CHASE, like kobolds), so the hostile
        # sweep finds it wherever the knight fell.
        s.check("trio tile engaged",
                engage_hostile(s, planner, "castle", knight_xy),
                expected="DECK:", actual="none")
        s.check("trio banner readable",
                s.wait_for(lambda: s.text_has("TARGET "), ticks=120),
                expected="TARGET banner", actual=s.bg_text()[0].strip())
        s.check("trio banner readable",
                s.wait_for(lambda: s.text_has("TARGET "), ticks=120),
                expected="TARGET banner", actual=s.bg_text()[0].strip())
    s.check("trio engaged",
            s.text_has("TARGET SLIME"),
            expected="TARGET SLIME", actual=s.bg_text()[0].strip())
    s.check("trio fight",
            transit_fight(s),
            expected="VICTORY", actual="defeat")
    s.check("no second briefing",
            s.press_until("a", lambda: not s.text_has("DECK:"),
                          tries=4, label="battle exit"),
            expected="closed", actual="open")
    s.check("no briefing repeat", not s.text_has("KNIGHT:"),
            expected="no KNIGHT: box", actual=s.bg_text()[0].strip())
    # The mimic never moves (AI_NONE) and nothing else patrols its
    # tile: walk beside it and bump it directly.  Deterministic on
    # purpose -- the sweep helper is for chasers, and a sweep here
    # could catch the bat instead and then fight the walk's objective.
    _walk_path(s, planner.path("castle", s.pos(), (mimic_xy[0] - 1, mimic_xy[1])) or [])
    s.check("mimic engaged",
            bump_actor(s, planner, "castle", (mimic_xy[0] - 1, mimic_xy[1]),
                       "right", "DECK:"),
            expected="DECK:", actual="none")
    s.check("mimic banner", s.text_has("TARGET MIMIC"),
            expected="MIMIC", actual=s.bg_text()[0].strip())
    s.tick(40)
    s.shoot("23-mimic-battle", need="MIMIC")
    s.close()


# ── Walk S: content sweep — every level in levels/ is visited ────────

def _hostile_floor(level):
    """How many UNGATED hostile actors a level declares (0 = hub/empty).
    Gated spawns (quest_var) are skipped: they may be legitimately absent.
    Flags default by object type (collision.effective_actor_flags), so a
    flagless enemy counts like the ROM's default HOSTILE row."""
    n = 0
    for o in level.get("objects", []):
        p = o.get("properties") or {}
        if not p.get("entity_id"):
            continue  # decoration: compile.py emits no actor row
        if "HOSTILE" not in effective_actor_flags(o.get("type"), p):
            continue
        if p.get("quest_var"):
            continue
        n += 1
    return n


_TILESET_KIND = {"forest": 2, "village": 12, "desolate_landscape": 14,
                 "desolate": 14, "castle": 15}


def _tileset_kind(level):
    return _TILESET_KIND.get(level["map"].get("tileset"), 2)


def _check_hostiles(s, name, level):
    """Sweep hostile-spawn assert with an explicit mirror-mismatch path.

    world_hostile_count returns -1 when NEITHER const-char* stride
    candidate makes the World tail byte match the expected tileset kind.
    Boot anchors passing first proves g_game/.sym and the
    GameState/World-head layout are fine, so a uniform -1 is NOT a
    ROM/.sym mismatch — the suspects are: levels/ edited after the ROM
    was built (the expected kind comes from the working-tree level
    JSON, including the silent default for unknown tileset names), or
    World-tail struct drift (reader mirror vs src/world/world.h).
    Report observed-vs-expected on the spot (see
    docs/verify-walkthrough.md §4.2)."""
    kind = _tileset_kind(level)
    got = s.reader.world_hostile_count(kind)
    if got == -1:
        seen = s.reader.world_tail_candidates()
        s.check("sweep %s hostiles" % name, False,
                expected="World tail tileset byte %d" % kind,
                actual="tail bytes %s; suspects: levels/ edited after "
                       "the ROM was built (git diff HEAD -- levels/), "
                       "World-tail struct drift (mirror vs world.h), "
                       "or unknown tileset name (default kind 2)" % seen)
        return
    s.check("sweep %s hostiles" % name, got >= 1,
            expected=">=1 of %d spawned" % _hostile_floor(level),
            actual=str(got))


def walk_sweep(planner, checks):
    """Sweep every level the editor can produce: for each level reachable
    from the field spawn, boot a fresh session, BFS-route there, assert
    the ROM booted the right scene with the right music, and capture
    sweep-<name>.png.  A NEW level added to levels/ is automatically
    swept on the next run; an UNREACHABLE level fails loudly (you can't
    walk to it — likely a content bug, e.g. no portal points at it).
    One session per level: every walk starts coherent at the spawn, so
    a failure isolates to exactly one level."""
    grove_spawn = _grove_spawn()
    for name in sorted(planner.scenes):
        scene = planner.scenes[name]
        want_music = _music_enum(scene.level["map"].get("music"))
        if want_music is None:
            checks.append(("sweep", "sweep %s music" % name, False,
                           "MUSIC_* mirror in state_reader.py",
                           str(scene.level["map"].get("music"))))
            want_music = -1
        s = Session(checks, "sweep")
        god_mode(s)
        if name == "wake_grove":
            st = s.reader.scene_state()
            s.check_eq("sweep %s scene" % name, st["scene_id"],
                       scene.scene_id)
            s.check_eq("sweep %s music" % name,
                       s.reader.music_track(), want_music)
            if _hostile_floor(scene.level):
                _check_hostiles(s, name, scene.level)
            s.shoot("sweep-%s" % name)
            s.close()
            continue
        goal = planner.arrival_pos(name)
        if goal is None:
            s.check("sweep %s reachable" % name, False,
                    expected="some portal (exit/edge) targets it",
                    actual="unreachable (no portal -> %s)" % name)
            s.close()
            continue
        follow(s, planner, "wake_grove", grove_spawn, name, goal)
        s.settle_scene(expected_scene=scene.scene_id)
        st = s.reader.scene_state()
        s.check_eq("sweep %s scene" % name, st["scene_id"], scene.scene_id)
        s.check_eq("sweep %s music" % name, s.reader.music_track(),
                   want_music)
        if _hostile_floor(scene.level):
            _check_hostiles(s, name, scene.level)
        s.shoot("sweep-%s" % name)
        s.close()


def _music_enum(name):
    """Map a level JSON music name (MUSIC_FOREST) to the mirrored enum;
    an unknown name becomes a check failure, never a crash."""
    from walkthrough import state_reader as sr
    value = getattr(sr, name, None) if name else None
    return value


# ── Walk D: title menu + tutorial slides (no planner) ────────────────

def walk_d(checks):
    s = Session(checks, "walk-d", boot=False)
    s.check("boot rendered", s.wait_for(s.screen_rendered, ticks=360),
            expected="render", actual="blank")
    s.check("title menu", s.press_until(
        "start", lambda: s.text_has("NEW GAME"), label="title menu"),
        expected="NEW GAME", actual="none")
    s.check("tutorial entry", s.press_until(
        "down", lambda: s.text_has("> TUTORIAL"), label="caret"),
        expected="> TUTORIAL", actual="none")
    s.shoot("15-title-menu", need="> TUTORIAL")
    s.check("tutorial entered", s.press_until(
        "a", lambda: s.text_has("TUTORIAL BASICS"), label="slide 0"),
        expected="TUTORIAL BASICS", actual="none")
    s.shoot("16-tutorial-slide0", need="TUTORIAL BASICS")
    for label, marker in (
            ("17-tutorial-slide1", "SW: SWORD"),
            ("18-tutorial-slide2", "CARD TYPES 2"),
            ("19-tutorial-slide3", "COMBOS"),
            ("20-tutorial-slide4", "1/turn"),
            ("21-tutorial-slide5", "DEFEND & STATUS"),
            ("22-tutorial-slide6", "SHIELD CARD")):
        s.check(label, s.press_until(
            "right", lambda m=marker: s.text_has(m), label=label),
            expected=marker, actual="none")
        s.shoot(label, need=marker)
    s.close()


# ── Walk L: load roundtrip from persisted cartridge RAM ──────────────

def walk_l(checks, saved):
    """Load-roundtrip walk.  `saved` = (sram_bytes, expected) captured by
    walk_a; the gold/ring/deck asserts prove the loaded state is the
    saved one, not a fresh boot (a fresh boot has gold 20, no ring)."""
    sram_bytes, exp = saved
    s = Session(checks, "walk-l", boot=False,
                ram_file=io.BytesIO(sram_bytes))
    # Title -> menu -> CONTINUE -> LOAD GAME -> slot 1.
    s.check("title menu", s.press_until(
        "start", lambda: s.text_has("NEW GAME"), label="menu"),
        expected="NEW GAME", actual="none")
    s.check("continue entry", s.press_until(
        "down", lambda: s.text_has("> CONTINUE"), label="continue"),
        expected="> CONTINUE", actual="none")
    s.press("a", settle=40)
    s.check("load screen", s.press_until(
        "a", lambda: s.text_has("LOAD GAME"), label="load screen"),
        expected="LOAD GAME", actual="none")
    s.check("slot loaded", s.press_until(
        "a", lambda: not s.text_has("LOAD GAME"), label="slot 1"),
        expected="closed", actual="open")
    s.settle_scene(expected_scene=exp["scene"])
    st = s.reader.scene_state()
    s.check_eq("restored scene", st["scene_id"], exp["scene"])
    s.check_eq("restored gold", s.reader.gold(), exp["gold"])
    s.check_eq("restored ring", s.reader.collection_count(CARD_WOOD_RING),
               exp["ring"])
    s.check_eq("restored deck", s.reader.deck_count(), exp["deck"])
    s.close()
