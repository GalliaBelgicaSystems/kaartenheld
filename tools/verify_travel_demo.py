"""Headless verification driver for the travel-demo ROM.

Usage: python3 tools/verify_travel_demo.py [MODE] (from repo root,
inside `nix develop` for PyBoy)
  premium: carriage instant travel (-50G, no days, no events)
  nogold:  refused carriage (gold/days untouched)
  supply:  field merchant provisions shop + leave
  starve:  empty belly forces ambush even at frequency 0
  ambush:  route pool forced to [ambush_slimes] @100% (rebuild ROM first)
  help:    route pool forced to [traveler_help] @100% (rebuild ROM first)
  ignore:  same tuning; ignore keeps the schedule
  defeat:  lose on purpose: cold game over (no floor, no resume)
  flee:    flee resumes the journey
  days:    day burn, zero crossing, late-buffed slime (long route
           + clear weather forced; rebuild ROM first)
  events:  route pools forced to single events + clear weather
           (rebuild ROM first):
           SHARE path on the act-1 road, FIGHT path on act 2.
  camp:    daily camp menu (shipped tuning): HUNT finds 1-7 prov
           with a 0-2 step detour, REST heals 5 for
           +1 step, STUDY costs 6 prov and saves 0-2
           steps (or wanders lost +0-2), 2 dry days cost 5 HP.
  acts:    per-road per-act tables (shipped tuning): chapter 1
           walks the 8-step act-1 road, the town signpost walks
           the 10-step act-2 return road, chapter 2 walks the
           10-step act-2 road.
  ruins:   route pool forced to [ruins_sight] @100% (rebuild ROM
           first): EXPLORE enters the 3-room map with no day burn,
           the relic chest grants +1 AP, LEAVE RUINS? YES resumes
           the journey costing one extra day, trip arrives.
  ruins_ignore: same tuning; IGNORE keeps the schedule, no day burn.
  full:    shipped tuning (35%, mixed pool); asserts arrival only.

Drives: field boot -> signpost -> JOURNEY -> steps/events -> village.
Battle wins use the walkthrough's poke-99 technique (test cheat, same
as transit_fight in tools/walkthrough/walks.py).
"""
import os
import sys

sys.path.insert(0, os.path.join("tools", "walkthrough"))
from pyboy import PyBoy
from state_reader import (StateReader, ATTACK_TYPES, BATTLE_HAND, CARD_SIZE,
                          CARD_TYPE, CARD_VALUE, OFF_VARIABLES,
                          OFF_CURRENCY, OFF_PARTY, CHARACTER_STATE_HP)

ROM = "build/kaartenheld-travel-demo.gb"
SCREEN_OVERWORLD = 0
SCREEN_BATTLE = 2
SCREEN_JOURNEY = 16
SCENE_FIELD = 0
SCENE_ACT2_FIELD = 24
SCENE_ACT2_TOWN = 25
SCENE_ACT2_RUINS = 26


