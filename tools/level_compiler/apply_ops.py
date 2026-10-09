#!/usr/bin/env python3
"""
apply_ops.py - LLM & Programmatic Command Layer for Game Boy RPG Level Files

Executes high-level structured operations against level JSON files.
Both human UI tools and LLM agents use these exact same operations.

Supported Operations:
- create_level: { operation: "create_level", id: "dungeon", width: 20, height: 18, tileset: "interior" }
- paint_rectangle: { operation: "paint_rectangle", tile: "forest.tree", x: 3, y: 2, width: 5, height: 4 }
- paint_tile: { operation: "paint_tile", tile: "forest.tree", x: 5, y: 5 }
- place_object: { operation: "place_object", id: "hermit", type: "npc", position: {x: 8, y: 6}, properties: {...} }
- move_object: { operation: "move_object", id: "hermit", position: {x: 9, y: 6} }
- delete_object: { operation: "delete_object", id: "hermit" }
- create_exit: { operation: "create_exit", x: 12, y: 0, target_scene: "mountain_pass", target_x: 12, target_y: 10, direction: "NORTH" }
- delete_exit: { operation: "delete_exit", x: 12, y: 0 }
- set_spawn: { operation: "set_spawn", x: 12, y: 10, facing: "UP" }
- describe_region: { operation: "describe_region", id: "north_clearing", bounds: {x: 3, y: 2, width: 8, height: 6}, description: "...", gameplay: {...} }
- delete_region: { operation: "delete_region", id: "north_clearing" }

Usage:
    python3 tools/level_compiler/apply_ops.py levels/forest.json --op '{"operation": "paint_rectangle", "tile": "forest.tree", "x": 4, "y": 2, "width": 2, "height": 1}'
    python3 tools/level_compiler/apply_ops.py levels/forest.json --ops-file ops.json
"""

import sys
import os
import json
import argparse
from pathlib import Path

# Add level_compiler directory to sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
sys.path.insert(0, str(SCRIPT_DIR))

from validate import validate_level, load_tilesets


def ensure_grid_format(level_data):
    """Ensure terrain layer is in 2D array grid format for easy pixel/tile editing."""
    map_info = level_data["map"]
    w = map_info["width"]
    h = map_info["height"]
    tileset_id = map_info.get("tileset", "exterior")
    default_tile = f"{tileset_id}.floor"

    layers = level_data.setdefault("layers", {})
    terrain = layers.get("terrain")

    if isinstance(terrain, list) and len(terrain) > 0 and isinstance(terrain[0], list):
        return terrain

    # Convert block list to grid
    grid = [[default_tile for _ in range(w)] for _ in range(h)]
    if isinstance(terrain, list):
        for b in terrain:
            bx = b.get("x", 0)
            by = b.get("y", 0)
            bw = b.get("width", 0)
            bh = b.get("height", 0)
            bt = b.get("tile", default_tile)
            for cy in range(by, min(h, by + bh)):
                for cx in range(bx, min(w, bx + bw)):
                    grid[cy][cx] = bt

    layers["terrain"] = grid
    return grid


