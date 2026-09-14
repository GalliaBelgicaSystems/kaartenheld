"""BFS route planner over the real working content (levels/) — docs/
verify-walkthrough.md §3 Phase 3.

Reuses the level compiler's own collision derivation (derive_collision +
load_tilesets) so the walkthrough and the ROM agree on walkability with
zero duplicated logic.  Routes adapt automatically to content edits: actor
positions, exits, and patrol boxes are read from levels/*.json at runtime
(no hardcoded coordinates).

Multi-map graph: walkable cells per scene, portal edges from the exits
(entering an exit tile transitions to the target scene).
"""

import json
import os
import sys
from collections import deque

from walkthrough.session import LEVELS_DIR, REPO

# Import the level compiler's derivation (single source of truth).
sys.path.insert(0, os.path.join(REPO, "tools", "level_compiler"))
from validate import load_tilesets          # noqa: E402
from compile import derive_collision, scene_table_order, is_level_file   # noqa: E402
from collision import effective_actor_flags   # noqa: E402

# Patrol boxes (src/world/actor.h): blocked cells for routing — the
# walkthrough must not steer through a hostile's patrol path.
AI_NONE = 0
AI_PATROL_CIRCLE = 1
AI_PATROL_CROSS = 2
AI_CHASE = 3              # steps toward the player every AI tick
AI_PATROL_VERT = 4
AI_PATROL_VERT_TILES = 3

# levels/*.json carries the enum NAMES as strings.
AI_NAMES = {
    "AI_NONE": AI_NONE,
    "AI_PATROL_CIRCLE": AI_PATROL_CIRCLE,
    "AI_PATROL_CROSS": AI_PATROL_CROSS,
    "AI_CHASE": AI_CHASE,
    "AI_PATROL_VERT": AI_PATROL_VERT,
}


def patrol_cells(ai, x, y):
    """Cells the hostile may occupy (conservative inflation)."""
    if ai == AI_PATROL_VERT:
        return {(x, dy) for dy in range(y - AI_PATROL_VERT_TILES,
                                        y + AI_PATROL_VERT_TILES + 1)}
    if ai == AI_PATROL_CROSS:
        return {(x, y), (x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)}
    if ai == AI_PATROL_CIRCLE:
        return {(dx, dy) for dx in (x - 1, x) for dy in (y - 1, y)}
    if ai == AI_CHASE:
        # Chasers close on the player from anywhere: block a generous
        # halo so routes keep their distance.
        return {(dx, dy) for dx in range(x - 2, x + 3)
                for dy in range(y - 2, y + 3)}
    return {(x, y)}


# Neighbor edge directions in stable order (matches the engine's
# N/S/E/W checks and the mirrored-spawn rule).
EDGE_DIRS = ("north", "south", "east", "west")
_EDGE_NORMAL = {"north": (0, -1), "south": (0, 1),
                "west": (-1, 0), "east": (1, 0)}
_EDGE_BTN = {(0, -1): "up", (0, 1): "down",
             (-1, 0): "left", (1, 0): "right"}


def _edge_cells(scene):
    """Border cells per direction (corners belong to two edges)."""
    w, h = scene.width, scene.height
    return {
        "north": [(x, 0) for x in range(w)],
        "south": [(x, h - 1) for x in range(w)],
        "west": [(0, y) for y in range(h)],
        "east": [(w - 1, y) for y in range(h)],
    }


