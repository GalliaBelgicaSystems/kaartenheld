"""Headless verification driver for the travel-demo ROM.

Usage: python3 tools/verify_travel_demo.py [MODE] (from repo root,
inside `nix develop` for PyBoy)
  premium: carriage instant travel (-50G, no days, no events)
  nogold:  refused carriage (gold/days untouched)
  supply:  field merchant food/water shop + leave
  starve:  empty belly forces ambush even at frequency 0
  ambush:  route pool forced to [ambush_slimes] @100% (rebuild ROM first)
  help:    route pool forced to [traveler_help] @100% (rebuild ROM first)
  ignore:  same tuning; ignore keeps the schedule
  defeat:  lose on purpose (HP floor 1, +1 step, resume)
  flee:    flee resumes the journey
  days:    day burn, zero crossing, late-buffed slime
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
                          OFF_CURRENCY)

ROM = "build/kaartenheld-travel-demo.gb"
SCREEN_OVERWORLD = 0
SCREEN_BATTLE = 2
SCREEN_JOURNEY = 16
SCENE_FIELD = 0
SCENE_VILLAGE = 17


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

    def lord(self):
        return self.r.variable(11)

    def food(self):
        return self.r.variable(12)

    def water(self):
        return self.r.variable(13)

    def poke_day(self, v):
        import struct
        addr = self.r.state + OFF_VARIABLES + 2 * (10 - 1)
        raw = struct.pack("<h", v)
        self.pb.memory[addr] = raw[0]
        self.pb.memory[addr + 1] = raw[1]
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


def boot_to_journey(d):
    st = d.scene()
    assert d.screen() == SCREEN_OVERWORLD, "boot screen %d" % d.screen()
    assert st["scene_id"] == SCENE_FIELD, st
    assert (st["player_x"], st["player_y"]) == (5, 7), st
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
    assert d.wait_for(lambda: d.text_has("STEP 0 OF 5"), ticks=300), d.rows()
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
        # walk the rest of the way (more ambushes may fire: fight them)
        for _ in range(30):
            st = d.scene()
            if st["scene_id"] == SCENE_VILLAGE:
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
        assert st["scene_id"] == SCENE_VILLAGE, st
        assert (st["player_x"], st["player_y"]) == (10, 9), st
        assert d.gold() == gold0 + 8 * wins, "gold %d, wins %d" \
            % (d.gold(), wins)
        print("arrival village OK after %d ambush wins; final gold %d"
              % (wins, d.gold()))
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
        if not help_it:
            d.press("down", settle=12)  # toggle to IGNORE
            assert d.wait_for(lambda: d.text_has(">IGNORE"), ticks=200), \
                d.rows()
        else:
            assert d.text_has("> HELP"), d.rows()
        d.press("a", settle=30)  # confirm
        if help_it:
            assert d.wait_for(lambda: d.text_has("YOU SHARE"), ticks=300), \
                d.rows()
            assert d.text_has("+15G +1 STEP"), d.rows()
            assert d.gold() == gold0 + 15, "gold %d" % d.gold()
            d.press("a", settle=20)  # dismiss
            assert d.text_has("STEP 1 OF 6"), d.rows()
            print("help OK: +15G, detour to 6 steps")
        else:
            assert d.wait_for(lambda: d.text_has("YOU WALK ON"), ticks=300), \
                d.rows()
            assert d.gold() == gold0, "gold changed %d" % d.gold()
            d.press("a", settle=20)
            assert d.text_has("STEP 1 OF 5"), d.rows()
            print("ignore OK: no reward, still 5 steps (faster)")
        # finish the trip quietly
        for _ in range(30):
            st = d.scene()
            if st["scene_id"] == SCENE_VILLAGE:
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
        assert st["scene_id"] == SCENE_VILLAGE, st
        print("arrival village OK")
    finally:
        d.close()


def test_defeat():
    """Lose the ambush on purpose: empty turns until DEFEATED, then the
    journey resumes with HP floored at 1 and a recovery step."""
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
        d.press("a", settle=30)  # fight
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE,
                          ticks=600), "no battle"
        # stage nothing, execute empty turns until the slime wins
        # (~2 dmg/round vs 15 HP: needs ~8 rounds, ~6 loop iters each)
        won = False
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
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=600), "no journey resume after defeat"
        assert d.text_has("YOU WAKE UP HURT"), d.rows()
        assert d.r.hero()["hp"] == 1, d.r.hero()
        assert d.day() == 9, d.day()  # step day + recovery day burned
        assert d.gold() == gold0, "gold changed on defeat"
        print("defeat OK: HP floored to 1, journey resumes, no gold")
        d.press("a", settle=20)  # dismiss
        assert d.text_has("STEP"), d.rows()
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
    supreme (buffed late ambushes + LATE! row)."""
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        assert d.day() == 180, d.day()
        assert d.text_has("DAYS 180"), d.rows()
        d.press("a", settle=20)  # advance: step + day
        assert d.day() == 179, d.day()
        assert d.text_has("AMBUSH!"), d.rows()
        d.press("a", settle=30)  # fight
        assert d.wait_for(lambda: d.screen() == SCREEN_BATTLE, ticks=600)
        assert win_fight(d)
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
        assert d.day() == 178, d.day()  # victory burned victory_days=1
        assert d.text_has("+8G LOOT!")
        print("day burn OK: step -1, victory -1 (180 -> 178)")
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
        assert d.wait_for(lambda: d.scene()["scene_id"] == SCENE_VILLAGE,
                          ticks=600), d.scene()
        st = d.scene()
        assert (st["player_x"], st["player_y"]) == (10, 9), st
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
    """Field merchant: walk over, open the supply shop, buy food+water
    with gold, leave back to the field unmoved."""
    d = D()
    try:
        st = d.scene()
        assert d.screen() == SCREEN_OVERWORLD, d.screen()
        assert (st["player_x"], st["player_y"]) == (5, 7), st
        for b in ("down", "right", "right"):
            d.press(b, settle=20)
        st = d.scene()
        assert (st["player_x"], st["player_y"]) == (7, 8), st
        d.press("down", settle=12)  # bump the merchant: talk = supply opens
        assert d.wait_for(lambda: d.screen() == SCREEN_JOURNEY,
                          ticks=300), d.screen()
        assert d.wait_for(lambda: d.text_has("FOOD"), ticks=300), d.rows()
        assert d.text_has("WATER"), d.rows()
        assert d.text_has("LEAVE"), d.rows()
        assert d.food() == 6 and d.water() == 12, (d.food(), d.water())
        d.press("a", settle=20)  # buy FOOD (2G)
        assert d.gold() == 18, d.gold()
        assert d.food() == 7, d.food()
        d.press("down", settle=12)  # select WATER
        d.press("a", settle=20)  # buy WATER (1G)
        assert d.gold() == 17, d.gold()
        assert d.water() == 13, d.water()
        d.press("down", settle=12)  # select LEAVE
        assert d.wait_for(lambda: d.text_has("> LEAVE"), ticks=200), d.rows()
        d.press("a", settle=20)  # leave
        assert d.wait_for(lambda: d.screen() == SCREEN_OVERWORLD,
                          ticks=300), d.screen()
        st = d.scene()
        assert (st["player_x"], st["player_y"]) == (7, 8), st
        print("supply OK: bought food+water, left unmoved")
    finally:
        d.close()


