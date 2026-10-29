"""Entry-spawn contract tests (per-arrival landings).

Locks the Python side of the directional-entry contract:
player.entry.<side> selected by arrival side (opposite of the crossed
edge), missing entries falling back to the main spawn, and point-exit
target_entry resolving to the destination entry.  Mirrors the ROM rule
(src/world/world.c EDGE path + SceneDefinition entries).

Run: python3 tools/level_compiler/tests/test_entry_spawns.py
Exit code 0 = all pass, 1 = failure.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from collision import (
    ENTRY_KEYS,
    edge_link_report,
    entry_side_for_link,
    get_entry,
    resolve_entry_spawn,
    resolve_exit_landing,
    unused_entry_warnings,
    used_entry_keys,
)

FAILURES = 0


def check(name, actual, expected):
    global FAILURES
    if actual != expected:
        print("FAIL %s: got %r want %r" % (name, actual, expected))
        FAILURES += 1
    else:
        print("ok %s" % name)


def lvl(lid, spawn, entry=None, neighbors=None, exits=None, w=20, h=18):
    player = {"spawn": dict(spawn)}
    if entry:
        player["entry"] = {k: dict(v) for k, v in entry.items()}
    return {
        "id": lid,
        "map": {"width": w, "height": h, "tileset": "forest"},
        "player": player,
        "neighbors": dict(neighbors or {}),
        "exits": list(exits or []),
        "layers": {"terrain": []},
        "objects": [],
    }


def main():
    tilesets = {"forest": {"tiles": []}}

    # Side mapping: arrival side is opposite the crossed edge.
    check("side north->south", entry_side_for_link("north"), "south")
    check("side south->north", entry_side_for_link("south"), "north")
    check("side east->west", entry_side_for_link("east"), "west")
    check("side west->east", entry_side_for_link("west"), "east")

    # get_entry validation.
    check("entry keys", ENTRY_KEYS,
          ("north", "south", "east", "west", "stairs_up", "stairs_down"))
    check("missing entry", get_entry(lvl("test_a", {"x": 1, "y": 1}), "west"), None)
    check("bad key", get_entry(lvl("test_a", {"x": 1, "y": 1}), "up"), None)
    check("entry coords",
          get_entry(lvl("test_a", {"x": 1, "y": 1},
                        {"west": {"x": 3, "y": 4, "facing": "RIGHT"}}), "west"),
          {"x": 3, "y": 4, "facing": "RIGHT"})

    # Fallback to main spawn.
    a = lvl("test_a", {"x": 5, "y": 6, "facing": "DOWN"})
    check("fallback coords", resolve_entry_spawn(a, "west"), (5, 6))
    b = lvl("test_b", {"x": 5, "y": 6},
            {"west": {"x": 1, "y": 7, "facing": "RIGHT"}})
    check("entry wins", resolve_entry_spawn(b, "west"), (1, 7))
    check("other side falls back", resolve_entry_spawn(b, "east"), (5, 6))

    # used_entry_keys: source link direction consumes the opposite side.
    by_id = {
        "test_a": lvl("test_a", {"x": 5, "y": 6}, neighbors={"east": "test_b"}),
        "test_b": lvl("test_b", {"x": 5, "y": 6}, neighbors={"west": "test_a"}),
    }
    check("used keys", used_entry_keys(by_id),
          {"test_a": {"east"}, "test_b": {"west"}})

    # Edge report: landing is the arrival-side entry.
    report = {e["direction"]: e
              for e in edge_link_report("test_a", by_id["test_a"], by_id, tilesets)}
    check("report side", report["east"]["entry_side"], "west")
    # (2,2) interior is walkable (empty terrain = default ground); the
    # fallback spawn (5,6) is walkable, so the link is ok with a warning
    # (no west entry defined on b).
    check("fallback link ok", report["east"]["ok"], True)
    check("fallback warns", len(report["east"]["warnings"]) > 0, True)

    # Defined + walkable entry: no warnings about fallback.
    by_id["test_b"]["player"]["entry"] = {"west": {"x": 1, "y": 7, "facing": "RIGHT"}}
    report = {e["direction"]: e
              for e in edge_link_report("test_a", by_id["test_a"], by_id, tilesets)}
    check("entry link ok", report["east"]["ok"], True)
    check("entry link quiet", report["east"]["warnings"], [])

    # Blocked entry: hard error naming the side.
    by_id["test_b"]["layers"] = {"terrain": [[
        "forest.forest_plain_floor_2" if (x, y) != (1, 7) else "forest.forest_top_left_treetop"
        for x in range(20)] for y in range(18)]}
    # Painted cells need manifest walkability: fake a tileset.
    tilesets2 = {"forest": {"tiles": [
        {"id": "forest_plain_floor_2", "walkable": True},
        {"id": "forest_top_left_treetop", "walkable": False},
    ]}}
    report = {e["direction"]: e
              for e in edge_link_report("test_a", by_id["test_a"], by_id, tilesets2)}
    check("blocked entry fails", report["east"]["ok"], False)

    # Point-exit target_entry resolution.
    dest = lvl("d", {"x": 9, "y": 9},
               {"stairs_up": {"x": 4, "y": 4, "facing": "UP"}})
    by_id2 = {"s": lvl("s", {"x": 1, "y": 1}), "d": dest}
    ex = {"x": 2, "y": 2, "target_scene": "d",
          "target_x": 9, "target_y": 9, "target_entry": "stairs_up"}
    check("exit entry landing", resolve_exit_landing(ex, by_id2), (4, 4, True))
    ex2 = {"x": 2, "y": 2, "target_scene": "d",
           "target_x": 9, "target_y": 9, "target_entry": "stairs_down"}
    check("exit missing entry falls back", resolve_exit_landing(ex2, by_id2),
          (9, 9, True))
    ex3 = {"x": 2, "y": 2, "target_scene": "d",
           "target_x": 9, "target_y": 9}
    check("plain exit raw", resolve_exit_landing(ex3, by_id2), (9, 9, False))

    # Unused-entry warnings.
    lonely = lvl("lonely", {"x": 1, "y": 1},
                 {"north": {"x": 2, "y": 2, "facing": "DOWN"}})
    warns = unused_entry_warnings({"lonely": lonely})
    check("unused warns", len(warns), 1)

    if FAILURES:
        print("%d checks FAILED" % FAILURES)
        return 1
    print("all entry-spawn checks pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
