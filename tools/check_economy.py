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
   Python mirror of s_sc_market_quote/economy_quote_banked must satisfy,
   for every price, every bias, every index 24..48:
   - base == 0  =>  S == 0 (unsellable stays unsellable);
   - base >= 1  =>  1 <= S <= 255 (saturates, never truncates/wraps --
     a wrap once quoted 7g for a 255g card at high inflation);
   - S(base, 32) == base (neutral identity: pre-economy expectations
     hold exactly);
   - S non-decreasing in the index (inflation never discounts).
   C-drift pins (this gate fails if they are edited out): both C
   copies must contain the rounding shift (`>> 5` with `+ 16`) and
   screen_content.c must use the shared ECONOMY_INDEX_MIN/MAX/NEUTRAL
   band (not a hardcoded copy), so a formula or band edit breaks the
   gate even where no scenario reaches.
3. No-arbitrage gate (docs/economy.md §5.1): for every price 0..255,
   every bias -128..127 (folded into the buy base exactly like the C
   sell path), and every material tier 0..4,
   S(cut(base,tier), idx) <= S(base, idx) at every index -- the rarity
   cut sits under the BIASED buy base and the quote is monotonic, so a
   same-visit buy-then-sell can never print gold. Cross-visit profit
   from market movement is market risk, not arbitrage (see §5.1).
   Loot needs no pair here: shops stock curated only
   (shops_compile.py rejects loot-range ids), so loot is never bought.

Stdlib only.  Exit 0 = green, 1 = violation (with expected/actual).
"""
import re
import sys
from functools import lru_cache
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


@lru_cache(maxsize=None)
def c_quote(base, idx):
    """Exact mirror of the C quote (repeated-add multiply, one rounding,
    saturating return).  Memoized: only 255 distinct bases x 25 indices
    exist, so the 8M-iteration arbitrage gate reuses ~6k entries instead
    of re-running the 24-48-step add loop every time."""
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


def c_cut(price, bias, material):
    """Exact mirror of the C rarity cut (screen_content.c
    sc_sell_price): biased base first (same saturating shape as the
    quote bodies, floored at 1), then n = base x (5-material),
    q = n/6, floored at 1."""
    base = c_base(price, bias)
    n = base * (5 - material)
    q = n // 6
    return q if q else 1


def check_no_arbitrage(idx_min, idx_max):
    """No same-visit buy-then-sell pump (docs/economy.md §5.1): for
    every price, every bias, and every material tier,
    S(cut, idx) <= S(base, idx) at every index -- the cut sits under
    the biased buy base and the quote is monotonic, so a same-visit
    resale never exceeds purchase whatever the index."""
    for price in range(0, 256):
        for bias in range(-128, 128):
            base = c_base(price, bias)
            if base == 0:
                continue  # price-0 rows never sell (sc_sell_price: 0)
            for mat in range(0, 5):
                cut = c_cut(price, bias, mat)
                if cut > base:
                    return ("arbitrage: cut %d exceeds base %d "
                            "(price=%d bias=%d mat=%d)"
                            % (cut, base, price, bias, mat))
                for idx in range(idx_min, idx_max + 1):
                    if c_quote(cut, idx) > c_quote(base, idx):
                        return ("arbitrage: S(cut=%d) > S(base=%d) "
                                "at idx=%d (price=%d bias=%d mat=%d)"
                                % (cut, base, idx, price, bias, mat))
    return None


def check_wallet_floor(rows):
    """Expected-wallet floor (docs/economy.md §3.2): every
    economy_expected_for_scene value must cover 1.5x the most expensive
    card stocked in any shop (integer form: expected * 2 >= price * 3).
    Parses the return values out of economy_banked.c, the stock list out
    of shops_content.c, and prices out of g_cards[] -- no hardcoded
    copies to drift."""
    try:
        eco_banked = (REPO / "src" / "game" / "economy_banked.c").read_text(
            encoding="utf-8")
        shops_c = (REPO / "src" / "game" / "shops_content.c").read_text(
            encoding="utf-8")
    except OSError as exc:
        return "floor: cannot read C sources: %s" % exc
    expected = [int(v) for v in re.findall(r"return (\d+);", eco_banked)]
    if not expected:
        return "floor: no expected-wallet returns parsed (shape changed?)"
    shop_init = shops_c.split("g_shops[]", 1)
    stocked = [s for s in re.findall(r"CARD_[A-Z0-9_]+",
               shop_init[1] if len(shop_init) > 1 else shops_c)
               if s != "CARD_NONE"]
    if not stocked:
        return "floor: no stocked cards parsed (shape changed?)"
    price_of = {sym: price for sym, price, _ in rows}
    missing = [s for s in set(stocked) if s not in price_of]
    if missing:
        return ("floor: stocked %s have no g_cards[] row (catalog "
                "changed?)" % sorted(missing))
    price_max = max(price_of[s] for s in set(stocked))
    required = (price_max * 3 + 1) // 2
    for e in expected:
        if e * 2 < price_max * 3:
            return ("floor: expected wallet %d below 1.5x best stock "
                    "(%dg -> need %d)" % (e, price_max, required))
    return None


def check_c_drift():
    """Pin the C copies to the mirrored formula: both quote bodies
    must carry the rounding shift, and the bank-2 display copy must
    use the shared index band (not a hardcoded copy that can drift)."""
    copies = [
        REPO / "src" / "screens" / "screen_content.c",
        REPO / "src" / "game" / "economy_banked.c",
    ]
    for path in copies:
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            return "drift: cannot read %s: %s" % (path, exc)
        if ">> 5" not in text or "+ 16" not in text:
            return ("drift: %s no longer carries the (p + 16) >> 5 "
                    "rounding shift -- update the Python mirror with it"
                    % path.name)
    disp = copies[0].read_text(encoding="utf-8")
    for sym in ("ECONOMY_INDEX_MIN", "ECONOMY_INDEX_MAX",
                "ECONOMY_INDEX_NEUTRAL"):
        if sym not in disp:
            return ("drift: screen_content.c no longer uses %s -- the "
                    "display clamp band can drift from economy.h" % sym)
    if "5 - mat" not in disp:
        return ("drift: screen_content.c sell cut no longer reads "
                "5 - mat -- update c_cut with it")
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
        bad = check_no_arbitrage(idx_min, idx_max)
        if bad:
            fail(bad)
    bad = check_wallet_floor(rows)
    if bad:
        fail(bad)
    bad = check_c_drift()
    if bad:
        fail(bad)

    if FAILURES:
        for f in FAILURES[:20]:
            print("ERROR: %s" % f, file=sys.stderr)
        if len(FAILURES) > 20:
            print("ERROR: ... and %d more" % (len(FAILURES) - 20),
                  file=sys.stderr)
        return 1
    print("economy-check OK: %d card rows sound, quote formula + "
          "no-arbitrage hold over the byte domain" % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