def apply_op(level_data, op):
    """Apply a single operation dict to level_data in-place. Returns (success, message)."""
    action = op.get("operation") or op.get("op")
    if not action:
        return False, "Missing 'operation' in command"

    if action == "create_level":
        lid = op.get("id", "new_level")
        name = op.get("name", lid.replace("_", " ").title())
        w = op.get("width", 20)
        h = op.get("height", 18)
        ts = op.get("tileset", "exterior")
        music = op.get("music", "MUSIC_OVERWORLD")

        level_data.clear()
        level_data.update({
            "id": lid,
            "name": name,
            "map": {
                "width": w,
                "height": h,
                "tileset": ts,
                "music": music,
                "map_id": f"MAP_{lid.upper()}"
            },
            "layers": {
                "terrain": []
            },
            "player": {
                "spawn": {
                    "x": w // 2,
                    "y": h // 2,
                    "facing": "DOWN"
                }
            },
            "exits": [],
            "objects": [],
            "regions": []
        })
        return True, f"Created new level '{lid}' ({w}x{h}, {ts})"

    map_info = level_data.setdefault("map", {})
    width = map_info.get("width", 20)
    height = map_info.get("height", 18)

    if action == "paint_rectangle":
        tile = op.get("tile", "floor")
        x = op.get("x", 0)
        y = op.get("y", 0)
        w = op.get("width", 1)
        h = op.get("height", 1)

        # Check if we should append to block list or edit grid
        terrain = level_data.get("layers", {}).get("terrain")
        if isinstance(terrain, list) and (len(terrain) == 0 or isinstance(terrain[0], dict)):
            # Block format
            block = {"x": x, "y": y, "width": w, "height": h, "tile": tile}
            level_data.setdefault("layers", {}).setdefault("terrain", []).append(block)
        else:
            grid = ensure_grid_format(level_data)
            for cy in range(y, min(height, y + h)):
                for cx in range(x, min(width, x + w)):
                    grid[cy][cx] = tile
        return True, f"Painted rectangle {w}x{h} at ({x},{y}) with '{tile}'"

    elif action == "paint_tile":
        tile = op.get("tile", "floor")
        x = op.get("x", 0)
        y = op.get("y", 0)
        grid = ensure_grid_format(level_data)
        if 0 <= x < width and 0 <= y < height:
            grid[y][x] = tile
            return True, f"Painted tile at ({x},{y}) with '{tile}'"
        return False, f"Coordinates ({x},{y}) out of map bounds ({width}x{height})"

    elif action == "place_object":
        oid = op.get("id")
        otype = op.get("type", "npc")
        pos = op.get("position", {"x": 0, "y": 0})
        props = op.get("properties", {})
        if not oid:
            return False, "Missing object 'id'"

        objects = level_data.setdefault("objects", [])
        # Replace if existing or append
        existing = next((o for o in objects if o["id"] == oid), None)
        if existing:
            existing.update({"type": otype, "position": pos, "properties": props})
            return True, f"Updated existing object '{oid}'"
        else:
            objects.append({"id": oid, "type": otype, "position": pos, "properties": props})
            return True, f"Placed object '{oid}' ({otype}) at ({pos['x']},{pos['y']})"

    elif action == "move_object":
        oid = op.get("id")
        pos = op.get("position", {"x": 0, "y": 0})
        objects = level_data.setdefault("objects", [])
        obj = next((o for o in objects if o["id"] == oid), None)
        if not obj:
            return False, f"Object '{oid}' not found"
        obj["position"] = pos
        return True, f"Moved object '{oid}' to ({pos['x']},{pos['y']})"

    elif action == "delete_object":
        oid = op.get("id")
        objects = level_data.setdefault("objects", [])
        level_data["objects"] = [o for o in objects if o["id"] != oid]
        return True, f"Deleted object '{oid}'"

    elif action == "create_exit":
        x = op.get("x", 0)
        y = op.get("y", 0)
        target = op.get("target_scene", "")
        tx = op.get("target_x", 0)
        ty = op.get("target_y", 0)
        direction = op.get("direction", "SOUTH")
        tile_char = op.get("tile_char", ">" if direction in ["NORTH", "EAST"] else "<")

        exits = level_data.setdefault("exits", [])
        # Check existing at same location
        existing = next((e for e in exits if e["x"] == x and e["y"] == y), None)
        new_exit = {
            "x": x, "y": y,
            "target_scene": target,
            "target_x": tx, "target_y": ty,
            "direction": direction,
            "tile_char": tile_char
        }
        if existing:
            existing.update(new_exit)
            return True, f"Updated exit at ({x},{y}) -> {target}"
        else:
            exits.append(new_exit)
            return True, f"Created exit at ({x},{y}) -> {target} ({tx},{ty})"

    elif action == "delete_exit":
        x = op.get("x")
        y = op.get("y")
        exits = level_data.setdefault("exits", [])
        level_data["exits"] = [e for e in exits if not (e["x"] == x and e["y"] == y)]
        return True, f"Deleted exit at ({x},{y})"

    elif action == "set_spawn":
        x = op.get("x", 0)
        y = op.get("y", 0)
        facing = op.get("facing", "DOWN")
        level_data.setdefault("player", {})["spawn"] = {"x": x, "y": y, "facing": facing}
        return True, f"Set player spawn to ({x},{y}) facing {facing}"

    elif action == "describe_region":
        rid = op.get("id")
        bounds = op.get("bounds", {"x": 0, "y": 0, "width": 1, "height": 1})
        desc = op.get("description", "")
        gp = op.get("gameplay", {"purpose": "exploration", "difficulty": 1})
        if not rid:
            return False, "Missing region 'id'"

        regions = level_data.setdefault("regions", [])
        existing = next((r for r in regions if r["id"] == rid), None)
        region_obj = {
            "id": rid,
            "bounds": bounds,
            "description": desc,
            "gameplay": gp
        }
        if existing:
            existing.update(region_obj)
            return True, f"Updated region '{rid}'"
        else:
            regions.append(region_obj)
            return True, f"Created region '{rid}'"

    elif action == "delete_region":
        rid = op.get("id")
        regions = level_data.setdefault("regions", [])
        level_data["regions"] = [r for r in regions if r["id"] != rid]
        return True, f"Deleted region '{rid}'"

    else:
        return False, f"Unknown operation: '{action}'"