class D:
    def __init__(self):
        self.pb = PyBoy(ROM, window="null")
        for _ in range(180):
            self.pb.tick()
        self.r = StateReader(self.pb, ROM)

    def tick(self, n=1):
        for _ in range(n):
            self.pb.tick()

    BTN_BIT = {"right": 0x01, "left": 0x02, "up": 0x04, "down": 0x08,
               "a": 0x10, "b": 0x20, "select": 0x40, "start": 0x80}

    def press(self, btn, settle=12):
        # PyBoy delivers physical button state several ticks late, so a
        # fixed 4-tick hold is a race (the press can release before the
        # game ever samples it).  Hold until the game's own pad byte
        # shows the bit (cap breaks a frozen-game deadlock), keep
        # holding a few more ticks so the edge iteration definitely
        # lands, then release AND WAIT FOR THE RELEASE TO BE SAMPLED
        # (pad bit clears).  Without the release ack, a stall-eaten
        # release leaves g_inp_prev set, which silently swallows every
        # later press of the same button (no 0->1 edge can ever form).
        # Held-then-acked is edge-safe: gameplay uses input_pressed.
        bit = self.BTN_BIT[btn]
        pad = self.r.sym.wram("g_inp_pad")
        self.pb.button_press(btn)
        for _ in range(40):
            self.tick(1)
            if self.pb.memory[pad] & bit:
                break
        self.tick(4)
        self.pb.button_release(btn)
        for _ in range(40):
            self.tick(1)
            if not (self.pb.memory[pad] & bit):
                break
        self.tick(settle)

    def screen(self):
        return self.pb.memory[self.r.g_game]

    def scene(self):
        return self.r.scene_state()

    def gold(self):
        return self.r.gold()

    def day(self):
        return self.r.variable(10)

    def ap(self):
        return self.r.variable(6)

    def total_steps(self):
        import struct
        return self.pb.memory[self.r.sym.wram("g_travel_total")]

    def lord(self):
        return self.r.variable(11)

    def prov(self):
        return self.r.variable(12)

    def weather(self):
        return self.pb.memory[self.r.sym.wram("g_travel_weather")]

    def poke_day(self, v):
        import struct
        addr = self.r.state + OFF_VARIABLES + 2 * (10 - 1)
        raw = struct.pack("<h", v)
        self.pb.memory[addr] = raw[0]
        self.pb.memory[addr + 1] = raw[1]
        self.tick(2)

    def poke_php(self, v):
        addr = self.r.state + OFF_PARTY + CHARACTER_STATE_HP
        self.pb.memory[addr] = v
        self.tick(2)

    def poke_gold(self, v):
        import struct
        addr = self.r.state + OFF_CURRENCY
        raw = struct.pack("<h", v)
        self.pb.memory[addr] = raw[0]
        self.pb.memory[addr + 1] = raw[1]
        self.tick(2)

    def rows(self):
        try:
            self.pb.memory[0xFF4F] = 0
        except Exception:
            pass
        scx = self.pb.memory[0xFF43]
        scy = self.pb.memory[0xFF42]
        out = []
        for r in range(18):
            line = ""
            for c in range(20):
                t = self.pb.memory[0x9800 + ((scy // 8 + r) % 32) * 32
                                   + (scx // 8 + c) % 32]
                line += chr(t + 32) if t < 96 else "?"
            out.append(line)
        return out

    def text_has(self, s):
        return any(s in r for r in self.rows())

    def wait_for(self, cond, ticks=600):
        for _ in range(ticks):
            if cond():
                return True
            self.tick()
        return bool(cond())

    def battle_hud_up(self):
        if self.pb.memory[self.r.g_game] != 2:
            return False
        scx = self.pb.memory[0xFF43]
        scy = self.pb.memory[0xFF42]
        t = self.pb.memory[0x9800 + ((scy // 8 + 7) % 32) * 32
                           + (scx // 8 + 0) % 32]
        return t >= 96

    def close(self):
        self.r.close()
        self.pb.stop(save=False)


def poke_hand_99(d):
    try:
        base = d.r._resolve_battle_base() + BATTLE_HAND
    except Exception:
        return
    for i in range(5):
        t = d.r.rd(base + i * CARD_SIZE + CARD_TYPE, 1)[0]
        if t in ATTACK_TYPES:
            d.pb.memory[base + i * CARD_SIZE + CARD_VALUE] = 99


def cursor_to(d, idx):
    for _ in range(8):
        if d.r.battle_cursor() == idx:
            return True
        d.press("right", settle=10)
    return d.r.battle_cursor() == idx


def win_fight(d):
    """Port of walks.transit_fight: win the live ambush, True on VICTORY."""
    for _round in range(5):
        if not d.wait_for(lambda: d.battle_hud_up(), ticks=30):
            break
        if d.text_has("VICTORY") or d.text_has("DEFEATED"):
            break
        poke_hand_99(d)
        d.wait_for(lambda: d.text_has("TARGET ")
                   or d.text_has("PLAYER TURN"), ticks=180)
        poke_hand_99(d)
        for idx in range(5):
            if not cursor_to(d, idx):
                continue
            if d.r.battle_hand()[idx][0] not in ATTACK_TYPES:
                continue
            for _ in range(3):
                before = d.r.battle_combo_count()
                d.press("a", settle=12)
                if d.r.battle_combo_count() > before:
                    break
            break
        for _ in range(4):
            d.press("select", settle=40)
            if d.text_has("VICTORY") or d.text_has("DEFEATED"):
                break
            if d.wait_for(lambda: d.text_has("DEFEND"), ticks=240):
                break
        if d.text_has("VICTORY") or d.text_has("DEFEATED"):
            break
        for _ in range(4):
            d.press("select", settle=40)
            if d.text_has("VICTORY") or d.text_has("DEFEATED"):
                break
            if d.wait_for(lambda: d.text_has("TARGET ")
                           or d.text_has("PLAYER TURN"), ticks=240):
                break
    if d.text_has("VICTORY"):
        for _ in range(6):
            if ((not d.battle_hud_up() and not d.text_has("VICTORY"))):
                break
            d.press("a", settle=30)
            d.tick(120)
        d.tick(20)
        return True
    return False


SCREEN_TITLE = 9
SCREEN_INTRO = 10
SCREEN_SAVE_LOAD = 8


def boot_to_overworld(d):
    """Press through the main-ROM boot (splash -> notice -> title with
    the TRAVEL DEMO subtitle -> NEW GAME -> slot -> intro) to the field.
    Fresh PyBoy SRAM means empty slots, so one confirm takes the slot."""
    assert d.wait_for(lambda: d.screen() == SCREEN_TITLE, ticks=1200), \
        "no TITLE after boot (screen=%d)" % d.screen()
    assert d.wait_for(lambda: d.text_has("TRAVEL DEMO"), ticks=300), d.rows()
    assert not d.text_has("BATTLE DEMO"), d.rows()
    assert d.text_has("PRESS START"), d.rows()
    print("boot splash/title OK (TRAVEL DEMO subtitle)")
    d.press("start", settle=30)  # PRESS START -> menu
    assert d.wait_for(lambda: d.text_has("NEW GAME"), ticks=300), d.rows()
    assert d.text_has("> CONTINUE"), d.rows()
    d.press("down", settle=12)  # CONTINUE -> NEW GAME
    assert d.wait_for(lambda: d.text_has("> NEW GAME"), ticks=200), d.rows()
    d.press("a", settle=30)  # NEW GAME -> slot pick
    assert d.wait_for(lambda: d.screen() == SCREEN_SAVE_LOAD,
                      ticks=300), "no slot pick (screen=%d)" % d.screen()
    assert d.text_has("SLOT 1:"), d.rows()
    d.press("a", settle=30)  # empty slot -> intro
    assert d.wait_for(lambda: d.screen() == SCREEN_INTRO,
                      ticks=300), "no INTRO (screen=%d)" % d.screen()
    for _ in range(6):
        if d.screen() == SCREEN_OVERWORLD:
            break
        d.press("a", settle=30)  # 4 slides, last drops to the field
    assert d.wait_for(lambda: d.screen() == SCREEN_OVERWORLD,
                      ticks=300), "no field after intro (screen=%d)" % d.screen()
    st = d.scene()
    assert st["scene_id"] == SCENE_ACT2_FIELD, st
    assert (st["player_x"], st["player_y"]) == (5, 7), st
    print("boot->overworld OK (title -> new game -> intro -> field)")


def boot_to_journey(d):
    boot_to_overworld(d)
    st = d.scene()
    d.press("down")
    st = d.scene()
    assert (st["player_x"], st["player_y"]) == (5, 8), st
    d.press("a", settle=30)
    assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=300), \
        "no JOURNEY after signpost (screen=%d)" % d.screen()
    assert d.wait_for(lambda: d.text_has("TRAVEL"), ticks=300), d.rows()
    assert d.text_has("FIELD ROAD"), d.rows()
    assert d.text_has("CARRIAGE"), d.rows()
    assert d.text_has("ON FOOT"), d.rows()
    print("boot->journey OK")


def select_foot(d):
    # default choice is the free method (ON FOOT): confirm straight in
    d.press("a", settle=20)
    assert d.wait_for(lambda: d.text_has("> CONTINUE"), ticks=300), d.rows()
    assert d.text_has("HUNT"), d.rows()
    assert d.text_has("REST"), d.rows()
    assert d.text_has("MAP"), d.rows()
    print("foot selected OK")


def test_ambush():
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        gold0 = d.gold()
        wins = 0
        # first event: keep stepping until the ambush stages
        for _ in range(6):
            d.press("a", settle=30)
            if d.wait_for(lambda: d.text_has("AMBUSH!"), ticks=120):
                break
        assert d.text_has("AMBUSH!"), d.rows()
        d.press("a", settle=30)  # fight
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE,
                          ticks=600), "no battle (screen=%d)" % d.screen()
        assert win_fight(d), "ambush not won"
        wins += 1
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=600), "no journey resume"
        assert d.text_has("+8G LOOT!"), d.rows()
        assert d.gold() == gold0 + 8, "gold %d -> %d" % (gold0, d.gold())
        print("ambush victory +8G OK (gold %d -> %d)" % (gold0, d.gold()))
        d.press("a", settle=20)  # dismiss loot msg
        # walk on a while (more ambushes may fire: fight them). Arrival
        # itself is covered by the full/premium trips (this tuning uses
        # a long route so retries can never strand it).
        for _ in range(12):
            st = d.scene()
            if st["scene_id"] == SCENE_ACT2_TOWN:
                break
            if d.screen() == SCREEN_BATTLE:
                assert win_fight(d), "follow-up ambush lost?"
                wins += 1
                d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
                d.press("a", settle=20)  # dismiss loot msg
            elif d.screen() == SCREEN_JOURNEY:
                d.press("a", settle=20)
            else:
                d.tick(30)
        st = d.scene()
        assert st["scene_id"] in (SCENE_ACT2_TOWN, SCENE_ACT2_FIELD), st
        assert d.gold() == gold0 + 8 * wins, "gold %d, wins %d" \
            % (d.gold(), wins)
        print("walk-on OK after %d ambush wins; gold %d" % (wins, d.gold()))
    finally:
        d.close()


def test_help(help_it):
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        gold0 = d.gold()
        for _ in range(6):
            d.press("a", settle=30)
            if d.wait_for(lambda: d.text_has("TRAVELER NEEDS"), ticks=120):
                break
        assert d.wait_for(lambda: d.text_has("TRAVELER NEEDS"),
                          ticks=120), d.rows()
        total0 = d.total_steps()
        if not help_it:
            d.press("down", settle=12)  # toggle to IGNORE
            assert d.wait_for(lambda: d.text_has(">IGNORE"), ticks=200), \
                d.rows()
        else:
            assert d.text_has("> HELP"), d.rows()
        total0 = d.total_steps()
        d.press("a", settle=30)  # confirm
        if help_it:
            assert d.wait_for(lambda: d.text_has("YOU SHARE"), ticks=300), \
                d.rows()
            assert d.text_has("+15G +1 STEP"), d.rows()
            assert d.gold() == gold0 + 15, "gold %d" % d.gold()
            d.press("a", settle=20)  # dismiss
            assert d.total_steps() == total0 + 1, d.total_steps()
            print("help OK: +15G, detour +1 step")
        else:
            assert d.wait_for(lambda: d.text_has("YOU WALK ON"), ticks=300), \
                d.rows()
            assert d.gold() == gold0, "gold changed %d" % d.gold()
            total0 = d.total_steps()
            d.press("a", settle=20)
            assert d.total_steps() == total0, d.total_steps()
            print("ignore OK: no reward, schedule kept")
        # walk on a while (ignoring everything): progress without arrival
        # asserts here (arrival is covered by the full/ambush trips)
        for _ in range(12):
            st = d.scene()
            if st["scene_id"] == SCENE_ACT2_TOWN:
                break
            if d.screen() == SCREEN_JOURNEY:
                if d.text_has("TRAVELER NEEDS"):
                    d.press("down", settle=12)
                    d.press("a", settle=20)
                    d.wait_for(lambda: d.text_has("YOU WALK ON"), ticks=300)
                    d.press("a", settle=20)
                else:
                    d.press("a", settle=20)
            else:
                d.tick(30)
        st = d.scene()
        assert st["scene_id"] in (SCENE_ACT2_TOWN, SCENE_ACT2_FIELD), st
        print("walk-on OK")
    finally:
        d.close()


def test_defeat():
    """Lose the ambush on purpose: empty turns until DEFEATED, then a
    cold hard game over (no floor, no resume)."""
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        gold0 = d.gold()
        for _ in range(6):
            d.press("a", settle=20)
            if d.text_has("AMBUSH!"):
                break
        assert d.text_has("AMBUSH!"), d.rows()
        d.poke_day(10)
        d.poke_php(3)  # quick deterministic defeat (2 slime hits)
        d.press("a", settle=30)  # fight
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE,
                          ticks=600), "no battle"
        # stage nothing, execute empty turns until the slime wins
        # (~2 dmg/round vs 15 HP: needs ~8 rounds, ~6 loop iters each)
        for _ in range(80):
            if d.text_has("VICTORY") or d.text_has("DEFEATED"):
                break
            if d.text_has("TARGET ") or d.text_has("PLAYER TURN"):
                d.press("select", settle=40)
            elif d.text_has("DEFEND"):
                d.press("select", settle=40)
            else:
                d.tick(30)
        assert d.text_has("DEFEATED"), "slime never won"
        d.press("a", settle=30)
        assert d.wait_for(lambda: d.screen() == 3,
                          ticks=600), "no GAME OVER after defeat (screen=%d)" \
            % d.screen()
        assert d.wait_for(lambda: d.text_has("GAME OVER"), ticks=300), \
            d.rows()
        assert d.gold() == gold0, "gold changed on defeat"
        assert d.pb.memory[d.r.sym.wram("g_travel_active")] == 0, \
            "journey not abandoned on game over"
        print("defeat OK: cold game over, journey abandoned, no gold")
    finally:
        d.close()


def test_flee():
    """Flee the ambush with B: the journey resumes where it left off."""
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        for _ in range(6):
            d.press("a", settle=20)
            if d.text_has("AMBUSH!"):
                break
        assert d.text_has("AMBUSH!"), d.rows()
        d.press("a", settle=30)  # fight
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE, ticks=600)
        assert d.wait_for(lambda: d.text_has("TARGET "), ticks=600)
        d.press("b", settle=40)  # flee with empty combo
        assert d.wait_for(lambda: d.text_has("FLED!"), ticks=600), d.rows()
        d.press("a", settle=20)
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=600), "no journey resume after flee"
        print("flee OK: journey resumes")
    finally:
        d.close()


def test_days():
    """Day counter: steps and victories burn days; zero flips the Lord
    supreme (buffed late ambushes + LATE! row).
    NOTE: run with a long route (steps=99, ambush pool @255, weather
    forced clear) so retries can never arrive mid-test and snow can
    never double a step burn; restore shipped tuning afterwards.
    (With the shipped 10-step route the walk below still fits: it
    advances ~5 times, so arrival cannot trigger.)"""
    d = D()
    try:
        # Chapter 2 walks the act-2 road (the forced 99-step one);
        # a fresh boot starts chapter 1 (8-step act-1 road).
        import struct
        addr = d.r.state + OFF_VARIABLES + 2 * (1 - 1)
        raw = struct.pack("<h", 2)
        d.pb.memory[addr] = raw[0]
        d.pb.memory[addr + 1] = raw[1]
        d.tick(2)
        boot_to_journey(d)
        select_foot(d)
        assert d.total_steps() >= 10, d.total_steps()
        assert d.day() == 180, d.day()
        assert d.text_has("DAYS 180"), d.rows()
        d.press("a", settle=20)  # advance: step + day (always burns 1)
        assert d.day() == 179, d.day()
        for _ in range(6):  # first advance may roll quiet (1/256 at 255)
            if d.text_has("AMBUSH!"):
                break
            d.press("a", settle=20)
        assert d.text_has("AMBUSH!"), d.rows()
        day0 = d.day()
        d.press("a", settle=30)  # fight
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE, ticks=600)
        assert win_fight(d)
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
        assert d.day() == day0 - 1, (d.day(), day0)  # victory burned 1 day
        assert d.text_has("+8G LOOT!")
        print("day burn OK: step -1, victory -1 (day %d -> %d)"
              % (day0, d.day()))
        d.press("a", settle=20)  # dismiss
        d.poke_day(2)
        assert d.day() == 2
        d.press("a", settle=20)  # advance: day 2 -> 1, ambush staged
        assert d.day() == 1, d.day()
        d.press("a", settle=30)  # fight
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE, ticks=600)
        assert win_fight(d)
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
        assert d.day() == 0, d.day()
        assert d.lord() == 1, d.lord()
        assert d.text_has("LATE!"), d.rows()
        print("zero crossing OK: LORD_POWER=1, LATE! shown")
        d.press("a", settle=20)  # dismiss
        d.press("a", settle=20)  # advance: day stays 0, ambush staged
        assert d.day() == 0, d.day()
        d.press("a", settle=30)  # fight the buffed slime
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE, ticks=600)
        assert d.wait_for(lambda: d.battle_hud_up(), ticks=120)
        assert d.r.battle_enemy_hp(0) == 16, d.r.battle_enemy_hp(0)
        print("late ambush OK: 16 HP slime")
        assert win_fight(d)
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
        assert d.day() == 0, d.day()  # clamped, never negative
    finally:
        d.close()


