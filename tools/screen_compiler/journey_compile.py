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
    supply (food/water prices + food_per_day/water_per_day burn),
    camp (hunt food_min/max + delay_min/max, rest heal + delay,
    starve food/water hp + every, study save_min/max),
    methods (1..2 entries, see below).

Event schemas (kind):
    battle: text (<=20), battle (BATTLE_* in src/world/actor.h).
    choice: text (<=20, prompt), help_text (<=20, result line),
            help_reward_gold (0..30000), help_delay_steps (0..255),
            ignore_text (<=20).
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

TRAVEL_POOL_MAX = 4
TRAVEL_TEXT_LEN = 20
TRAVEL_NAME_LEN = 11
TRAVEL_METHODS_MAX = 2
TRAVEL_BANK = 12


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
                           "victory_days": days, "late_hp": late})
        elif kind == "choice":
            check_text("text", doc.get("text"), eid)
            check_text("help_text", doc.get("help_text"), eid)
            check_text("ignore_text", doc.get("ignore_text"), eid)
            gold = doc.get("help_reward_gold")
            delay = doc.get("help_delay_steps")
            ignore = doc.get("ignore_days")
            if not isinstance(gold, int) or not (0 <= gold <= 30000):
                fail("%s: help_reward_gold must be 0..30000" % eid)
            if not isinstance(delay, int) or not (0 <= delay <= 255):
                fail("%s: help_delay_steps must be 0..255" % eid)
            if not isinstance(ignore, int) or not (0 <= ignore <= 255):
                fail("%s: ignore_days must be 0..255" % eid)
            events.append({"id": eid, "kind": "choice",
                           "text": doc["text"], "help_text": doc["help_text"],
                           "ignore_text": doc["ignore_text"],
                           "gold": gold, "delay": delay, "ignore": ignore})
        elif kind == "ruins":
            check_text("text", doc.get("text"), eid)
            check_text("explore_text", doc.get("explore_text"), eid)
            check_text("ignore_text", doc.get("ignore_text"), eid)
            events.append({"id": eid, "kind": "ruins",
                           "text": doc["text"],
                           "help_text": doc["explore_text"],
                           "ignore_text": doc["ignore_text"],
                           "gold": 0, "delay": 0, "ignore": 0})
        else:
            fail("%s: kind must be 'battle', 'choice' or 'ruins'" % eid)
    if not events:
        fail("no events in journeys/events/")
    return events