class Scene:
    def __init__(self, name, scene_id, level, tileset):
        self.name = name
        self.scene_id = scene_id
        self.level = level
        self.width = level["map"]["width"]
        self.height = level["map"]["height"]
        self.grid = derive_collision(level, tileset)
        self.exits = level.get("exits", [])
        # Point-exit triggers keep their painted art (or compiler-opened
        # ground); they are portal nodes for routing whether or not the
        # collision grid marks them.
        self.exit_cells = {(e["x"], e["y"]) for e in self.exits}
        # Whole-edge links (neighbors): every walkable border cell of a
        # linked edge is a portal node with a mirrored entry spawn.
        # Corner cells belong to two edges, but the ROM fires in N/S/W/E
        # priority order, so a corner is assigned to the first linked
        # walkable edge in that order (setdefault): planner and ROM can
        # never disagree on where a corner leads.
        self.neighbors = level.get("neighbors", {}) or {}
        self.edge_cells = {}  # (x, y) -> (direction, target_scene)
        for direction in ("north", "south", "west", "east"):
            target = (self.neighbors.get(direction) or "").strip()
            if not target:
                continue
            for cell in _edge_cells(self)[direction]:
                if cell in self.edge_cells:
                    continue
                # A point exit on the same cell wins over the edge rule
                # in the ROM (gate check first), so it is never an edge
                # portal.
                if cell in self.exit_cells:
                    continue
                if self.grid[cell[1]][cell[0]]:
                    self.edge_cells[cell] = (direction, target)
        self.hostiles = []      # (x, y, ai)
        self.blocked_actors = set()
        for obj in level.get("objects", []):
            props = obj.get("properties", {}) or {}
            if not props.get("entity_id"):
                continue  # decoration: compile.py emits no actor row
            # Mirror actor_load_scene_banked: HOSTILE flags spawn into
            # World.actors (patrol boxes below); every OTHER compiled actor
            # lands in g_static_actors, which world_try_begin_move blocks on
            # unconditionally (ACTOR_FLAG_BLOCKING is emitted but never read
            # by the ROM -- see docs/roadmap.md).  Flags default by type via
            # the compiler's single source (collision.effective_actor_flags).
            flags = effective_actor_flags(obj.get("type"), props)
            x = obj["position"]["x"]
            y = obj["position"]["y"]
            if "HOSTILE" in flags:
                ai_name = props.get("ai", "AI_NONE")
                self.hostiles.append((x, y, AI_NAMES.get(ai_name, AI_NONE)))
            else:
                self.blocked_actors.add((x, y))

    def walkable(self, x, y, avoid=None):
        if not (0 <= x < self.width and 0 <= y < self.height):
            return False
        # `avoid` first: callers that must not step onto a portal (walking
        # past one exit to reach another) add other exit cells to avoid.
        if avoid and (x, y) in avoid:
            return False
        if (x, y) in self.exit_cells:
            return True
        if (x, y) in self.edge_cells:
            return True
        if not self.grid[y][x]:
            return False
        return True

    def portals_to(self, target_scene_name):
        """Point exits targeting the scene, in order."""
        return [e for e in self.exits
                if e.get("target_scene") == target_scene_name]

    def edge_goals(self, target_scene_name):
        """(cell, direction) edge portals targeting the scene."""
        return [(cell, direction)
                for cell, (direction, target) in self.edge_cells.items()
                if target == target_scene_name]

    def patrol_blocked(self):
        """Union of all hostiles' patrol cells."""
        cells = set()
        for x, y, ai in self.hostiles:
            cells |= patrol_cells(ai, x, y)
        return cells