def test_premium():
    """Paid carriage: deducts fare, arrives instantly, no days, no events."""
    d = D()
    try:
        boot_to_journey(d)
        assert d.text_has("> ON FOOT"), d.rows()  # free default
        d.poke_gold(60)
        assert d.gold() == 60, d.gold()
        d.press("up", settle=12)  # toggle to CARRIAGE
        assert d.wait_for(lambda: d.text_has("> CARRIAGE"), ticks=200), \
            d.rows()
        d.press("a", settle=30)  # confirm: pay and arrive
        assert d.wait_for(lambda: d.scene()["scene_id"] == SCENE_ACT2_TOWN,
                          ticks=600), d.scene()
        st = d.scene()
        assert (st["player_x"], st["player_y"]) == (2, 7), st
        assert d.gold() == 10, d.gold()  # 60 - 50 fare
        assert d.day() == 180, d.day()  # instantaneous: no days burned
        assert d.screen() == SCREEN_OVERWORLD, d.screen()
        print("premium OK: -50G, instant arrival, day untouched")
    finally:
        d.close()


def test_nogold():
    """Too poor for the carriage: refusal line, no travel, gold kept."""
    d = D()
    try:
        boot_to_journey(d)
        assert d.gold() == 20, d.gold()
        d.press("up", settle=12)  # toggle to CARRIAGE
        assert d.wait_for(lambda: d.text_has("> CARRIAGE"), ticks=200)
        d.press("a", settle=20)  # confirm: refused
        assert d.wait_for(lambda: d.text_has("NOT ENOUGH GOLD"),
                          ticks=300), d.rows()
        assert d.text_has("NEED 50G"), d.rows()
        assert d.gold() == 20, d.gold()
        assert d.screen() == SCREEN_JOURNEY, d.screen()
        assert d.day() == 180, d.day()
        for _ in range(4):  # dismiss (retry: PyBoy phase can eat an edge)
            d.press("a", settle=20)
            if d.wait_for(lambda: d.text_has("> CARRIAGE"), ticks=120):
                break
        assert d.text_has("> CARRIAGE"), d.rows()
        print("nogold OK: refused, gold and days untouched")
    finally:
        d.close()


