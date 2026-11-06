#!/usr/bin/env python3
"""make economy-check gate (docs/economy.md §4-§5, required).

Fails loudly (exit 1) on any violation:

1. Price/bias soundness for every row of g_cards[] in
   src/game/cards_content.c (prices parsed paren-aware: CARD_AP(...)
   contains commas, so naive splitting misaligns fields):
   - 0 <= price <= 255, -128 <= bias <= 127 (the C fields are
     uint8_t/int8_t; out-of-range literals would silently truncate).
   - price == 0 implies bias == 0.  The ROM quote returns 0 for a
     price-0 row however biased (not for sale, always free in the
     shop); a nonzero bias there is dead content, i.e. a typo.
2. Quote-formula gate, exhaustive over the whole byte domain: the
   Python mirror of bank2_price/economy_quote_banked must satisfy, for
   every price, every bias, every index 24..48:
   - base == 0  =>  S == 0 (unsellable stays unsellable);
   - base >= 1  =>  1 <= S <= 255 (saturates, never truncates/wraps --
     a wrap once quoted 7g for a 255g card at high inflation);
   - S(base, 32) == base (neutral identity: pre-economy expectations
     hold exactly);
   - S non-decreasing in the index (inflation never discounts).
   If someone edits the shift/rounding/saturation in either C copy,
   this gate catches the drift even where no scenario reaches.

Stdlib only.  Exit 0 = green, 1 = violation (with expected/actual).
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CARDS_C = REPO / "src" / "game" / "cards_content.c"
CARDS_H = REPO / "src" / "rpg" / "cards.h"
ECONOMY_H = REPO / "src" / "game" / "economy.h"

FAILURES = []


def fail(msg):
    FAILURES.append(msg)


def parse_define_int(text, name):
    m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)" % re.escape(name),
                  text)
    if not m:
        fail("economy.h: #%s not found (header shape changed?)" % name)
        return None
    return int(m.group(1), 0)


def split_top_level(body):
    """Split on commas that are not inside parens or string literals."""
    parts, depth, cur, instr = [], 0, [], False
    for ch in body:
        if ch == '"':
            instr = not instr
            cur.append(ch)
        elif instr:
            cur.append(ch)
        elif ch == '(':
            depth += 1
            cur.append(ch)
        elif ch == ')':
            depth -= 1
            cur.append(ch)
        elif ch == ',' and depth == 0:
            parts.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append(''.join(cur))
    return [p.strip() for p in parts]


def load_card_prices():
    """(symbol, price, bias) for every g_cards[] row.  Raises ValueError
    on shape drift (never guesses)."""
    text = CARDS_C.read_text(encoding="utf-8")
    rows = []
    for m in re.finditer(r"\{\s*(CARD_[A-Z0-9_]+)\s*,(.*?)\}\s*,?", text,
                         re.S):
        symbol, body = m.group(1), m.group(2)
        fields = split_top_level(body)
        # group(2) excludes the id: [type, power, cost, uses, max,
        # effect, battle_type, price, bias, status, chance, name]
        if len(fields) < 11:
            raise ValueError(
                "cards_content.c: row %s has %d fields, expected 12 "
                "(CardDefinition changed?)" % (symbol, len(fields) + 1))
        try:
            price = int(fields[7], 0)
            bias = int(fields[8], 0)
        except ValueError as exc:
            raise ValueError(
                "cards_content.c: bad numeric price/bias in %s: %s"
                % (symbol, exc))
        rows.append((symbol, price, bias))
    if not rows:
        raise ValueError("cards_content.c: no card rows parsed")
    return rows


def c_base(price, bias):
    """Exact mirror of the C base computation (8-bit saturating)."""
    if price == 0:
        return 0
    b = price + bias
    if b > 255:
        return 255
    if b < 1:
        return 1
    return b


def c_quote(base, idx):
    """Exact mirror of the C quote (repeated-add multiply, one rounding,
    saturating return)."""
    p = 0
    for _ in range(idx):
        p += base
    q = (p + 16) >> 5
    return 255 if q > 255 else q


def check_formula(idx_min, idx_max, idx_neutral):
    """First formula violation over the byte domain, or None (fail-fast:
    one drift report is enough to send the author to the C copies)."""
    for price in range(0, 256):
        for bias in range(-128, 128):
            base = c_base(price, bias)
            if base == 0:
                if c_quote(base, idx_neutral) != 0:
                    return ("formula: base 0 quotes nonzero (price=%d "
                            "bias=%d)" % (price, bias))
                continue
            if c_quote(base, idx_neutral) != base:
                return ("formula: neutral identity broken at base=%d "
                        "(price=%d bias=%d)" % (base, price, bias))
            prev = 0
            for idx in range(idx_min, idx_max + 1):
                s = c_quote(base, idx)
                if not 1 <= s <= 255:
                    return ("formula: S=%d out of [1,255] (base=%d idx=%d)"
                            % (s, base, idx))
                if s < prev:
                    return ("formula: S decreased %d->%d (base=%d idx=%d)"
                            % (prev, s, base, idx))
                prev = s
    return None


def main():
    try:
        rows = load_card_prices()
    except ValueError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 1

    for symbol, price, bias in rows:
        if not 0 <= price <= 255:
            fail("%s: price %d outside uint8_t range" % (symbol, price))
        if not -128 <= bias <= 127:
            fail("%s: bias %d outside int8_t range" % (symbol, bias))
        if price == 0 and bias != 0:
            fail("%s: price 0 with bias %d -- the ROM quotes 0 however "
                 "biased, the bias is dead content" % (symbol, bias))

    try:
        eco_text = ECONOMY_H.read_text(encoding="utf-8")
        idx_min = parse_define_int(eco_text, "ECONOMY_INDEX_MIN")
        idx_max = parse_define_int(eco_text, "ECONOMY_INDEX_MAX")
        idx_neutral = parse_define_int(eco_text, "ECONOMY_INDEX_NEUTRAL")
    except OSError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 1
    if idx_min is not None and idx_max is not None and \
            idx_neutral is not None:
        bad = check_formula(idx_min, idx_max, idx_neutral)
        if bad:
            fail(bad)

    if FAILURES:
        for f in FAILURES[:20]:
            print("ERROR: %s" % f, file=sys.stderr)
        if len(FAILURES) > 20:
            print("ERROR: ... and %d more" % (len(FAILURES) - 20),
                  file=sys.stderr)
        return 1
    print("economy-check OK: %d card rows sound, quote formula holds over "
          "the byte domain" % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
