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
                                      STORY_FLAG_ID_ARRIVED_TOWN,
                                      STORY_FLAG_ID_MET_MAYOR,
                                      VARIABLE_ID_QUEST_MONSTER_HUNT,
                                      CARD_WOOD_RING, HERO_START_GOLD,
                                      HERO_START_HP, MUSIC_FOREST,
                                      MUSIC_DUNGEON)

WALK_SECONDS = 300        # per-walk wall-clock cap
MAX_BATTLE_ROUNDS = 14
from walkthrough.state_reader import (ATTACK_TYPES, BT_SHIELD, BT_EMPTY)
# collision lives in tools/level_compiler; set the path here rather than
# borrowing route.py's namespace, so a future refactor of route.py's import
# list cannot silently break this module.
sys.path.insert(0, os.path.join(REPO, "tools", "level_compiler"))
from collision import effective_actor_flags   # noqa: E402

_BTNS = {(0, -1): "up", (0, 1): "down", (-1, 0): "left", (1, 0): "right"}


def _level(name):
    return json.load(open(os.path.join(LEVELS_DIR, name + ".json")))


# ── shared step helpers ──────────────────────────────────────────────

def _walk_path(s, path):
    """Execute a BFS cell path as discrete one-tile presses."""
    for dx, dy in path:
        btn = _BTNS[(dx, dy)]
        fx, fy = s.pos()
        s.check("route move %s" % btn,
                s.walk_btn(btn, lambda: s.pos() == (fx + dx, fy + dy)),
                expected="tile commit", actual="pos %s" % (s.pos(),))