def test_starve():
    """Starving (no food) forces an ambush even at frequency 0."""
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        assert d.day() == 180, d.day()
        d.poke_day(170)
        # drain food through the state address (variable 12)
        import struct
        addr = d.r.state + OFF_VARIABLES + 2 * (12 - 1)
        raw = struct.pack("<h", 0)
        d.pb.memory[addr] = raw[0]
        d.pb.memory[addr + 1] = raw[1]
        d.tick(2)
        assert d.food() == 0, d.food()
        d.press("a", settle=20)  # advance: quiet roll is 0, hunger forces it
        assert d.wait_for(lambda: d.text_has("AMBUSH!"), ticks=300), d.rows()
        print("starve OK: 100% ambush with empty belly")
    finally:
        d.close()


def test_full():
    d = D()
    try:
        boot_to_journey(d)
        select_foot(d)
        for _ in range(40):
            st = d.scene()
            if st["scene_id"] == SCENE_VILLAGE:
                break
            if d.screen() == SCREEN_BATTLE:
                assert win_fight(d)
                d.wait_for(lambda: d.screen() == SCREEN_JOURNEY, ticks=600)
                d.press("a", settle=20)
            elif d.screen() == SCREEN_JOURNEY:
                if d.text_has("TRAVELER NEEDS"):
                    d.press("a", settle=20)  # help
                    d.wait_for(lambda: d.text_has("YOU SHARE"), ticks=300)
                    d.press("a", settle=20)
                elif d.text_has("AMBUSH!"):
                    d.press("a", settle=20)
                else:
                    d.press("a", settle=20)
            else:
                d.tick(30)
        st = d.scene()
        assert st["scene_id"] == SCENE_VILLAGE, st
        print("full-trip arrival OK; gold %d" % d.gold())
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
    if mode == "defeat":
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
    else:
        test_full()