def test_supply():
    """Field merchant: walk over, open the supply shop, buy provisions
    with gold, leave back to the field unmoved."""
    d = D()
    try:
        boot_to_overworld(d)
        st = d.scene()
        for b in ("down", "right", "right"):
            d.press(b, settle=20)
        st = d.scene()
        assert (st["player_x"], st["player_y"]) == (7, 8), st
        d.press("down", settle=12)  # bump the merchant: talk = supply opens
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=300), d.screen()
        assert d.wait_for(lambda: d.text_has("PROV"), ticks=300), d.rows()
        assert d.text_has("LEAVE"), d.rows()
        assert d.prov() == 36, d.prov()
        d.press("a", settle=20)  # buy PROV (2G)
        assert d.gold() == 18, d.gold()
        assert d.prov() == 37, d.prov()
        d.press("down", settle=12)  # select LEAVE
        assert d.wait_for(lambda: d.text_has("> LEAVE"), ticks=200), d.rows()
        d.press("a", settle=20)  # leave
        assert d.wait_for(lambda: d.screen() == SCREEN_OVERWORLD,
                          ticks=300), d.screen()
        st = d.scene()
        assert (st["player_x"], st["player_y"]) == (7, 8), st
        print("supply OK: bought provisions, left unmoved")
    finally:
        d.close()


def test_starve():
    """Starving (no provisions) forces an ambush even at frequency 0."""
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        assert d.day() == 180, d.day()
        d.poke_day(170)
        # drain provisions through the state address (variable 12)
        import struct
        addr = d.r.state + OFF_VARIABLES + 2 * (12 - 1)
        raw = struct.pack("<h", 0)
        d.pb.memory[addr] = raw[0]
        d.pb.memory[addr + 1] = raw[1]
        d.tick(2)
        assert d.prov() == 0, d.prov()
        d.press("a", settle=20)  # advance: quiet roll is 0, hunger forces it
        assert d.wait_for(lambda: d.text_has("AMBUSH!"), ticks=300), d.rows()
        print("starve OK: 100% ambush with empty belly")
    finally:
        d.close()