def follow(s, planner, from_scene, start, to_scene, goal,
           capture_when=None, capture_label=None):
    """Execute a cross-scene BFS route; optional mid-route screenshot."""
    steps = planner.route(from_scene, start, to_scene, goal)
    for step in steps:
        if step[0] == "move":
            _walk_path(s, [step[1:]])
        else:
            _, direction, e = step
            s.check("exit %s -> %s" % (direction, e["target_scene"]),
                    s.cross_exit(direction,
                                 planner.scenes[e["target_scene"]].scene_id),
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
    each execute)."""
    # Select phase: TARGET banner (fallback literal PLAYER TURN).
    s.wait_for(lambda: s.text_has("TARGET ")
               or s.text_has("PLAYER TURN"), ticks=180)
    _select_cards(s, ATTACK_TYPES)
    s.press("select", settle=40)     # execute attack
    if s.text_has("VICTORY") or s.text_has("DEFEATED"):
        return True
    # Defense phase: <name>: DEFEND! banner.
    s.wait_for(lambda: s.text_has("DEFEND"), ticks=240)
    _select_cards(s, (BT_SHIELD,))
    s.press("select", settle=40)     # execute defense
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
    field = _level("field")
    spawn = (field["player"]["spawn"]["x"], field["player"]["spawn"]["y"])
    s.shoot("00-boot-field")

    # Route into town; capture the camera-scrolled field shot on the way.
    # Town entry is the field-east mirrored spawn (1,7): one inside the
    # west edge at the crossed row.
    follow(s, planner, "field", spawn, "town", (1, 7),
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
    s.check("guard dialogue opened",
            bump_actor(s, planner, "town", (9, 8), "right", "GUARD:"),
            expected="GUARD:", actual="none")
    s.shoot("03-guard-dialogue", need="GUARD:")
    # The box is background tiles at rows 12-17; no actor OAM entry (the
    # town fire/dog use enemy-kind OAM art) may draw over it.  Box top
    # row 12 -> stored OAM y 112.  Regression for the fire/dog overlay.
    s.tick(4)
    _over = [y for y in s.oam_actor_ys() if y >= 112]
    s.check("guard dialogue: no actor sprite over the box", not _over,
            expected="actor OAM y < 112 (box top row 12)",
            actual=str(_over))
    s.press("a", settle=30)
    s.shoot("04-dialogue-next")
    close_dialogue(s, "GUARD:")

    # Mayor: interact event sets MET_MAYOR + starts the quest.
    s.check("mayor dialogue opened",
            bump_actor(s, planner, "town", (9, 5), "right", "MAYOR:",
                       tries=6),
            expected="MAYOR:", actual="none")
    close_dialogue(s, "MAYOR:")
    s.check_eq("MET_MAYOR flag", s.reader.flag(STORY_FLAG_ID_MET_MAYOR),
               True)
    s.check_eq("quest monster hunt active",
               s.reader.variable(VARIABLE_ID_QUEST_MONSTER_HUNT), 1)

    # Shop purchase: shopkeeper at (9,3) bumped from (9,4) upwards.
    s.check("shop opened",
            bump_actor(s, planner, "town", (9, 4), "up", "SHOP"),
            expected="SHOP", actual="none")
    s.shoot("05-shop", need="SHOP")
    gold0 = s.reader.gold()
    price = s.reader.card_price(CARD_WOOD_RING)
    s.check_eq("gold before purchase", gold0, HERO_START_GOLD)
    s.press("a", settle=30)
    s.check_eq("gold after purchase", s.reader.gold(), gold0 - price)
    s.check_eq("ring in collection", s.reader.collection_count(
        CARD_WOOD_RING), 1)
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


# ── Walk B: slime battle -> victory + loot ───────────────────────────

def walk_b(planner, checks):
    s = Session(checks, "walk-b")
    field = _level("field")
    spawn = (field["player"]["spawn"]["x"], field["player"]["spawn"]["y"])
    slime = next((o for o in field["objects"]
                  if (o.get("properties") or {}).get("entity_id")
                  == "ENTITY_ID_SLIME"), None)
    slime_xy = (slime["position"]["x"], slime["position"]["y"])
    props = slime["properties"]
    s.check("slime engaged",
            engage_hostile(s, planner, "field", slime_xy),
            expected="DECK:", actual="none")
    s.tick(40)
    s.shoot("09-battle", need="DECK:")
    s.check_eq("hero hp at battle start", s.reader.battle_player_hp(),
               HERO_START_HP)
    s.check_eq("slime hp at battle start", s.reader.battle_enemy_hp(0),
               props["hp"])

    # Fight with hand-aware selection until the trio drops (variance-
    # safe: total enemy HP strictly decreases per landed hit; never
    # assert exact damage).  Bounded rounds + defeat detection.
    prev_total = sum(s.reader.battle_enemy_hp(i) for i in range(3))
    start_total = prev_total
    victory = False
    defeated = False
    for round_no in range(MAX_BATTLE_ROUNDS):
        done = _battle_round(s)
        if round_no == 0:
            s.shoot("10-battle-attack")
        if done:
            victory = s.text_has("VICTORY")
            defeated = s.text_has("DEFEATED")
            break
        if s.reader.battle_player_hp() == 0:
            defeated = True
            break
        total = sum(s.reader.battle_enemy_hp(i) for i in range(3))
        if total < prev_total:
            s.check("damage dealt round %d" % round_no, True)
            prev_total = total
    s.check("battle victory", victory, expected="VICTORY",
            actual="enemy hp %d/%d after %d rounds%s"
            % (prev_total, start_total, MAX_BATTLE_ROUNDS,
               " (hero defeated)" if defeated else ""))
    s.shoot("11-battle-victory", need="VICTORY")

    # Leave the result screen; prove the overworld return.  The loot
    # hook credits gold on battle end, so assert gold AFTER the exit.
    s.press("a", settle=30)
    s.check("aftermath overworld",
            s.wait_for(lambda: s.reader.scene_state()["scene_id"]
                       == SCENE_FIELD, ticks=180),
            expected="SCENE_FIELD",
            actual="scene %d" % s.reader.scene_state()["scene_id"])
    s.check_eq("victory gold", s.reader.gold(),
               HERO_START_GOLD + props.get("gold_reward", 0))
    s.check("hero hp post-battle sane",
            0 < s.reader.hero()["hp"] <= HERO_START_HP, expected="1..10",
            actual=str(s.reader.hero()["hp"]))
    s.walk_btn("up", lambda: s.pos()[1] == 7)
    s.walk_btn("down", lambda: s.pos()[1] == 8)
    s.tick(30)
    s.shoot("11-battle-aftermath")
    s.close()


# ── Walk C: forest gate ──────────────────────────────────────────────

def walk_c(planner, checks):
    s = Session(checks, "walk-c")
    field = _level("field")
    spawn = (field["player"]["spawn"]["x"], field["player"]["spawn"]["y"])
    # Forest arrival = the field's north link (content-coupled).
    assert (field.get("neighbors", {}) or {}).get("north") == "forest", \
        "walk_c expects field.north == forest"
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
    field = _level("field")
    spawn = (field["player"]["spawn"]["x"], field["player"]["spawn"]["y"])
    castle = _level("castle")
    mimic = next((o for o in castle["objects"]
                  if (o.get("properties") or {}).get("battle")
                  == "BATTLE_MIMIC"), None)
    mimic_xy = (mimic["position"]["x"], mimic["position"]["y"])
    # One multi-hop BFS route: field -> south_field -> mountain_pass ->
    # castle; per-exit scene asserts run inside follow().
    follow(s, planner, "field", spawn, "castle",
           (mimic_xy[0] - 1, mimic_xy[1]))
    s.check_eq("castle scene", s.reader.scene_state()["scene_id"],
               SCENE_CASTLE)
    s.check_eq("castle music", s.reader.music_track(), MUSIC_DUNGEON)
    s.check("mimic engaged",
            engage_hostile(s, planner, "castle", mimic_xy),
            expected="DECK:", actual="none")
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
    candidate makes the World tail byte match the expected tileset kind
    — a systematic ROM/.sym/reader mismatch (stale .sym, mixed tree
    state, rebuild race), never timing jitter.  Report that loudly
    instead of a bare "-1" so triage checks build/ first (see
    docs/verify-walkthrough.md §4.2)."""
    got = s.reader.world_hostile_count(_tileset_kind(level))
    if got == -1:
        s.check("sweep %s hostiles" % name, False,
                expected="world mirror match (World tail tileset byte)",
                actual="MISMATCH: ROM/.sym inconsistent with "
                       "state_reader.py mirror — check build/ matches "
                       "sources (stale .sym? mixed tree state?), "
                       "not a content failure")
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
    field = _level("field")
    spawn = (field["player"]["spawn"]["x"], field["player"]["spawn"]["y"])
    for name in sorted(planner.scenes):
        scene = planner.scenes[name]
        want_music = _music_enum(scene.level["map"].get("music"))
        if want_music is None:
            checks.append(("sweep", "sweep %s music" % name, False,
                           "MUSIC_* mirror in state_reader.py",
                           str(scene.level["map"].get("music"))))
            want_music = -1
        s = Session(checks, "sweep")
        if name == "field":
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
        follow(s, planner, "field", spawn, name, goal)
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
            ("20-tutorial-slide4", "6/turn"),
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
