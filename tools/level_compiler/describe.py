#!/usr/bin/env python3
"""
describe.py - Semantic LLM representation generator for level JSON.

Converts machine-level level definitions into LLM-native summaries (JSON or Markdown)
so that AI agents can reason about spatial connectivity, gameplay intent,
regions, and entities without parsing raw tile arrays.

Usage:
    python3 tools/level_compiler/describe.py levels/forest.json
    python3 tools/level_compiler/describe.py levels/forest.json --format markdown
    python3 tools/level_compiler/describe.py levels/forest.json --format json -o forest_summary.json
"""

import sys
import os
import json
import argparse
from pathlib import Path

def describe_level_dict(level_data):
    """Generate high-level semantic dictionary for an LLM."""
    level_id = level_data.get("id", "unnamed")
    name = level_data.get("name", level_id.title())
    map_info = level_data.get("map", {})
    width = map_info.get("width", 0)
    height = map_info.get("height", 0)
    tileset = map_info.get("tileset", "exterior")
    music = map_info.get("music", "MUSIC_OVERWORLD")

    spawn = level_data.get("player", {}).get("spawn", {})
    player_start = [spawn.get("x", 0), spawn.get("y", 0)]
    facing = spawn.get("facing", "DOWN")

    connections = []
    for e in level_data.get("exits", []):
        direction = e.get("direction", "exit").lower()
        target = e.get("target_scene", "unknown")
        connections.append(f"{direction} (at {e.get('x')},{e.get('y')}) -> {target} (spawn {e.get('target_x')},{e.get('target_y')})")

    regions = []
    for r in level_data.get("regions", []):
        reg_info = {
            "name": r.get("id"),
            "bounds": [r.get("bounds", {}).get("x"), r.get("bounds", {}).get("y"),
                       r.get("bounds", {}).get("width"), r.get("bounds", {}).get("height")],
            "purpose": r.get("gameplay", {}).get("purpose", "general"),
            "difficulty": r.get("gameplay", {}).get("difficulty", 1),
            "description": r.get("description", "")
        }
        regions.append(reg_info)

    objects = []
    for obj in level_data.get("objects", []):
        objects.append({
            "id": obj.get("id"),
            "type": obj.get("type"),
            "position": [obj.get("position", {}).get("x"), obj.get("position", {}).get("y")],
            "properties": obj.get("properties", {})
        })

    return {
        "level": level_id,
        "name": name,
        "size": f"{width}x{height}",
        "tileset": tileset,
        "music": music,
        "player_start": player_start,
        "player_facing": facing,
        "connections": connections,
        "regions": regions,
        "objects": objects
    }


def format_markdown(desc):
    """Format description as a human and LLM-friendly Markdown document."""
    lines = []
    lines.append(f"# {desc['name'].upper()} (`{desc['level']}`)")
    lines.append(f"\n**Size:** {desc['size']} tiles | **Tileset:** `{desc['tileset']}` | **Music:** `{desc['music']}`")
    lines.append(f"**Player Start:** `({desc['player_start'][0]}, {desc['player_start'][1]})` facing {desc['player_facing']}")

    lines.append("\n## Connections / Exits")
    if desc["connections"]:
        for c in desc["connections"]:
            lines.append(f"- {c}")
    else:
        lines.append("- None (isolated scene)")

    lines.append("\n## Regions & Design Intent")
    if desc["regions"]:
        for r in desc["regions"]:
            b = r["bounds"]
            lines.append(f"### {r['name'].replace('_', ' ').title()} (`{r['name']}`)")
            lines.append(f"- **Bounds:** [{b[0]}, {b[1]}] to [{b[0]+b[2]}, {b[1]+b[3]}] ({b[2]}x{b[3]})")
            lines.append(f"- **Purpose:** `{r['purpose']}` (Difficulty: {r['difficulty']})")
            if r["description"]:
                lines.append(f"- **Description:** {r['description']}")
    else:
        lines.append("- No explicit regions defined.")

    lines.append("\n## Objects & Entities")
    if desc["objects"]:
        for o in desc["objects"]:
            lines.append(f"- **[{o['type'].upper()}]** `{o['id']}` at `({o['position'][0]}, {o['position'][1]})` {o['properties']}")
    else:
        lines.append("- No objects placed.")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Describe a level JSON semantically for LLM consumption.")
    parser.add_argument("level", help="Path to level JSON file")
    parser.add_argument("--format", choices=["markdown", "json"], default="markdown", help="Output format (default: markdown)")
    parser.add_argument("-o", "--output", help="Optional output file path")

    args = parser.parse_args()

    with open(args.level, "r", encoding="utf-8") as f:
        level_data = json.load(f)

    desc = describe_level_dict(level_data)

    if args.format == "json":
        out_text = json.dumps(desc, indent=2)
    else:
        out_text = format_markdown(desc)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(out_text)
        print(f"Saved description to {args.output}")
    else:
        print(out_text)


if __name__ == "__main__":
    main()