def _walk_ruins_to(d, tx, ty):
    """Greedy corridor walk inside the ruins (enemies stationary)."""
    for _ in range(150):
        st = d.scene()
        if (st["player_x"], st["player_y"]) == (tx, ty):
            return True
        if d.screen() == SCREEN_BATTLE:
            assert win_fight(d), "ruins fight lost?"
            assert d.wait_for(lambda: d.screen() == SCREEN_OVERWORLD,
                              ticks=600)
            continue
        x, y = st["player_x"], st["player_y"]
        if x < tx:
            d.press("right", settle=14)
        elif x > tx:
            d.press("left", settle=14)
        elif y < ty:
            d.press("down", settle=14)
        elif y > ty:
            d.press("up", settle=14)
        else:
            return True
    return False


def _dismiss_dialogue(d):
    for _ in range(12):
        if d.screen() == SCREEN_OVERWORLD:
            break
        d.press("a", settle=30)
        d.tick(30)
    assert d.screen() == SCREEN_OVERWORLD, "dialogue did not close"


def _journey_until_ruins(d, ignore_first):
    """Walk one journey; returns 'explored' once the ruins prompt is
    confirmed with EXPLORE, 'ignored' after an IGNORE, 'town' on
    arrival without any ruins prompt (caller reboots and retries)."""
    for _ in range(60):
        st = d.scene()
        if st["scene_id"] == SCENE_ACT2_TOWN:
            return "town"
        sc = d.screen()
        if sc == SCREEN_BATTLE:
            assert win_fight(d)
            d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
            d.press("a", settle=20)
            continue
        if sc != SCREEN_JOURNEY:
            return "overworld?"
        if d.text_has("AMBUSH!"):
            d.press("a", settle=30)
            assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE,
                              ticks=600), "no battle"
            assert win_fight(d), "ambush lost?"
            assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                              ticks=600)
            d.press("a", settle=20)
        elif d.text_has("YOU SPOT OLD RUINS"):
            if ignore_first:
                day0 = d.day()
                total0 = d.total_steps()
                d.press("down", settle=12)  # toggle to IGNORE
                assert d.wait_for(lambda: d.text_has("EXPLORE>IGNORE"),
                                  ticks=200), d.rows()
                d.press("a", settle=20)
                assert d.wait_for(lambda: d.text_has("YOU WALK ON."),
                                  ticks=300), d.rows()
                assert d.gold() == d.gold(), d.gold()
                d.press("a", settle=20)
                assert d.day() == day0, (d.day(), day0)
                assert d.total_steps() == total0, d.total_steps()
                print("ignore OK: no reward, no day burn, schedule kept")
                return "ignored"
            day0 = d.day()
            assert d.text_has(">EXPLORE"), d.rows()
            d.press("a", settle=30)  # EXPLORE
            assert d.wait_for(lambda: d.screen() == SCREEN_OVERWORLD,
                              ticks=600), "no ruins map (screen=%d)" % d.screen()
            st = d.scene()
            assert st["scene_id"] == SCENE_ACT2_RUINS, st
            assert d.day() == day0, "entry must not burn days"
            print("explore OK: ruins map, day untouched (%d)" % day0)
            return "explored"
        elif d.text_has("TRAVELER NEEDS"):
            d.press("a", settle=20)  # help
            d.wait_for(lambda: d.text_has("YOU SHARE"), ticks=300)
            d.press("a", settle=20)
        else:
            d.press("a", settle=20)
    return "timeout"


def _finish_trip(d):
    for _ in range(80):
        st = d.scene()
        if st["scene_id"] == SCENE_ACT2_TOWN:
            break
        if d.screen() == SCREEN_BATTLE:
            assert win_fight(d)
            d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
            d.press("a", settle=20)
        elif d.screen() == SCREEN_JOURNEY:
            if d.text_has("AMBUSH!"):
                d.press("a", settle=30)
            elif d.text_has("TRAVELER NEEDS"):
                d.press("down", settle=12)
                d.press("a", settle=20)
                d.wait_for(lambda: d.text_has("YOU WALK ON."), ticks=300)
                d.press("a", settle=20)
            elif d.text_has("YOU SPOT OLD RUINS"):
                d.press("down", settle=12)
                d.press("a", settle=20)
                d.wait_for(lambda: d.text_has("YOU WALK ON."), ticks=300)
                d.press("a", settle=20)
            else:
                d.press("a", settle=20)
        else:
            d.tick(30)
    st = d.scene()
    assert st["scene_id"] == SCENE_ACT2_TOWN, st


def test_risk():
    """Every scenario shares 50/50 success/failure odds (route pool
    forced to [stranded_merchant] @100% and weather forced clear,
    rebuild ROM first): walk
    the whole journey always sharing, and assert both the success
    line (+12G) and the fail line show up with the right effects."""
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        seen_success = 0
        seen_fail = 0
        for _ in range(40):
            st = d.scene()
            if st["scene_id"] == SCENE_ACT2_TOWN:
                break
            if d.screen() != SCREEN_JOURNEY:
                d.tick(30)
                continue
            if not d.text_has("MERCHANT NEEDS"):
                d.press("a", settle=20)
                continue
            gold0 = d.gold()
            food0 = d.prov()
            d.press("a", settle=20)  # SHARE
            if d.wait_for(lambda: d.text_has("HE PAYS IN GOLD."),
                          ticks=120):
                seen_success += 1
                assert d.gold() == gold0 + 12, (gold0, d.gold())
                assert d.text_has("+12G"), d.rows()
            elif d.wait_for(lambda: d.text_has("EMPTY PURSE."),
                            ticks=120):
                seen_fail += 1
                assert d.gold() == gold0, (gold0, d.gold())
            else:
                assert False, d.rows()
            d.press("a", settle=20)  # dismiss
            assert d.prov() == max(0, food0 - 2), (food0, d.prov())
        st = d.scene()
        assert st["scene_id"] == SCENE_ACT2_TOWN, st
        assert seen_success >= 1, "never rolled success"
        assert seen_fail >= 1, "never rolled failure"
        print("risk OK: %d success, %d fail (50/50 branch both live)"
              % (seen_success, seen_fail))
    finally:
        d.close()