class Planner:
    def __init__(self, levels_dir=LEVELS_DIR):
        """Mirror the compiler's content registry: scene ids come from
        levels/registry.json (same scene_table_order() the ROM tables
        use), so planner scene ids match the ROM's compiled ids.  A NEW
        level added by the editor gets a scene id automatically and is
        swept by walk_sweep."""
        self.scenes = {}
        tilesets = load_tilesets()
        names = sorted(os.path.splitext(f)[0]
                       for f in os.listdir(levels_dir)
                       if f.endswith(".json") and is_level_file(f))
        levels = {}
        for name in names:
            data = json.load(open(os.path.join(levels_dir,
                                               name + ".json")))
            levels[data["id"]] = data
        ordered, _ = scene_table_order(levels)
        for pos, name in enumerate(ordered):
            if name is None:
                continue  # retired hole
            level = levels[name]
            ts = tilesets.get(level["map"]["tileset"], {})
            self.scenes[name] = Scene(name, pos, level, ts)

    def arrival_pos(self, name):
        """A reachable tile inside scene `name` (a parent exit's spawn,
        else the scene's own spawn as a neighbor-entry fallback: route()
        lands edge crossings exactly and walks from there).  None if the
        scene has no inbound portal at all."""
        for scene in self.scenes.values():
            for e in scene.exits:
                if e["target_scene"] == name:
                    return (e["target_x"], e["target_y"])
        for scene in self.scenes.values():
            for target in scene.neighbors.values():
                if (target or "").strip() == name:
                    level = self.scenes[name].level
                    spawn = level.get("player", {}).get("spawn", {})
                    return (spawn.get("x", 2), spawn.get("y", 2))
        return None

    def scene_of(self, scene_id):
        for s in self.scenes.values():
            if s.scene_id == scene_id:
                return s
        raise KeyError("no scene with id %d" % scene_id)

    # ── BFS ──────────────────────────────────────────────────────────
    def _bfs(self, scene, start, goal, avoid=None):
        """Shortest walkable path start->goal as a list of (dx, dy)
        moves.  Exit tiles are ordinary cells here; the caller decides
        whether the goal is an exit (portal crossing)."""
        if start == goal:
            return []
        prev = {start: None}
        q = deque([start])
        while q:
            cur = q.popleft()
            for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                nxt = (cur[0] + dx, cur[1] + dy)
                if nxt in prev:
                    continue
                if not scene.walkable(nxt[0], nxt[1], avoid):
                    continue
                prev[nxt] = cur
                if nxt == goal:
                    path = []
                    node = nxt
                    while prev[node] is not None:
                        p = prev[node]
                        path.append((node[0] - p[0], node[1] - p[1]))
                        node = p
                    return list(reversed(path))
                q.append(nxt)
        return None

    def path(self, scene_name, start, goal, avoid_exits=False):
        """Path within one scene.  `avoid` = patrol cells + blocking
        actors, EXCEPT cells that are the start/goal themselves (the
        start may sit inside a patrol box right after boot, and the
        goal may be a bump-adjacent cell next to a blocking actor).
        `avoid_exits` also routes AROUND every exit gate except the goal
        and start, so a walk to one gate never warps through another."""
        scene = self.scenes[scene_name]
        avoid = scene.patrol_blocked() | scene.blocked_actors
        if avoid_exits:
            avoid |= (scene.exit_cells - {(start[0], start[1]), (goal[0], goal[1])})
            avoid |= (set(scene.edge_cells) - {(start[0], start[1]), (goal[0], goal[1])})
        avoid.discard(start)
        avoid.discard(goal)
        return self._bfs(scene, start, goal, avoid)

    def path_to_exit(self, scene_name, start, target_scene_name):
        """Path to the exit tile whose target_scene is target_scene_name.
        Returns (path_to_exit_cell, exit).  Other exit gates are avoided
        so the walk cannot accidentally cross a different portal."""
        scene = self.scenes[scene_name]
        for e in scene.portals_to(target_scene_name):
            goal = (e["x"], e["y"])
            path = self.path(scene_name, start, goal, avoid_exits=True)
            if path is not None:
                return path, e
        return None, None

    def path_to_link(self, scene_name, start, target_scene_name):
        """Path to a point exit or a linked edge leading to the target.
        Returns (path, button, exit_dict, goal_cell, direction) where
        exit_dict carries at least target_scene (point exits carry the
        full row), goal_cell is the portal cell, and direction is the
        linked edge (None for point exits)."""
        start = (start[0], start[1])
        path, e = self.path_to_exit(scene_name, start, target_scene_name)
        if path is not None and path:
            dx, dy = path[-1]
            return path, _EDGE_BTN[(dx, dy)], e, (e["x"], e["y"]), None
        best, best_dir, best_goal = None, None, None
        scene = self.scenes[scene_name]
        other = self.scenes[target_scene_name]
        # Landing inside a hostile patrol halo (or on a blocking actor)
        # is an ambush, not an arrival: prefer entries outside them.
        # Two passes so a fully-covered map still routes (loudly, via
        # battle) instead of failing.
        halo = other.patrol_blocked() | other.blocked_actors
        avoid = scene.patrol_blocked() | scene.blocked_actors
        avoid |= (scene.exit_cells - {(start[0], start[1])})
        avoid |= (set(scene.edge_cells) - {(start[0], start[1])})
        avoid.discard(start)
        for safe_only in (True, False):
            for goal, direction in scene.edge_goals(target_scene_name):
                if not self._entry_ok(goal, direction, target_scene_name):
                    continue  # trap link: landing cell is not walkable
                entry = self._mirror_entry(goal, direction,
                                           target_scene_name)
                if safe_only and entry in halo:
                    continue
                avoid.discard(goal)
                epath = self._bfs(scene, start, goal, avoid)
                avoid.add(goal)
                if epath is not None and (best is None or len(epath) < len(best)):
                    best, best_dir, best_goal = epath, direction, goal
            if best is not None:
                break
        if best is None or not best:
            return None, None, None, None, None
        return best, _EDGE_BTN[_EDGE_NORMAL[best_dir]], \
            {"target_scene": target_scene_name}, best_goal, best_dir

    def _scene_next(self, from_name, to_name):
        """Next hop on the shortest scene-graph path from -> to (BFS over
        exit + neighbor adjacency); None if unreachable.  A neighbor link
        counts only when at least one crossing has a walkable landing
        cell, so a trap link is never routed through."""
        prev = {from_name: None}
        q = deque([from_name])
        while q:
            cur = q.popleft()
            nxts = [e["target_scene"] for e in self.scenes[cur].exits]
            nxts += [t for t in self.scenes[cur].neighbors.values()
                     if (t or "").strip() and self._edge_reaches(cur, t.strip())]
            for nxt in nxts:
                if nxt in prev:
                    continue
                prev[nxt] = cur
                if nxt == to_name:
                    node = nxt
                    while prev[node] != from_name:
                        node = prev[node]
                    return node
                q.append(nxt)
        return None

    def route(self, from_scene, start, to_scene, goal):
        """Cross-scene route: list of steps.
        ("move", dx, dy) | ("exit", direction, exit).  Edge crossings use
        the same ("exit", ...) step shape with a synthetic exit dict
        (follow() only needs target_scene)."""
        steps = []
        scene_name = from_scene
        cur = start
        guard = 0
        while scene_name != to_scene:
            guard += 1
            if guard > 8:
                raise ValueError("route: too many transitions "
                                 "(%s -> %s)" % (from_scene, to_scene))
            next_scene = self._scene_next(scene_name, to_scene)
            if next_scene is None:
                raise ValueError("route: %s cannot reach %s"
                                 % (from_scene, to_scene))
            found = self.path_to_link(scene_name, cur, next_scene)
            if not found or found[0] is None:
                raise ValueError("route: no exit path %s:%s -> %s"
                                 % (scene_name, cur, next_scene))
            path, btn, e, portal, direction = found
            # The BFS path's final move enters the gate/edge cell — that
            # move IS the portal crossing, so convert it to an exit step
            # (press toward the portal, ride the wipe).
            moves = path[:-1]
            steps += [("move", dx, dy) for dx, dy in moves]
            steps.append(("exit", btn, e))
            cur = self._arrival(e, portal, direction, next_scene)
            scene_name = to_scene if e.get("target_scene") == to_scene \
                else e["target_scene"]
        path = self.path(scene_name, cur, goal, avoid_exits=True)
        if path is None:
            raise ValueError("route: no path %s:%s -> %s"
                             % (scene_name, cur, goal))
        steps += [("move", dx, dy) for dx, dy in path]
        return steps

    def _arrival(self, e, goal, direction, to_name):
        """Landing tile after crossing portal e into to_name: explicit
        spawn for point exits, mirrored entry from the crossed edge cell
        for linked edges (mirror of edge_banked.c, using the crossed
        direction — corners belong to two edges, so position alone
        cannot decide)."""
        if "target_x" in e:
            return (e["target_x"], e["target_y"])
        return self._mirror_entry(goal, direction, to_name)

    def _mirror_entry(self, goal, direction, to_name):
        """Mirror of the ROM's edge-spawn rule (edge_banked.c): leaving
        via an edge enters one cell inside the OPPOSITE edge, with the
        crossing coordinate clamped for size mismatches."""
        other = self.scenes[to_name]
        gx, gy = goal
        w, h = other.width, other.height
        if direction == "north":
            return (max(1, min(gx, w - 2)), h - 2)
        if direction == "south":
            return (max(1, min(gx, w - 2)), 1)
        if direction == "west":
            return (w - 2, max(1, min(gy, h - 2)))
        return (1, max(1, min(gy, h - 2)))

    def _entry_ok(self, goal, direction, to_name):
        """True when the mirrored landing cell in `to_name` is walkable.
        Trap links (landing inside a wall) are never routed through; the
        compiler rejects them, this keeps the planner honest on stale
        content too."""
        if to_name not in self.scenes:
            return False
        other = self.scenes[to_name]
        ex, ey = self._mirror_entry(goal, direction, to_name)
        if not (0 <= ex < other.width and 0 <= ey < other.height):
            return False
        return other.walkable(ex, ey)

    def _edge_reaches(self, from_name, target):
        """True when some linked-edge cell of `from_name` has a walkable
        landing cell in `target`."""
        scene = self.scenes[from_name]
        for cell, (direction, t) in scene.edge_cells.items():
            if t == target and self._entry_ok(cell, direction, target):
                return True
        return False

    # ── encounter helpers ────────────────────────────────────────────
    def edge_of(self, scene_name, start, hostile_xy):
        """Nearest walkable cell adjacent to a hostile's patrol box (the
        sweep start for an engagement walk)."""
        scene = self.scenes[scene_name]
        hx, hy = hostile_xy
        best, best_cost = None, None
        for dy in range(-AI_PATROL_VERT_TILES - 1,
                        AI_PATROL_VERT_TILES + 2):
            for dx in (-2, -1, 0, 1, 2):
                cand = (hx + dx, hy + dy)
                path = self.path(scene_name, start, cand)
                if path is not None:
                    cost = len(path)
                    if best_cost is None or cost < best_cost:
                        best, best_cost = cand, cost
        return best