def main():
    parser = argparse.ArgumentParser(description="Apply operations to a level JSON file.")
    parser.add_argument("level", help="Target level JSON file")
    parser.add_argument("--op", help="Single JSON operation string")
    parser.add_argument("--ops-file", help="Path to JSON file containing an operation or list of operations")
    parser.add_argument("--create-if-missing", action="store_true", help="Create new level file if target does not exist")
    parser.add_argument("-o", "--output", help="Optional output path (defaults to overwriting input level file)")

    args = parser.parse_args()
    target_path = Path(args.level)

    level_data = {}
    if target_path.exists():
        with open(target_path, "r", encoding="utf-8") as f:
            level_data = json.load(f)
    elif not args.create_if_missing and not (args.op and "create_level" in args.op):
        print(f"Error: Target level '{target_path}' not found. Use --create-if-missing to create.", file=sys.stderr)
        sys.exit(1)

    ops = []
    if args.op:
        try:
            ops.append(json.loads(args.op))
        except Exception as e:
            print(f"Error parsing --op JSON: {e}", file=sys.stderr)
            sys.exit(1)

    if args.ops_file:
        try:
            with open(args.ops_file, "r", encoding="utf-8") as f:
                loaded = json.load(f)
                if isinstance(loaded, list):
                    ops.extend(loaded)
                elif isinstance(loaded, dict):
                    ops.append(loaded)
        except Exception as e:
            print(f"Error reading ops file '{args.ops_file}': {e}", file=sys.stderr)
            sys.exit(1)

    if not ops:
        print("No operations specified. Use --op or --ops-file.", file=sys.stderr)
        sys.exit(1)

    for op in ops:
        ok, msg = apply_op(level_data, op)
        if ok:
            print(f"✓ {msg}")
        else:
            print(f"✗ Failed op: {msg}", file=sys.stderr)
            sys.exit(1)

    # Validate resulting state
    tilesets = load_tilesets()
    valid, errors, warnings, passed = validate_level(level_data, tilesets)
    if not valid:
        print("\nWarning: Resulting level has validation errors:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)

    out_file = Path(args.output) if args.output else target_path
    out_file.parent.mkdir(parents=True, exist_ok=True)
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(level_data, f, indent=2)

    print(f"\nSaved updated level to {out_file}")


if __name__ == "__main__":
    main()