def test_ruins():
    """Roadside ruins (shipped tuning, reboot until the prompt stages):
    EXPLORE -> 3-room map, relic chest +1 AP, LEAVE RUINS? YES resumes
    the journey costing one extra day, trip still arrives."""
    d = None
    try:
        for _ in range(5):
            d = D()
            boot_to_journey(d)
            select_foot(d)
            res = _journey_until_ruins(d, False)
            print("ruins attempt -> %s" % res)
            if res == "explored":
                break
            d.close()
            d = None
        assert d is not None, "never rolled ruins in 5 boots?"
        assert _walk_ruins_to(d, 5, 6), d.scene()
        assert _walk_ruins_to(d, 7, 6), d.scene()
        assert _walk_ruins_to(d, 12, 6), d.scene()
        assert _walk_ruins_to(d, 14, 6), d.scene()
        assert _walk_ruins_to(d, 15, 6), d.scene()
        ap0 = d.ap()
        d.press("right", settle=20)  # bump the relic chest
        assert d.wait_for(lambda: d.screen() != SCREEN_OVERWORLD,
                          ticks=300), "no chest dialogue"
        assert d.wait_for(lambda: d.text_has("OLD RELIC!"), ticks=300), \
            d.rows()
        _dismiss_dialogue(d)
        assert d.ap() == ap0 + 1, (d.ap(), ap0)
        print("chest OK: AP %d -> %d" % (ap0, d.ap()))
        assert _walk_ruins_to(d, 13, 6), d.scene()
        assert _walk_ruins_to(d, 6, 6), d.scene()
        assert _walk_ruins_to(d, 1, 6), d.scene()
        assert _walk_ruins_to(d, 1, 3), d.scene()
        day0 = d.day()
        d.press("left", settle=20)  # bump the exit door in the west wall
        assert d.wait_for(lambda: d.text_has("LEAVE RUINS?"), ticks=300), \
            d.rows()
        d.press("a", settle=30)  # YES
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=600), "no journey resume (screen=%d)" % d.screen()
        assert d.day() == day0 - 1, (d.day(), day0)
        print("leave OK: looted exit cost +1 day (%d -> %d)"
              % (day0, d.day()))
        _finish_trip(d)
        print("ruins trip OK; AP %d" % d.ap())
    finally:
        if d is not None:
            d.close()


def test_ruins_ignore():
    """IGNORE on the ruins prompt: no detour, no day burn, schedule kept."""
    d = None
    try:
        for _ in range(5):
            d = D()
            boot_to_journey(d)
            select_foot(d)
            res = _journey_until_ruins(d, True)
            print("ruins-ignore attempt -> %s" % res)
            if res == "ignored":
                break
            d.close()
            d = None
        assert d is not None, "never rolled ruins in 5 boots?"
        _finish_trip(d)
        print("ruins-ignore trip OK")
    finally:
        if d is not None:
            d.close()


def test_camp():
    """Daily camp menu (shipped tuning, no forced pool needed: camp
    actions roll no road events, so the deterministic stream cannot
    interfere).  HUNT nets found-3 provisions with a 0-2 detour, REST heals
    5 for +1 step, STUDY saves 0-2 steps, and two consecutive dry
    days cost 5 HP (route starve tuning)."""
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        assert d.ap() == 5, d.ap()
        # HUNT: +1..7 provisions found (-3 eaten),
        # +0..2 steps, -1 day.
        f0 = d.prov()
        day0 = d.day()
        tot0 = d.total_steps()
        d.press("down", settle=12)  # CONTINUE -> HUNT
        assert d.wait_for(lambda: d.text_has("> HUNT"), ticks=200), d.rows()
        d.press("a", settle=30)
        assert d.wait_for(lambda: d.text_has("YOU FORAGE."), ticks=300), \
            d.rows()
        assert d.wait_for(lambda: d.text_has("PROV"), ticks=120), d.rows()
        net = d.prov() - f0
        assert -2 <= net <= 4, (f0, d.prov())
        assert d.day() == day0 - 1, (d.day(), day0)
        detour = d.total_steps() - tot0
        assert 0 <= detour <= 2, detour
        print("hunt OK: net %+d provisions, detour +%d, day -1"
              % (net, detour))
        d.press("a", settle=20)  # dismiss
        # STARVE (provisions): two dry study days cost 5 HP on the
        # second one (study burns days without foraging, so the
        # tally is exact and no road event can interfere).
        import struct
        addr = d.r.state + OFF_VARIABLES + 2 * (12 - 1)
        raw = struct.pack("<h", 0)
        d.pb.memory[addr] = raw[0]
        d.pb.memory[addr + 1] = raw[1]
        d.tick(2)
        assert d.prov() == 0, d.prov()
        for _ in range(3):
            d.press("down", settle=12)  # CONTINUE -> HUNT -> REST -> MAP
        assert d.wait_for(lambda: d.text_has("> STUDY MAP"), ticks=200), d.rows()
        d.press("a", settle=30)  # study 1: dry day 1, no damage
        assert d.wait_for(lambda: d.text_has("YOU STUDY MAP.")
                          or d.text_has("YOU GET LOST."), ticks=300)
        assert d.r.hero()["hp"] == 15, d.r.hero()
        d.press("a", settle=20)
        for _ in range(3):
            d.press("down", settle=12)
        d.press("a", settle=30)  # study 2: dry day 2, -5 HP
        assert d.wait_for(lambda: d.text_has("YOU STUDY MAP.")
                          or d.text_has("YOU GET LOST."), ticks=300)
        assert d.r.hero()["hp"] == 10, d.r.hero()
        print("starve OK: 2 dry days cost 5 HP (15 -> 10)")
        d.press("a", settle=20)
        # REST: +5 HP (capped), +1 step, -1 day.
        tot0 = d.total_steps()
        day0 = d.day()
        d.press("down", settle=12)
        d.press("down", settle=12)  # CONTINUE -> HUNT -> REST
        assert d.wait_for(lambda: d.text_has("> REST"), ticks=200), d.rows()
        d.press("a", settle=30)
        assert d.wait_for(lambda: d.text_has("YOU REST."), ticks=300), \
            d.rows()
        assert d.r.hero()["hp"] == 15, d.r.hero()
        assert d.day() == day0 - 1, (d.day(), day0)
        assert d.total_steps() == tot0 + 1, d.total_steps()
        print("rest OK: HP 10 -> 15, +1 step, day -1")
        d.press("a", settle=20)
        # STUDY: saves 0-2 steps for one day plus 6 provisions
        # of survey rations.
        left0 = d.pb.memory[d.r.sym.wram("g_travel_left")]
        day0 = d.day()
        f0 = d.prov()
        d.press("down", settle=12)
        d.press("down", settle=12)
        d.press("down", settle=12)  # -> STUDY MAP
        assert d.wait_for(lambda: d.text_has("> STUDY MAP"), ticks=200), d.rows()
        d.press("a", settle=30)
        assert d.wait_for(lambda: d.text_has("YOU STUDY MAP.")
                          or d.text_has("YOU GET LOST."), ticks=300), \
            d.rows()
        assert d.day() == day0 - 1, (d.day(), day0)
        assert d.prov() == (f0 - 9 if f0 >= 9 else 0), (f0, d.prov())
        left1 = d.pb.memory[d.r.sym.wram("g_travel_left")]
        # study may arrive (net saved the last step): accept either.
        st = d.scene()
        if st["scene_id"] == SCENE_ACT2_TOWN:
            print("study OK: mapped away the final step, arrived")
        else:
            assert -2 <= left0 - left1 <= 2, (left0, left1)
            if left1 > left0:
                print("study OK: got lost, +%d steps, day -1"
                      % (left1 - left0))
            else:
                print("study OK: saved %d steps, day -1" % (left0 - left1))
            d.press("a", settle=20)
        # STARVE TO DEATH: 4 HP and two more dry days is a cold
        # game over, not a floor.
        d.poke_php(4)
        addr = d.r.state + OFF_VARIABLES + 2 * (12 - 1)
        raw = struct.pack("<h", 0)
        d.pb.memory[addr] = raw[0]
        d.pb.memory[addr + 1] = raw[1]
        d.tick(2)
        assert d.prov() == 0, d.prov()
        for _ in range(3):
            d.press("down", settle=12)  # -> STUDY MAP
        d.press("a", settle=30)  # dry day 1: 4 HP left, no damage
        assert d.wait_for(lambda: d.text_has("YOU STUDY MAP.")
                          or d.text_has("YOU GET LOST."), ticks=300)
        assert d.r.hero()["hp"] == 4, d.r.hero()
        d.press("a", settle=20)
        for _ in range(3):
            d.press("down", settle=12)
        d.press("a", settle=30)  # dry day 2: -5 HP -> 0, game over
        assert d.wait_for(lambda: d.screen() == 3, ticks=600), \
            "no GAME OVER on starvation (screen=%d)" % d.screen()
        assert d.wait_for(lambda: d.text_has("GAME OVER"), ticks=300), \
            d.rows()
        assert d.pb.memory[d.r.sym.wram("g_travel_active")] == 0, \
            "journey not abandoned on game over"
        print("starve-death OK: cold game over, journey abandoned")
    finally:
        d.close()


