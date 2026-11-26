#!/usr/bin/env python3
"""
journey_compile.py - Travel content JSON to C for the travel-demo ROM.

journeys/routes/*.json + journeys/events/*.json
    -> src/game/travel_content.c
       (g_travel_routes[] + g_travel_events[] + counts, ROM bank 9)

The travel-demo ROM links this; the main ROMs exclude it via the
TRAVEL_SRCS Makefile filter, so journey content can never affect the
main game or its memory budget.

Route schema:
    id, name (<=20 chars), start (scene in levels/registry.json),
    destination (scene), dest_x/dest_y (inside the level bounds),
    act (1..9: the act this road belongs to),
    supply (provisions_price + provisions_per_day burn),
    camp (hunt provisions min..max + delay_min..max + jackpot
    min..max at jackpot_chance %, rest heal + delay, starve
    provisions hp + every, study save/lost min..max + provisions),
    weather (optional per-weather weight 0..255 + days_min/max
    1..10 + extra provisions -10..10 per day + hunt jackpot-chance
    delta -100..+100; each road tunes its own climate, so a desert
    road hunts worse than a green one, weights sum <= 255),
    methods (1..2 entries: paid instant or foot slog with its own
    event pool; every pool event must list the road act in its
    own acts tag, and so must every opt battle it stages).

Event schemas (kind):
    battle: text (<=20), battle (BATTLE_* in src/world/actor.h).
    choice: text (<=20 prompt), opt1/opt2 (<=8 option labels),
            opt1_effects/opt2_effects objects: text (<=20 result),
            risk (ALWAYS 50: every scenario shares the same
            success/failure odds), optional battle (battle event id:
            that option fights), or numeric effects (gold
            -30000..30000, provisions -30..30, heal/hurt 0..30,
            delay/save/days 0..10), plus a fail object with text
            (<=20 fail line), optional fail battle, and fail_*
            numeric overrides (absent key = 0).  Optional wx object
            per option maps a weather name to hurt/delay/save/days/
            provisions deltas (-10..10) applied when that weather is
            active (e.g. rain makes a ford hurt more, fog eats a
            shortcut's save); battle options only support hurt.
            Optional acts (default: every act 1..9).
    ruins: text (<=20, prompt), explore_text (<=20, enter line),
            ignore_text (<=20).

Usage:
    python3 tools/screen_compiler/journey_compile.py --all -o src/game/travel_content.c
    python3 tools/screen_compiler/journey_compile.py --all --check
"""
import argparse
import json
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent

ROUTES_DIR = REPO_ROOT / "journeys" / "routes"
EVENTS_DIR = REPO_ROOT / "journeys" / "events"
REGISTRY = REPO_ROOT / "levels" / "registry.json"
ACTOR_H = REPO_ROOT / "src" / "world" / "actor.h"
LEVELS_DIR = REPO_ROOT / "levels"

OUT_DEFAULT = REPO_ROOT / "src" / "game" / "travel_content.c"
OUT_IDS_NAME = "travel_routes_generated.h"

TRAVEL_POOL_MAX = 64
TRAVEL_TEXT_LEN = 20
TRAVEL_NAME_LEN = 11
TRAVEL_OPT_LEN = 8
TRAVEL_METHODS_MAX = 2
TRAVEL_BANK = 14

# Fixed weather vocabulary (ids match travel.h TRAVEL_WEATHER_*).
WEATHER_NAMES = ["clear", "rain", "storm", "heat",
                 "cold", "wind", "snow", "fog"]
WEATHER_ID = {n: i for i, n in enumerate(WEATHER_NAMES)}

WX_MOD_KEYS = ("hurt", "delay", "save", "days", "provisions")


def fail(msg):
    raise ValueError(msg)


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def load_battle_ids():
    text = ACTOR_H.read_text(encoding="utf-8")
    ids = {}
    for m in re.finditer(r"\b(BATTLE_[A-Z0-9_]+)\s*=\s*(\d+)", text):
        ids[m.group(1)] = int(m.group(2))
    if "BATTLE_SLIME" not in ids:
        fail("actor.h: BATTLE_* ids not found (header shape changed?)")
    return ids


def load_registry():
    reg = json.loads(REGISTRY.read_text(encoding="utf-8"))
    return reg.get("scenes", {})


def load_level_size(scene_id):
    for path in LEVELS_DIR.glob("*.json"):
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        if doc.get("id") == scene_id:
            m = doc.get("map", {})
            return (int(m["width"]), int(m["height"]))
    fail("level JSON for scene '%s' not found" % scene_id)


def check_text(field, value, owner):
    if not isinstance(value, str):
        fail("%s: field '%s' must be a string" % (owner, field))
    if len(value) > TRAVEL_TEXT_LEN:
        fail("%s: field '%s' is %d chars (max %d): %r"
             % (owner, field, len(value), TRAVEL_TEXT_LEN, value))
    if not re.fullmatch(r"[A-Z0-9 .!'+?,:\-/]*", value):
        fail("%s: field '%s' has non-staging chars (A-Z0-9 .!'+?,:-/): %r"
             % (owner, field, value))


def check_acts(doc, eid):
    """Acts tag: which acts an event may appear on. Absent means every
    act (all-acts default keeps old files valid); otherwise a non-empty
    list of 1..9. Routes admit only pool events listing their act."""
    acts = doc.get("acts", None)
    if acts is None:
        return list(range(1, 10))
    if not isinstance(acts, list) or not acts:
        fail("%s: acts must be a non-empty list of 1..9" % eid)
    for a in acts:
        if not isinstance(a, int) or not (1 <= a <= 9):
            fail("%s: acts entries must be 1..9" % eid)
    return sorted(set(acts))