def load_routes(scenes, event_ids):
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
            fail("%s: supply must be an object with food_price/water_price"
                 % rid)
        for key in ("food_price", "water_price", "food_per_day",
                    "water_per_day"):
            v = supply.get(key)
            if not isinstance(v, int) or not (0 <= v <= 255):
                fail("%s: supply.%s must be 0..255" % (rid, key))
        camp = doc.get("camp")
        if not isinstance(camp, dict):
            fail("%s: camp needs hunt/rest/starve/study" % rid)
        hunt = camp.get("hunt")
        if not isinstance(hunt, dict):
            fail("%s: camp.hunt must be an object" % rid)
        for key, lo, hi in (("food_min", 0, 30), ("food_max", 0, 30),
                             ("water_min", 0, 30), ("water_max", 0, 30),
                             ("delay_min", 0, 10), ("delay_max", 0, 10)):
            v = hunt.get(key)
            if not isinstance(v, int) or not (lo <= v <= hi):
                fail("%s: camp.hunt.%s must be %d..%d" % (rid, key, lo, hi))
        if hunt["food_max"] < hunt["food_min"]:
            fail("%s: camp.hunt.food_max < food_min" % rid)
        if hunt["water_max"] < hunt["water_min"]:
            fail("%s: camp.hunt.water_max < water_min" % rid)
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
        for key, lo, hi in (("food_hp", 0, 30), ("food_every", 1, 10),
                             ("water_hp", 0, 30), ("water_every", 1, 10)):
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
        for key in ("food", "water"):
            v = study.get(key)
            if not isinstance(v, int) or not (0 <= v <= 30):
                fail("%s: camp.study.%s must be 0..30" % (rid, key))
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
    lines.append("")
    lines.append("#pragma bank %d" % TRAVEL_BANK)
    lines.append("")
    lines.append("const TravelRoute g_travel_routes[] = {")
    for doc in routes:
        lines.append("    { /* %s */" % doc["id"])
        lines.append("        %s, /* name */" % c_str(doc["name"]))
        lines.append("        %d, /* dest_scene (%s) */"
                     % (scenes[doc["destination"]], doc["destination"]))
        lines.append("        %d, %d, /* dest_x, dest_y */"
                     % (doc["dest_x"], doc["dest_y"]))
        lines.append("        %d, /* food_price */" % doc["supply"]["food_price"])
        lines.append("        %d, /* water_price */" % doc["supply"]["water_price"])
        lines.append("        %d, /* food_per_day */" % doc["supply"]["food_per_day"])
        lines.append("        %d, /* water_per_day */" % doc["supply"]["water_per_day"])
        lines.append("        %d, /* hunt_food_min */" % doc["camp"]["hunt"]["food_min"])
        lines.append("        %d, /* hunt_food_max */" % doc["camp"]["hunt"]["food_max"])
        lines.append("        %d, /* hunt_water_min */" % doc["camp"]["hunt"]["water_min"])
        lines.append("        %d, /* hunt_water_max */" % doc["camp"]["hunt"]["water_max"])
        lines.append("        %d, /* hunt_delay_min */" % doc["camp"]["hunt"]["delay_min"])
        lines.append("        %d, /* hunt_delay_max */" % doc["camp"]["hunt"]["delay_max"])
        lines.append("        %d, /* rest_heal */" % doc["camp"]["rest"]["heal"])
        lines.append("        %d, /* rest_delay */" % doc["camp"]["rest"]["delay"])
        lines.append("        %d, /* starve_food_hp */" % doc["camp"]["starve"]["food_hp"])
        lines.append("        %d, /* starve_food_every */" % doc["camp"]["starve"]["food_every"])
        lines.append("        %d, /* starve_water_hp */" % doc["camp"]["starve"]["water_hp"])
        lines.append("        %d, /* starve_water_every */" % doc["camp"]["starve"]["water_every"])
        lines.append("        %d, /* study_save_min */" % doc["camp"]["study"]["save_min"])
        lines.append("        %d, /* study_save_max */" % doc["camp"]["study"]["save_max"])
        lines.append("        %d, /* study_food */" % doc["camp"]["study"]["food"])
        lines.append("        %d, /* study_water */" % doc["camp"]["study"]["water"])
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
            lines.append("        0, /* delay */")
            lines.append("        0, /* ignore_days (unused) */")
            lines.append("        %s, /* text */" % c_str(e["text"]))
            lines.append('        "", /* text2 */')
            lines.append('        "", /* text3 */')
        else:
            if e["kind"] == "ruins":
                lines.append("        TRAVEL_EVENT_RUINS,")
            else:
                lines.append("        TRAVEL_EVENT_CHOICE,")
            lines.append("        0, /* battle */")
            lines.append('        "", /* enemy_name (unused) */')
            lines.append("        0, /* enemy_hp */")
            lines.append("        %d, /* reward_gold */" % e["gold"])
            lines.append("        0, /* victory_days (unused) */")
            lines.append("        0, /* late_enemy_hp (unused) */")
            lines.append("        %d, /* delay */" % e["delay"])
            lines.append("        %d, /* ignore_days */" % e["ignore"])
            lines.append("        %s, /* text */" % c_str(e["text"]))
            lines.append("        %s, /* help result */" % c_str(e["help_text"]))
            lines.append("        %s, /* ignore result */" % c_str(e["ignore_text"]))
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t g_travel_event_count = %d;" % len(events))
    lines.append("")
    return "\n".join(lines)


def compile_all(out_path):
    battle_ids = load_battle_ids()
    scenes = load_registry()
    events = load_events(battle_ids)
    routes = load_routes(scenes, {e["id"] for e in events})
    text = emit(routes, events, scenes, battle_ids)
    if out_path is not None:
        out_path.write_text(text, encoding="utf-8")
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true", required=True)
    ap.add_argument("-o", dest="out", default=None)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    try:
        if args.check:
            fresh = compile_all(None)
            if args.out:
                want = Path(args.out).read_text(encoding="utf-8")
            else:
                want = OUT_DEFAULT.read_text(encoding="utf-8")
            if fresh != want:
                print("journeys-check: committed travel_content.c is stale "
                      "(re-run journey_compile.py --all)", file=sys.stderr)
                return 1
            print("journeys-check: fresh")
            return 0
        out = Path(args.out) if args.out else OUT_DEFAULT
        compile_all(out)
        print("Compiled %s" % out)
        return 0
    except ValueError as ex:
        print("journey_compile: %s" % ex, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