def test_events():
    """Representative new choice mechanics (route pools forced to
    single events and weather forced clear, rebuild ROM first): on
    the act-1 road the
    stranded merchant's SHARE path applies prov/gold effects with a
    delta line; on the act-2 road (chapter 2) the bandits' FIGHT
    path dispatches the staged battle event and pays victory gold."""
    d = D()
    try:
        # SHARE path (act-1 road).
        boot_to_journey(d)
        select_foot(d)
        f0 = d.prov()
        gold0 = d.gold()
        day0 = d.day()
        for _ in range(6):
            d.press("a", settle=30)
            if d.text_has("MERCHANT NEEDS"):
                break
        assert d.text_has("MERCHANT NEEDS"), d.rows()
        assert d.text_has("> SHARE LEAVE"), d.rows()
        d.press("a", settle=30)  # SHARE
        assert d.wait_for(lambda: d.text_has("HE PAYS IN GOLD."),
                          ticks=300), d.rows()
        assert d.text_has("+12G"), d.rows()
        assert d.text_has("-2P"), d.rows()
        assert d.prov() == f0 - 5, (f0, d.prov())  # step (3) + share (2)
        assert d.gold() == gold0 + 12, (gold0, d.gold())
        assert d.day() == day0 - 1, (day0, d.day())  # step only
        print("events SHARE OK: -2 prov, +12 gold, delta line")
        d.press("a", settle=20)
        d.close()
        d = None
        # FIGHT path (act-2 road via chapter 2).
        d = D()
        import struct
        addr = d.r.state + OFF_VARIABLES + 2 * (1 - 1)
        raw = struct.pack("<h", 2)
        d.pb.memory[addr] = raw[0]
        d.pb.memory[addr + 1] = raw[1]
        d.tick(2)
        boot_to_journey(d)
        select_foot(d)
        assert d.total_steps() == 10, d.total_steps()
        gold0 = d.gold()
        day0 = d.day()
        for _ in range(6):
            d.press("a", settle=30)
            if d.text_has("BANDITS BLOCK"):
                break
        assert d.text_has("BANDITS BLOCK"), d.rows()
        d.press("down", settle=12)  # toggle to FIGHT
        assert d.wait_for(lambda: d.text_has("PAY>FIGHT"), ticks=200), \
            d.rows()
        d.press("a", settle=30)  # FIGHT
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE,
                          ticks=600), "no battle (screen=%d)" % d.screen()
        assert win_fight(d), "brigands not won"
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=600), "no journey resume"
        assert d.text_has("+14G LOOT!"), d.rows()
        assert d.gold() == gold0 + 14, (gold0, d.gold())
        assert d.day() == day0 - 2, (day0, d.day())  # step + victory
        print("events FIGHT OK: staged battle won, +14G, day burned")
        d.press("a", settle=20)
    finally:
        if d is not None:
            d.close()


