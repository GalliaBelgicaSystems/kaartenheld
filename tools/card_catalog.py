#!/usr/bin/env python3
"""card_catalog.py - Read the hand-written card catalog (C) for tooling.

`src/game/cards_content.c` remains the authored source of the card table
(SDCC partial initializers, inline names, etc.); this module parses it so
the level editor (via `--json`) and the content compilers can list card
symbols/names/prices without a second source of truth.

Row shape (src/rpg/cards.h CardDefinition):
    { CARD_ID, type, power, cost, uses, max_copies, effect, battle_type,
      price, status_id, status_chance, "NAME" }

Usage:
    python3 tools/card_catalog.py --json
"""
import argparse
import json
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
CARDS_C = REPO_ROOT / "src" / "game" / "cards_content.c"
GAME_IDS_H = REPO_ROOT / "src" / "game" / "game_ids.h"

_ROW_RE = re.compile(r"\{\s*(CARD_[A-Z0-9_]+)\s*,(.*?)\}\s*,?", re.S)


def load_cards():
    """Parse g_cards[] -> list of dicts (symbol, name, price, type, power).
    Raises ValueError if the table shape changed (never guesses)."""
    text = CARDS_C.read_text(encoding="utf-8")
    rows = []
    for m in _ROW_RE.finditer(text):
        symbol = m.group(1)
        fields = [f.strip() for f in m.group(2).split(",")]
        if len(fields) < 11:
            raise ValueError(
                f"cards_content.c: row {symbol} has {len(fields) + 1} fields, "
                "expected 12 (CardDefinition changed?)")
        try:
            price = int(fields[7])
            power = int(fields[1])
        except ValueError as exc:
            raise ValueError(f"cards_content.c: bad numeric field in {symbol}: {exc}")
        name = fields[10].strip()
        if name.startswith('"') and name.endswith('"'):
            name = name[1:-1]
        rows.append({
            "symbol": symbol,
            "name": name,
            "price": price,
            "type": fields[0],
            "power": power,
        })
    if not rows:
        raise ValueError("cards_content.c: no card rows parsed")
    return rows


def card_symbols():
    """The set of CARD_* ids declared in game_ids.h (the deckable range;
    loot-range ids are synthesized and never stocked in a shop)."""
    text = GAME_IDS_H.read_text(encoding="utf-8")
    return set(re.findall(r"#define\s+(CARD_[A-Z0-9_]+)\s", text))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Read the card catalog.")
    parser.add_argument("--json", action="store_true", help="emit JSON")
    args = parser.parse_args(argv)
    cards = load_cards()
    if args.json:
        json.dump(cards, sys.stdout)
        sys.stdout.write("\n")
    else:
        for c in cards:
            print(f"{c['symbol']:<22} {c['name']:<8} price={c['price']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