def load_events(battle_ids):
    events = []
    seen = set()
    for path in sorted(EVENTS_DIR.glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        eid = doc.get("id")
        if not eid or eid in seen:
            fail("event %s: missing or duplicate id" % path.name)
        seen.add(eid)
        kind = doc.get("kind")
        if kind == "battle":
            check_text("text", doc.get("text"), eid)
            b = doc.get("battle")
            if b not in battle_ids:
                fail("%s: unknown battle '%s'" % (eid, b))
            check_text("enemy_name", doc.get("enemy_name"), eid)
            if len(doc["enemy_name"]) > 11:
                fail("%s: enemy_name is %d chars (banner cap 11): %r"
                     % (eid, len(doc["enemy_name"]), doc["enemy_name"]))
            hp = doc.get("enemy_hp")
            if not isinstance(hp, int) or not (1 <= hp <= 255):
                fail("%s: enemy_hp must be 1..255" % eid)
            gold = doc.get("victory_gold")
            if not isinstance(gold, int) or not (0 <= gold <= 30000):
                fail("%s: victory_gold must be 0..30000" % eid)
            days = doc.get("victory_days")
            if not isinstance(days, int) or not (0 <= days <= 255):
                fail("%s: victory_days must be 0..255" % eid)
            late = doc.get("late_enemy_hp")
            if not isinstance(late, int) or not (1 <= late <= 255):
                fail("%s: late_enemy_hp must be 1..255" % eid)
            events.append({"id": eid, "kind": "battle", "battle": b,
                           "text": doc["text"], "enemy_name": doc["enemy_name"],
                           "enemy_hp": hp, "victory_gold": gold,
                           "victory_days": days, "late_hp": late,
                           "acts": check_acts(doc, eid)})
        elif kind == "choice":
            check_text("text", doc.get("text"), eid)
            for opt in ("opt1", "opt2"):
                label = doc.get(opt)
                if not isinstance(label, str) or not (1 <= len(label) <= TRAVEL_OPT_LEN):
                    fail("%s: %s must be 1..%d chars" % (eid, opt, TRAVEL_OPT_LEN))
                check_text(opt, label, eid)
                opt_doc = doc.get(opt + "_effects")
                if opt_doc is None:
                    opt_doc = {}
                if not isinstance(opt_doc, dict):
                    fail("%s: %s_effects must be an object" % (eid, opt))
                check_text(opt + "_text", opt_doc.get("text"), eid)
                risk = opt_doc.get("risk")
                if risk != 50:
                    fail("%s: %s_effects.risk must be 50 (every scenario "
                         "shares the same success/failure odds)" % (eid, opt))
                battle_ref = opt_doc.get("battle", None)
                if battle_ref is not None:
                    # A fight may only combine with ambush hurt (struck
                    # on the way in); everything else belongs to the
                    # non-fight path.
                    for key in ("gold", "provisions", "heal",
                                "delay", "save", "days"):
                        if key in opt_doc:
                            fail("%s: %s_effects mixes battle with '%s'"
                                 % (eid, opt, key))
                    v = opt_doc.get("hurt", 0)
                    if not isinstance(v, int) or not (0 <= v <= 30):
                        fail("%s: %s_effects.hurt must be 0..30" % (eid, opt))
                else:
                    for key, lo, hi in (("gold", -30000, 30000),
                                        ("provisions", -30, 30),
                                        ("heal", 0, 30),
                                        ("hurt", 0, 30),
                                        ("delay", 0, 10),
                                        ("save", 0, 10),
                                        ("days", 0, 10)):
                        v = opt_doc.get(key, 0)
                        if not isinstance(v, int) or not (lo <= v <= hi):
                            fail("%s: %s_effects.%s must be %d..%d"
                                 % (eid, opt, key, lo, hi))
                wx = opt_doc.get("wx", None)
                if wx is not None:
                    if not isinstance(wx, dict) or not wx:
                        fail("%s: %s_effects.wx must be a non-empty "
                             "object keyed by weather name" % (eid, opt))
                    for k, mod in wx.items():
                        if k not in WEATHER_ID:
                            fail("%s: %s_effects.wx unknown weather '%s'"
                                 % (eid, opt, k))
                        if not isinstance(mod, dict) or not mod:
                            fail("%s: %s_effects.wx.%s must be a "
                                 "non-empty object" % (eid, opt, k))
                        for dk, dv in mod.items():
                            if battle_ref is not None and dk != "hurt":
                                fail("%s: %s_effects.wx.%s only supports "
                                     "'hurt' with a battle (got '%s')"
                                     % (eid, opt, k, dk))
                            if dk not in WX_MOD_KEYS:
                                fail("%s: %s_effects.wx.%s unknown delta "
                                     "'%s'" % (eid, opt, k, dk))
                            if not isinstance(dv, int) or \
                                    not (-10 <= dv <= 10):
                                fail("%s: %s_effects.wx.%s.%s must be "
                                     "-10..10" % (eid, opt, k, dk))
                fail_doc = opt_doc.get("fail", None)
                if not isinstance(fail_doc, dict):
                    fail("%s: %s_effects.fail must be an object with "
                         "text + effects" % (eid, opt))
                check_text(opt + "_fail_text", fail_doc.get("text"), eid)
                fbattle = fail_doc.get("battle", None)
                if fbattle is not None:
                    for key in ("gold", "provisions", "heal",
                                "hurt", "delay", "save", "days"):
                        if key in fail_doc and key != "hurt":
                            fail("%s: %s_effects.fail mixes battle with "
                                 "'%s'" % (eid, opt, key))
                    v = fail_doc.get("hurt", 0)
                    if not isinstance(v, int) or not (0 <= v <= 30):
                        fail("%s: %s_effects.fail.hurt must be 0..30"
                             % (eid, opt))
                else:
                    for key, lo, hi in (("gold", -30000, 30000),
                                        ("provisions", -30, 30),
                                        ("heal", 0, 30),
                                        ("hurt", 0, 30),
                                        ("delay", 0, 10),
                                        ("save", 0, 10),
                                        ("days", 0, 10)):
                        v = fail_doc.get(key, 0)
                        if not isinstance(v, int) or not (lo <= v <= hi):
                            fail("%s: %s_effects.fail.%s must be %d..%d"
                                 % (eid, opt, key, lo, hi))
            events.append({"id": eid, "kind": "choice",
                           "text": doc["text"], "doc": doc,
                           "acts": check_acts(doc, eid)})
        elif kind == "ruins":
            check_text("text", doc.get("text"), eid)
            check_text("explore_text", doc.get("explore_text"), eid)
            check_text("ignore_text", doc.get("ignore_text"), eid)
            events.append({"id": eid, "kind": "ruins",
                           "text": doc["text"],
                           "help_text": doc["explore_text"],
                           "ignore_text": doc["ignore_text"],
                           "gold": 0, "delay": 0, "ignore": 0,
                           "acts": check_acts(doc, eid)})
        else:
            fail("%s: kind must be 'battle', 'choice' or 'ruins'" % eid)
    if not events:
        fail("no events in journeys/events/")
    if len(events) >= 255:
        fail("too many events (%d, max 254: 255 is TRAVEL_EVENT_NONE)"
             % len(events))
    return events


def load_routes(scenes, event_ids, event_acts):
    routes = []
    seen = set()
    for path in sorted(ROUTES_DIR.glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        rid = doc.get("id")
        if not rid or rid in seen:
            fail("route %s: missing or duplicate id" % path.name)
        seen.add(rid)
        check_text("name", doc.get("name"), rid)
        for key in ("start", "destination"):
            if doc.get(key) not in scenes:
                fail("%s: unknown scene '%s' (%s not in registry.json)"
                     % (rid, doc.get(key), key))
        w, h = load_level_size(doc["destination"])
        for key in ("dest_x", "dest_y"):
            if not isinstance(doc.get(key), int):
                fail("%s: %s must be an integer" % (rid, key))
        if not (0 <= doc["dest_x"] < w and 0 <= doc["dest_y"] < h):
            fail("%s: dest (%d,%d) outside %dx%d level"
                 % (rid, doc["dest_x"], doc["dest_y"], w, h))
        supply = doc.get("supply")
        if not isinstance(supply, dict):
            fail("%s: supply must be an object with provisions_price"
                 % rid)
        for key in ("provisions_price", "provisions_per_day"):
            v = supply.get(key)
            if not isinstance(v, int) or not (0 <= v <= 255):
                fail("%s: supply.%s must be 0..255" % (rid, key))
        camp = doc.get("camp")
        if not isinstance(camp, dict):
            fail("%s: camp needs hunt/rest/starve/study" % rid)
        hunt = camp.get("hunt")
        if not isinstance(hunt, dict):
            fail("%s: camp.hunt must be an object" % rid)
        for key, lo, hi in (("provisions_min", 0, 30), ("provisions_max", 0, 30),
                             ("delay_min", 0, 10), ("delay_max", 0, 10),
                             ("jackpot_min", 0, 60), ("jackpot_max", 0, 60),
                             ("jackpot_chance", 0, 100)):
            v = hunt.get(key)
            if not isinstance(v, int) or not (lo <= v <= hi):
                fail("%s: camp.hunt.%s must be %d..%d" % (rid, key, lo, hi))
        if hunt["provisions_max"] < hunt["provisions_min"]:
            fail("%s: camp.hunt.provisions_max < provisions_min" % rid)
        if hunt["jackpot_max"] < hunt["jackpot_min"]:
            fail("%s: camp.hunt.jackpot_max < jackpot_min" % rid)
        if hunt["delay_max"] < hunt["delay_min"]:
            fail("%s: camp.hunt.delay_max < delay_min" % rid)
        rest = camp.get("rest")
        if not isinstance(rest, dict):
            fail("%s: camp.rest must be an object" % rid)
        for key, lo, hi in (("heal", 0, 30), ("delay", 0, 10)):
            v = rest.get(key)
            if not isinstance(v, int) or not (lo <= v <= hi):
                fail("%s: camp.rest.%s must be %d..%d" % (rid, key, lo, hi))
        starve = camp.get("starve")
        if not isinstance(starve, dict):
            fail("%s: camp.starve must be an object" % rid)
        for key, lo, hi in (("provisions_hp", 0, 30), ("provisions_every", 1, 10)):
            v = starve.get(key)
            if not isinstance(v, int) or not (lo <= v <= hi):
                fail("%s: camp.starve.%s must be %d..%d"
                     % (rid, key, lo, hi))
        study = camp.get("study")
        if not isinstance(study, dict):
            fail("%s: camp.study must be an object" % rid)
        for key in ("save_min", "save_max"):
            v = study.get(key)
            if not isinstance(v, int) or not (0 <= v <= 20):
                fail("%s: camp.study.%s must be 0..20" % (rid, key))
        if study["save_max"] < study["save_min"]:
            fail("%s: camp.study.save_max < save_min" % rid)
        for key in ("lost_min", "lost_max"):
            v = study.get(key)
            if not isinstance(v, int) or not (0 <= v <= 10):
                fail("%s: camp.study.%s must be 0..10" % (rid, key))
        if study["lost_max"] < study["lost_min"]:
            fail("%s: camp.study.lost_max < lost_min" % rid)
        for key in ("provisions",):
            v = study.get(key)
            if not isinstance(v, int) or not (0 <= v <= 30):
                fail("%s: camp.study.%s must be 0..30" % (rid, key))
        # Ambient weather table (optional; absent = no weather system
        # on this road).  Per-weather roll weight + duration range +
        # extra rations/water burned per day.  Weights differ per road,
        # so each act gets its own climate (desert act drinks more,
        # northern act snows).
        wx = doc.get("weather", None)
        wxrow = {}
        if wx is None:
            wxrow = {n: {"weight": 0, "days_min": 1, "days_max": 1,
                         "provisions": 0} for n in WEATHER_NAMES}
        else:
            if not isinstance(wx, dict):
                fail("%s: weather must be an object keyed by weather "
                     "name" % rid)
            for k in wx.keys():
                if k not in WEATHER_ID:
                    fail("%s: weather unknown weather '%s'" % (rid, k))
            for n in WEATHER_NAMES:
                entry = wx.get(n, {})
                if not isinstance(entry, dict):
                    fail("%s: weather.%s must be an object" % (rid, n))
                wgt = entry.get("weight", 0)
                if not isinstance(wgt, int) or not (0 <= wgt <= 255):
                    fail("%s: weather.%s.weight must be 0..255"
                         % (rid, n))
                for key in ("days_min", "days_max"):
                    v = entry.get(key, 1)
                    if not isinstance(v, int) or not (1 <= v <= 10):
                        fail("%s: weather.%s.%s must be 1..10"
                             % (rid, n, key))
                if entry.get("days_max", 1) < entry.get("days_min", 1):
                    fail("%s: weather.%s.days_max < days_min" % (rid, n))
                for key in ("provisions",):
                    v = entry.get(key, 0)
                    if not isinstance(v, int) or not (-10 <= v <= 10):
                        fail("%s: weather.%s.%s must be -10..10"
                             % (rid, n, key))
                for key in ("hunt",):
                    v = entry.get(key, 0)
                    if not isinstance(v, int) or not (-100 <= v <= 100):
                        fail("%s: weather.%s.%s must be -100..100"
                             % (rid, n, key))
                wxrow[n] = {"weight": wgt,
                            "days_min": entry.get("days_min", 1),
                            "days_max": entry.get("days_max", 1),
                            "provisions": entry.get("provisions", 0),
                            "hunt": entry.get("hunt", 0)}
            wsum = sum(wxrow[n]["weight"] for n in WEATHER_NAMES)
            if wsum > 255:
                fail("%s: weather weights sum to %d (max 255, fixed-bank "
                     "rejection sampling)" % (rid, wsum))
        doc["weather_row"] = wxrow
        act = doc.get("act")
        if not isinstance(act, int) or not (1 <= act <= 9):
            fail("%s: act must be 1..9 (the act this road belongs to)"
                 % rid)
        doc["act"] = act
        methods = doc.get("methods")
        if not isinstance(methods, list) or \
                not (1 <= len(methods) <= TRAVEL_METHODS_MAX):
            fail("%s: methods must list 1..%d entries"
                 % (rid, TRAVEL_METHODS_MAX))
        mseen = set()
        for m in methods:
            mid = m.get("id")
            if not mid or mid in mseen:
                fail("%s: method missing or duplicate id" % rid)
            mseen.add(mid)
            if not isinstance(m.get("name"), str) or \
                    len(m["name"]) > TRAVEL_NAME_LEN:
                fail("%s/%s: method name max %d chars"
                     % (rid, mid, TRAVEL_NAME_LEN))
            check_text("method name", m.get("name"), rid + "/" + mid)
            cost = m.get("cost_gold")
            if not isinstance(cost, int) or not (0 <= cost <= 30000):
                fail("%s/%s: cost_gold must be 0..30000" % (rid, mid))
            for key, lo, hi in (("steps", 0, 255), ("frequency", 0, 255)):
                v = m.get(key)
                if not isinstance(v, int) or not (lo <= v <= hi):
                    fail("%s/%s: %s must be %d..%d"
                         % (rid, mid, key, lo, hi))
            pool = m.get("pool")
            if not isinstance(pool, list) or len(pool) > TRAVEL_POOL_MAX:
                fail("%s/%s: pool must list 0..%d entries"
                     % (rid, mid, TRAVEL_POOL_MAX))
            if m["frequency"] > 0 and not pool:
                fail("%s/%s: frequency>0 needs a non-empty pool" % (rid, mid))
            if m["steps"] == 0 and (pool or m["frequency"] > 0):
                fail("%s/%s: instant (0 steps) methods run no events"
                     % (rid, mid))
            wsum = 0
            for entry in pool:
                if entry.get("event") not in event_ids:
                    fail("%s/%s: unknown event '%s' in pool"
                         % (rid, mid, entry.get("event")))
                if doc["act"] not in event_acts[entry["event"]]:
                    fail("%s/%s: event '%s' is acts %s, road is act %d"
                         % (rid, mid, entry["event"],
                            event_acts[entry["event"]], doc["act"]))
                wgt = entry.get("weight")
                if not isinstance(wgt, int) or not (1 <= wgt <= 255):
                    fail("%s/%s: pool weight must be 1..255" % (rid, mid))
                wsum += wgt
            if wsum > 255:
                fail("%s/%s: pool weights sum to %d (max 255, "
                     "fixed-bank rejection sampling)" % (rid, mid, wsum))
        routes.append(doc)
    if not routes:
        fail("no routes in journeys/routes/")
    if len(routes) > 255:
        fail("too many routes (%d, max 255)" % len(routes))
    return routes


def emit(routes, events, scenes, battle_ids):
    event_index = {e["id"]: i for i, e in enumerate(events)}
    lines = []
    lines.append("/**")
    lines.append(" * Generated by journey_compile.py.")
    lines.append(" * Do not edit directly -- edit the journeys JSON and re-run.")
    lines.append(" * Linked into the travel-demo ROM only (TRAVEL_SRCS).")
    lines.append(" */")
    lines.append("")
    lines.append('#include "travel.h"')
    lines.append('#include "banked.h"')
    lines.append("")
    lines.append("#pragma bank %d" % TRAVEL_BANK)
    lines.append("")
    lines.append("const TravelRoute g_travel_routes[] = {")
    for doc in routes:
        lines.append("    { /* %s */" % doc["id"])
        lines.append("        %s, /* name */" % c_str(doc["name"]))
        lines.append("        %d, /* dest_scene (%s) */"
                     % (scenes[doc["destination"]], doc["destination"]))
        lines.append("        %d, /* act (road act) */" % doc["act"])
        lines.append("        %d, %d, /* dest_x, dest_y */"
                     % (doc["dest_x"], doc["dest_y"]))
        lines.append("        %d, /* provisions_price */" % doc["supply"]["provisions_price"])
        lines.append("        %d, /* provisions_per_day */" % doc["supply"]["provisions_per_day"])
        lines.append("        %d, /* hunt_prov_min */" % doc["camp"]["hunt"]["provisions_min"])
        lines.append("        %d, /* hunt_prov_max */" % doc["camp"]["hunt"]["provisions_max"])
        lines.append("        %d, /* hunt_jackpot_min */" % doc["camp"]["hunt"]["jackpot_min"])
        lines.append("        %d, /* hunt_jackpot_max */" % doc["camp"]["hunt"]["jackpot_max"])
        lines.append("        %d, /* hunt_jackpot_chance */" % doc["camp"]["hunt"]["jackpot_chance"])
        lines.append("        %d, /* starve_prov_hp */" % doc["camp"]["starve"]["provisions_hp"])
        lines.append("        %d, /* starve_prov_every */" % doc["camp"]["starve"]["provisions_every"])
        lines.append("        %d, /* hunt_delay_min */" % doc["camp"]["hunt"]["delay_min"])
        lines.append("        %d, /* hunt_delay_max */" % doc["camp"]["hunt"]["delay_max"])
        lines.append("        %d, /* rest_heal */" % doc["camp"]["rest"]["heal"])
        lines.append("        %d, /* rest_delay */" % doc["camp"]["rest"]["delay"])
        lines.append("        %d, /* study_save_min */" % doc["camp"]["study"]["save_min"])
        lines.append("        %d, /* study_save_max */" % doc["camp"]["study"]["save_max"])
        lines.append("        %d, /* study_lost_min */" % doc["camp"]["study"]["lost_min"])
        lines.append("        %d, /* study_lost_max */" % doc["camp"]["study"]["lost_max"])
        lines.append("        %d, /* study_prov */" % doc["camp"]["study"]["provisions"])
        wr = doc["weather_row"]
        lines.append("        { %s }, /* weather_weight (clear..fog) */"
                     % ", ".join(str(wr[n]["weight"]) for n in WEATHER_NAMES))
        lines.append("        { %s }, /* weather_days_min */"
                     % ", ".join(str(wr[n]["days_min"]) for n in WEATHER_NAMES))
        lines.append("        { %s }, /* weather_days_max */"
                     % ", ".join(str(wr[n]["days_max"]) for n in WEATHER_NAMES))
        lines.append("        { %s }, /* weather_prov (extra/day) */"
                     % ", ".join(str(wr[n]["provisions"]) for n in WEATHER_NAMES))
        lines.append("        { %s }, /* weather_hunt (jackpot-chance delta) */"
                     % ", ".join(str(wr[n]["hunt"]) for n in WEATHER_NAMES))
        lines.append("        %d, /* method_count */" % len(doc["methods"]))
        lines.append("        {")
        for m in doc["methods"]:
            pool = m["pool"]
            ev = ", ".join(str(event_index[e["event"]]) for e in pool)
            wt = ", ".join(str(e["weight"]) for e in pool)
            pad = ["0"] * (TRAVEL_POOL_MAX - len(pool))
            ev += (", " + ", ".join(pad)) if pad else ""
            wt += (", " + ", ".join(pad)) if pad else ""
            if not pool:
                ev = ", ".join(pad)
                wt = ", ".join(pad)
            lines.append("            { /* %s */" % m["id"])
            lines.append("                %s, /* name */" % c_str(m["name"]))
            lines.append("                %d, /* cost_gold */" % m["cost_gold"])
            lines.append("                %d, /* steps (0 = instant) */"
                         % m["steps"])
            lines.append("                %d, /* frequency */"
                         % m["frequency"])
            lines.append("                %d, /* pool_count */" % len(pool))
            lines.append("                { %s }, /* pool_event */" % ev)
            lines.append("                { %s }, /* pool_weight */" % wt)
            lines.append("            },")
        for _ in range(TRAVEL_METHODS_MAX - len(doc["methods"])):
            lines.append('            { "", 0, 0, 0, 0, { 0, 0, 0, 0 },'
                         ' { 0, 0, 0, 0 } },')
        lines.append("        },")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t g_travel_route_count = %d;" % len(routes))
    lines.append("")
    lines.append("const TravelEvent g_travel_events[] = {")
    for e in events:
        lines.append("    { /* %s */" % e["id"])
        if e["kind"] == "battle":
            lines.append("        TRAVEL_EVENT_BATTLE,")
            lines.append("        %d, /* battle (%s) */"
                         % (battle_ids[e["battle"]], e["battle"]))
            lines.append("        %s, /* enemy_name */" % c_str(e["enemy_name"]))
            lines.append("        %d, /* enemy_hp */" % e["enemy_hp"])
            lines.append("        %d, /* victory_gold */" % e["victory_gold"])
            lines.append("        %d, /* victory_days */" % e["victory_days"])
            lines.append("        %d, /* late_enemy_hp */" % e["late_hp"])
            lines.append("        %s, /* text */" % c_str(e["text"]))
            lines.append('        "", /* text2 (unused) */')
            lines.append('        "", /* text3 (unused) */')
            lines.append('        "", /* opt1 (unused) */')
            lines.append('        "", /* opt2 (unused) */')
            lines.append("        255, 255, /* opt battles (none) */")
            lines.append("        0, 0, /* opt risk (unused) */")
            lines.append("        0, 0, /* opt gold */")
            lines.append("        0, 0, /* opt prov */")
            lines.append("        0, 0, /* opt heal */")
            lines.append("        0, 0, /* opt hurt */")
            lines.append("        0, 0, /* opt delay */")
            lines.append("        0, 0, /* opt save */")
            lines.append("        0, 0, /* opt days */")
        elif e["kind"] == "ruins":
            lines.append("        TRAVEL_EVENT_RUINS,")
            lines.append("        0, /* battle */")
            lines.append('        "", /* enemy_name (unused) */')
            lines.append("        0, /* enemy_hp */")
            lines.append("        0, /* victory_gold (unused) */")
            lines.append("        0, /* victory_days (unused) */")
            lines.append("        0, /* late_enemy_hp (unused) */")
            lines.append("        %s, /* text */" % c_str(e["text"]))
            lines.append("        %s, /* explore result */" % c_str(e["help_text"]))
            lines.append("        %s, /* ignore result */" % c_str(e["ignore_text"]))
            lines.append('        "", /* opt1 (unused) */')
            lines.append('        "", /* opt2 (unused) */')
            lines.append("        255, 255, /* opt battles (none) */")
            lines.append("        0, 0, /* opt risk (unused) */")
            lines.append("        0, 0, /* opt gold */")
            lines.append("        0, 0, /* opt prov */")
            lines.append("        0, 0, /* opt heal */")
            lines.append("        0, 0, /* opt hurt */")
            lines.append("        0, 0, /* opt delay */")
            lines.append("        0, 0, /* opt save */")
            lines.append("        0, 0, /* opt days */")
        else:
            lines.append("        TRAVEL_EVENT_CHOICE,")
            lines.append("        0, /* battle */")
            lines.append('        "", /* enemy_name (unused) */')
            lines.append("        0, /* enemy_hp */")
            lines.append("        0, /* victory_gold (unused) */")
            lines.append("        0, /* victory_days (unused) */")
            lines.append("        0, /* late_enemy_hp (unused) */")
            lines.append("        %s, /* text */" % c_str(e["text"]))
            doc = e["doc"]
            for opt in ("opt1", "opt2"):
                lines.append("        %s, /* %s result */"
                             % (c_str(doc[opt + "_effects"]["text"]), opt))
            for opt in ("opt1", "opt2"):
                lines.append("        %s, /* %s label */"
                             % (c_str(doc[opt]), opt))
            for opt in ("opt1", "opt2"):
                ref = doc[opt + "_effects"].get("battle", None)
                idx = event_index[ref] if ref is not None else 255
                lines.append("        %d, /* %s battle event or 255 */"
                             % (idx, opt))
            vals = [doc[opt + "_effects"]["risk"] * 256 // 100
                    for opt in ("opt1", "opt2")]
            lines.append("        %d, %d, /* opt risk (50/50 pre-scaled) */"
                         % (vals[0], vals[1]))
            for key in ("gold", "provisions", "heal", "hurt",
                        "delay", "save", "days"):
                vals = [doc[opt + "_effects"].get(key, 0)
                        for opt in ("opt1", "opt2")]
                lines.append("        %d, %d, /* opt %s */"
                             % (vals[0], vals[1], key))
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t g_travel_event_count = %d;" % len(events))
    lines.append("")
    lines.append(emit_fail_body(events, event_index))
    lines.append(emit_wxmod_body(events, event_index))
    return "\n".join(lines)


def emit_wx_apply_body():
    # Bank-14 applicator body (same bank as the tables above): bakes
    # the active weather's deltas into the staged event copy.  Keeps
    # the fixed bank clear (AGENTS.md 52.18): WRAM + same-bank reads
    # only, no fixed-bank calls, no di/ei.  Pointers select the
    # option's fields once, so one clamp ladder serves both options.
    return """
/* Weather-interaction applicator (this bank, next to its tables). */
static int8_t wx_clamp(int16_t v, int16_t lo, int16_t hi)
{
    if (v < lo) return (int8_t)lo;
    if (v > hi) return (int8_t)hi;
    return (int8_t)v;
}

void travel_wx_apply_banked(void)
{
    uint8_t i;
    uint8_t battle;
    uint8_t fbattle;
    int8_t *hurt;
    int8_t *delay;
    int8_t *save;
    int8_t *days;
    int8_t *prov;
    int8_t *fhurt;
    int8_t *fdelay;
    int8_t *fsave;
    int8_t *fdays;
    int8_t *fprov;
    const TravelWxMod *m;
    if (g_travel_weather == TRAVEL_WEATHER_NONE) return;
    if (s_event.kind != TRAVEL_EVENT_CHOICE) return;
    for (i = 0; i < g_travel_wxmod_count; i++) {
        m = &g_travel_wxmods[i];
        if (m->event != g_travel_event) continue;
        if (m->weather != g_travel_weather) continue;
        if (m->option == 0) {
            hurt = &s_event.opt1_hurt;
            delay = &s_event.opt1_delay;
            save = &s_event.opt1_save;
            days = &s_event.opt1_days;
            prov = &s_event.opt1_prov;
            battle = s_event.opt1_battle;
            fhurt = &s_event_fail.opt1.hurt;
            fdelay = &s_event_fail.opt1.delay;
            fsave = &s_event_fail.opt1.save;
            fdays = &s_event_fail.opt1.days;
            fprov = &s_event_fail.opt1.prov;
            fbattle = s_event_fail.opt1.battle;
        } else if (m->option == 1) {
            hurt = &s_event.opt2_hurt;
            delay = &s_event.opt2_delay;
            save = &s_event.opt2_save;
            days = &s_event.opt2_days;
            prov = &s_event.opt2_prov;
            battle = s_event.opt2_battle;
            fhurt = &s_event_fail.opt2.hurt;
            fdelay = &s_event_fail.opt2.delay;
            fsave = &s_event_fail.opt2.save;
            fdays = &s_event_fail.opt2.days;
            fprov = &s_event_fail.opt2.prov;
            fbattle = s_event_fail.opt2.battle;
        } else {
            continue;
        }
        *hurt = wx_clamp((int16_t)(*hurt + m->hurt), 0, 30);
        *fhurt = wx_clamp((int16_t)(*fhurt + m->hurt), 0, 30);
        if (battle == 0xFF) {
            *delay = wx_clamp((int16_t)(*delay + m->delay), 0, 10);
            *save = wx_clamp((int16_t)(*save + m->save), 0, 10);
            *days = wx_clamp((int16_t)(*days + m->days), 0, 10);
            *prov = wx_clamp((int16_t)(*prov + m->prov), -30, 30);
        }
        if (fbattle == 0xFF) {
            *fdelay = wx_clamp((int16_t)(*fdelay + m->delay), 0, 10);
            *fsave = wx_clamp((int16_t)(*fsave + m->save), 0, 10);
            *fdays = wx_clamp((int16_t)(*fdays + m->days), 0, 10);
            *fprov = wx_clamp((int16_t)(*fprov + m->prov), -30, 30);
        }
    }
}

/* Starving-party ambush picker (this bank, next to its tables):
 * counts battle events in the route's pool and picks the k-th one
 * without 64x banked_copy() loops in the fixed bank.
 * In: g_bk_byte_a = route, g_bk_byte_b = method, g_bk_byte_c = rng.
 * Out: g_bk_byte_a = event index or TRAVEL_EVENT_NONE. */
void travel_starve_battle_banked(void)
{
    uint8_t route;
    uint8_t method;
    uint8_t pick;
    uint8_t i;
    uint8_t count;
    uint8_t mask;
    uint8_t ev;
    const TravelMethod *m;
    route = g_bk_byte_a;
    method = g_bk_byte_b;
    pick = g_bk_byte_c;
    g_bk_byte_a = TRAVEL_EVENT_NONE;
    if (route >= g_travel_route_count) return;
    if (method >= g_travel_routes[route].method_count) return;
    m = &g_travel_routes[route].methods[method];
    count = 0;
    for (i = 0; i < m->pool_count; i++) {
        ev = m->pool_event[i];
        if (g_travel_events[ev].kind == TRAVEL_EVENT_BATTLE) {
            count++;
        }
    }
    if (count == 0) return;
    if (count > 1) {
        mask = 1;
        while (mask < (uint8_t)(count - 1)) {
            mask = (uint8_t)((mask << 1) | 1);
        }
        pick = (uint8_t)(pick & mask);
        if (pick >= count && !g_travel_rng) {
            g_travel_rng = 0xACE1;
        }
        while (pick >= count) {
            g_travel_rng ^= (uint16_t)(g_travel_rng << 7);
            g_travel_rng ^= (uint16_t)(g_travel_rng >> 9);
            g_travel_rng ^= (uint16_t)(g_travel_rng << 8);
            pick = (uint8_t)((uint8_t)g_travel_rng & mask);
        }
    } else {
        pick = 0;
    }
    for (i = 0; i < m->pool_count; i++) {
        ev = m->pool_event[i];
        if (g_travel_events[ev].kind == TRAVEL_EVENT_BATTLE) {
            if (pick == 0) {
                g_bk_byte_a = ev;
                return;
            }
            pick--;
        }
    }
}
"""


def emit_fail_body(events, event_index):
    lines = []
    lines.append("/* Fail branches: same order as g_travel_events. */")
    lines.append("const TravelEventFail g_travel_event_fails[] = {")
    for e in events:
        lines.append("    { /* %s */" % e["id"])
        if e["kind"] == "choice":
            doc = e["doc"]
            for opt in ("opt1", "opt2"):
                f = doc[opt + "_effects"]["fail"]
                lines.append("        { /* %s fail */" % opt)
                lines.append("            %s, /* text */" % c_str(f["text"]))
                ref = f.get("battle", None)
                idx = event_index[ref] if ref is not None else 255
                lines.append("            %d, /* fail battle or 255 */" % idx)
                lines.append("            %d, /* fail gold */"
                             % f.get("gold", 0))
                for key in ("provisions", "heal", "hurt",
                            "delay", "save", "days"):
                    lines.append("            %d, /* fail %s */"
                                 % (f.get(key, 0), key))
                lines.append("        },")
        else:
            for opt in ("opt1", "opt2"):
                lines.append('        { "", 255, 0, 0, 0, 0, 0, 0, 0 },')
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t g_travel_event_fail_count = %d;"
                 % len(events))
    lines.append("")
    return "\n".join(lines)


def emit_wxmod_body(events, event_index):
    """Sparse weather-interaction table: one row per (event, option,
    weather) delta declared via opt_effects.wx.  The runtime bakes the
    active row into the staged event copy, so paint and resolve see
    the weather-sharpened numbers with no extra lookups."""
    rows = []
    for e in events:
        if e["kind"] != "choice":
            continue
        doc = e["doc"]
        for opt_i, opt in enumerate(("opt1", "opt2")):
            wx = doc[opt + "_effects"].get("wx", None)
            if not wx:
                continue
            for wname, mod in wx.items():
                rows.append((event_index[e["id"]], opt_i,
                             WEATHER_ID[wname], e["id"], opt, wname, mod))
    if len(rows) > 255:
        fail("too many weather mods (%d, max 255)" % len(rows))
    lines = []
    lines.append("/* Weather interactions: sparse (event, option, "
                 "weather) deltas. */")
    lines.append("const TravelWxMod g_travel_wxmods[] = {")
    if rows:
        for (eidx, opt_i, wid, eid, opt, wname, mod) in rows:
            lines.append("    { /* %s %s in %s */" % (eid, opt, wname))
            lines.append("        %d, /* event */" % eidx)
            lines.append("        %d, /* option (0 = opt1) */" % opt_i)
            lines.append("        %d, /* weather (%s) */" % (wid, wname))
            for key in ("hurt", "delay", "save", "days", "provisions"):
                lines.append("        %d, /* %s delta */"
                             % (mod.get(key, 0), key))
            lines.append("    },")
    else:
        lines.append("    { 255, 0, 0, 0, 0, 0, 0, 0 },")
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t g_travel_wxmod_count = %d;" % len(rows))
    lines.append("")
    lines.append(emit_wx_apply_body())
    return "\n".join(lines)


def emit_route_ids(routes):
    lines = []
    lines.append("#ifndef TRAVEL_ROUTES_GENERATED_H")
    lines.append("#define TRAVEL_ROUTES_GENERATED_H")
    lines.append("")
    lines.append("/* Generated by journey_compile.py. Do not edit directly. */")
    for idx, doc in enumerate(routes):
        sym = "TRAVEL_ROUTE_" + re.sub(r"[^A-Z0-9]+", "_", doc["id"].upper()).strip("_")
        lines.append("#define %-32s %d" % (sym, idx))
    lines.append("")
    lines.append("#endif /* TRAVEL_ROUTES_GENERATED_H */")
    lines.append("")
    return "\n".join(lines)


def compile_all(out_path):
    battle_ids = load_battle_ids()
    scenes = load_registry()
    events = load_events(battle_ids)
    by_id = {e["id"]: e for e in events}
    for e in events:
        if e["kind"] != "choice":
            continue
        for opt in ("opt1", "opt2"):
            for branch in (e["doc"][opt + "_effects"],
                           e["doc"][opt + "_effects"]["fail"]):
                ref = branch.get("battle", None)
                if ref is not None and ref not in by_id:
                    fail("%s: %s_effects.battle '%s' is not an event"
                         % (e["id"], opt, ref))
                if ref is not None and by_id[ref]["kind"] != "battle":
                    fail("%s: %s_effects.battle '%s' is not a battle event"
                         % (e["id"], opt, ref))
    routes = load_routes(scenes, {e["id"] for e in events},
                         {e["id"]: e["acts"] for e in events})
    for r in routes:
        for m in r["methods"]:
            for entry in m["pool"]:
                eid = entry["event"]
                if by_id[eid]["kind"] != "choice":
                    continue
                for opt in ("opt1", "opt2"):
                    for branch in (by_id[eid]["doc"][opt + "_effects"],
                                   by_id[eid]["doc"][opt + "_effects"]
                                   ["fail"]):
                        ref = branch.get("battle", None)
                        if ref is not None and \
                                r["act"] not in by_id[ref]["acts"]:
                            fail("%s: opt battle '%s' is acts %s, "
                                 "road is act %d"
                                 % (eid, ref, by_id[ref]["acts"], r["act"]))
    text = emit(routes, events, scenes, battle_ids)
    hdr = emit_route_ids(routes)
    if out_path is not None:
        out_path.write_text(text, encoding="utf-8")
        (out_path.parent / OUT_IDS_NAME).write_text(hdr, encoding="utf-8")
    return text, hdr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true", required=True)
    ap.add_argument("-o", dest="out", default=None)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    try:
        out = Path(args.out) if args.out else OUT_DEFAULT
        if args.check:
            fresh, fresh_hdr = compile_all(None)
            want = out.read_text(encoding="utf-8")
            hdr_path = out.parent / OUT_IDS_NAME
            want_hdr = hdr_path.read_text(encoding="utf-8") if hdr_path.exists() else ""
            if fresh != want or fresh_hdr != want_hdr:
                print("journeys-check: committed travel content is stale "
                      "(re-run journey_compile.py --all)", file=sys.stderr)
                return 1
            print("journeys-check: fresh")
            return 0
        compile_all(out)
        print("Compiled %s" % out)
        return 0
    except ValueError as ex:
        print("journey_compile: %s" % ex, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