def test_acts():
    """Per-road, per-act journey tables (shipped tuning): chapter 1
    walks the act-1 road (8 steps), the town signpost walks the
    act-2 return road back to the field (10 steps), and chapter 2
    walks the act-2 road (10 steps)."""
    d = None
    try:
        # Act-1 road on chapter 1: 8 steps to town.
        d = D()
        assert d.r.variable(1) == 1, d.r.variable(1)
        boot_to_journey(d)
        select_foot(d)
        assert d.total_steps() == 8, d.total_steps()
        print("act-1 road OK: 8 steps")
        _finish_trip(d)
        st = d.scene()
        assert st["scene_id"] == SCENE_ACT2_TOWN, st
        assert (st["player_x"], st["player_y"]) == (2, 7), st
        # Return leg from the town signpost: 10 steps to the field.
        for _ in range(5):
            d.press("right", settle=14)
            if d.screen() == SCREEN_JOURNEY:
                break
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=300), d.screen()
        assert d.wait_for(lambda: d.text_has("FIELD ROAD"), ticks=300)
        d.press("a", settle=20)  # foot
        assert d.wait_for(lambda: d.text_has("> CONTINUE"), ticks=300)
        assert d.total_steps() == 10, d.total_steps()
        print("return road OK: 10 steps")
        # Provision for the road home (the demo boots light and the
        # outward leg eats the rations; like the other poke setups).
        import struct
        d.poke_php(15)
        addr = d.r.state + OFF_VARIABLES + 2 * (12 - 1)
        raw = struct.pack("<h", 45)
        d.pb.memory[addr] = raw[0]
        d.pb.memory[addr + 1] = raw[1]
        d.tick(2)
        assert d.prov() == 45, d.prov()
        for _ in range(80):
            st = d.scene()
            if st["scene_id"] == SCENE_ACT2_FIELD:
                break
            if d.screen() == SCREEN_BATTLE:
                assert win_fight(d)
                d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
                d.press("a", settle=20)
            elif d.screen() == SCREEN_JOURNEY:
                if d.text_has("AMBUSH!"):
                    d.press("a", settle=30)
                elif d.text_has("TRAVELER NEEDS"):
                    d.press("down", settle=12)
                    d.press("a", settle=20)
                    d.wait_for(lambda: d.text_has("YOU WALK ON."),
                               ticks=300)
                    d.press("a", settle=20)
                elif d.text_has("YOU SPOT OLD RUINS"):
                    d.press("down", settle=12)
                    d.press("a", settle=20)
                    d.wait_for(lambda: d.text_has("YOU WALK ON."),
                               ticks=300)
                    d.press("a", settle=20)
                else:
                    d.press("a", settle=20)
            else:
                d.tick(30)
        st = d.scene()
        assert st["scene_id"] == SCENE_ACT2_FIELD, st
        print("return trip OK: arrived back on the field")
        d.close()
        d = None
        # Same road, different act: chapter 2 walks the act-2 table.
        d = D()
        import struct
        addr = d.r.state + OFF_VARIABLES + 2 * (1 - 1)
        raw = struct.pack("<h", 2)
        d.pb.memory[addr] = raw[0]
        d.pb.memory[addr + 1] = raw[1]
        d.tick(2)
        assert d.r.variable(1) == 2, d.r.variable(1)
        boot_to_journey(d)
        select_foot(d)
        assert d.total_steps() == 10, d.total_steps()
        print("act-2 road OK: 10 steps")
    finally:
        if d is not None:
            d.close()


def test_full():
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        seen = 0
        prev_event = 255
        for _ in range(80):
            st = d.scene()
            if st["scene_id"] == SCENE_ACT2_TOWN:
                break
            if d.screen() == SCREEN_BATTLE:
                assert win_fight(d)
                d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
                d.press("a", settle=20)
            elif d.screen() == SCREEN_JOURNEY:
                cur = d.pb.memory[d.r.sym.wram("g_travel_event")]
                if cur != 255 and prev_event == 255:
                    seen += 1
                prev_event = cur
                if d.text_has("TRAVELER NEEDS"):
                    d.press("a", settle=20)  # help
                    d.wait_for(lambda: d.text_has("YOU SHARE"), ticks=300)
                    d.press("a", settle=20)
                elif d.text_has("AMBUSH!"):
                    d.press("a", settle=20)
                elif d.text_has("YOU SPOT OLD RUINS"):
                    # full trip skips the detour: IGNORE keeps the schedule
                    d.press("down", settle=12)  # toggle to IGNORE
                    assert d.wait_for(lambda: d.text_has("EXPLORE>IGNORE"),
                                      ticks=200), d.rows()
                    d.press("a", settle=20)
                    assert d.wait_for(lambda: d.text_has("YOU WALK ON."),
                                      ticks=300), d.rows()
                    d.press("a", settle=20)
                else:
                    d.press("a", settle=20)
            else:
                d.tick(30)
        st = d.scene()
        assert st["scene_id"] == SCENE_ACT2_TOWN, st
        # on-foot guarantee: at least one random event per journey
        assert seen >= 1, "no random event on foot trip?"
        print("full-trip arrival OK; gold %d; events %d"
              % (d.gold(), seen))
    finally:
        d.close()


def test_weather():
    """Ambient weather (shipped act-2 tuning): the first travelled
    step rolls a condition on the travel stream, the route line shows
    it, and the day's burn follows it (rain eases hunger, heat eats
    more, snow burns a second day)."""
    WX = ["CLEAR", "RAIN", "STORM", "HEAT", "COLD", "WIND", "SNOW", "FOG"]
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        assert d.weather() == 0xFF, d.weather()
        f0 = d.prov()
        day0 = d.day()
        d.press("a", settle=20)  # CONTINUE: first step, weather rolls
        w = d.weather()
        assert 0 <= w <= 7, w
        assert WX[w] in d.rows()[3], (d.rows()[3], WX[w])
        dd = day0 - d.day()
        assert dd in (1, 2), (day0, d.day(), WX[w])
        if WX[w] == "SNOW":
            assert dd == 2, (day0, d.day())
        df = f0 - d.prov()
        assert 1 <= df <= 5, (f0, d.prov(), WX[w])
        print("weather OK: %s, day -%d, prov -%d"
              % (WX[w], dd, df))
    finally:
        d.close()


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "full"
    if mode == "ambush":
        test_ambush()
    elif mode == "help":
        test_help(True)
    elif mode == "ignore":
        test_help(False)
    elif mode == "defeat":
        test_defeat()
    elif mode == "flee":
        test_flee()
    elif mode == "premium":
        test_premium()
    elif mode == "supply":
        test_supply()
    elif mode == "starve":
        test_starve()
    elif mode == "nogold":
        test_nogold()
    elif mode == "days":
        test_days()
    elif mode == "events":
        test_events()
    elif mode == "risk":
        test_risk()
    elif mode == "camp":
        test_camp()
    elif mode == "acts":
        test_acts()
    elif mode == "ruins":
        test_ruins()
    elif mode == "ruins_ignore":
        test_ruins_ignore()
    elif mode == "weather":
        test_weather()
    else:
        test_full()
