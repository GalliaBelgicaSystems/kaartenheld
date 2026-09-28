#!/usr/bin/env python3
"""Static worst-case stack budget for the debug ROM (host-only, seconds).

Background: under the debug ROM the battle combo chain once carried ~70 B
of scratch on the banked-call stack. SP descended into WRAM globals and
pushes landed in input state, ghost-firing a menu confirm (AGENTS.md
52.24). A 1 B layout shift chose the victim, so the SameBoy harness could
not see it. The fix keeps scratch in maps and shared WRAM.

Gate: compute the worst-case stack depth from the compiled debug .lst
files (prologue `add sp,#-N` per function + a documented per-call
overhead) over the battle call graph, and FAIL if it exceeds the
committed BASELINE below. This is a ratchet, not an absolute budget:
static per-edge overhead cannot be exact, so the absolute number is
reported informationally while the gate pins regression (any frame
growth anywhere in the battle chain fails loudly and forces a conscious
baseline bump with reviewer eyes on it). The behavioral backstop is
game_over_quit in the harness suite, which caught the original overflow.

The graph is exact by construction: gameplay dispatch uses no function
pointers (AGENTS.md 52.1), only direct calls plus the WRAM banked-call
trampoline, whose targets resolve from the staged address (`ld hl,#_sym`
or the `#<(_sym)` / `#>(_sym)` byte immediates) right before each
`call _banked_call_run`.

Known gap: the 256 Hz timer ISR can land on the deepest main point on
real hardware (the harness skips it). The ISR chain (partly asm, no
.lst) is not modeled.

Run inside the Nix dev shell:
    make stack-budget
or: nix develop --command python3 tools/stack_budget.py

Exits non-zero if any assertion fails.
"""

import glob
import os
import re
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(TOOLS, "..", "build")
MAP = os.path.join(BUILD, "kaartenheld_debug.map")
LST_GLOB = os.path.join(BUILD, "debug", "**", "*.lst")

# Per nesting level above the parsed frames: 2 B return address plus ~1 B
# of saved registers/argument shuffling around SDCC calls. Deliberately
# approximate; the ratchet below absorbs model error (only DELTAS gate).
CALL_OVERHEAD = 3

# Pinned worst-case depth in bytes, measured on the post-stack-fix
# tree (perf-spree). This is a ratchet, not an absolute budget: the
# per-edge overhead above is approximate, so only DELTAS gate. Bump the
# constant only with reviewer eyes on the responsible frame growth, and
# re-run game_over_quit plus the sentinels (AGENTS.md 52.19/52.24).
BASELINE = 158

# Fixed main-loop prefix live under every battle chain.
PREFIX = ["main", "game_update", "screen_update"]

# Chain roots into battle.
ROOTS = [
    "battle_screen_update",
    "battle_screen_render",
    "start_battle_from_world",
    "battle_update",
]

failures = []


def check(label, expected, actual):
    ok = actual == expected
    print(f"  [{'OK ' if ok else 'FAIL'}] {label}: expected {expected}, got {actual}")
    if not ok:
        failures.append(label)


def globals_ceiling():
    """First non-globals byte (s__BSS) from the debug link map."""
    with open(MAP, errors="replace") as f:
        txt = f.read()
    m = re.search(r"([0-9A-Fa-f]{4,8})\s+s__BSS\b", txt)
    if not m:
        raise RuntimeError(f"s__BSS not found in {MAP}")
    return int(m.group(1), 16)


def parse_units():
    """Map function name -> {frame, calls, staged} from debug .lst files.

    Frame = first prologue `add sp,#-N`. Calls = direct `call`/`jp`
    targets. Staged = symbols loaded for the banked-call trampoline.
    """
    units = {}
    for lst in glob.glob(LST_GLOB, recursive=True):
        try:
            with open(lst, errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        for m in re.finditer(r"; Function (\w+)", text):
            name = m.group(1)
            if name in units:
                continue
            body = text[m.end():]
            nxt = re.search(r"; Function \w+", body)
            if nxt:
                body = body[:nxt.start()]
            frame = 0
            a = re.search(r"add\s+sp,\s*#-(\d+|0x[0-9A-Fa-f]+)", body)
            if a:
                raw = a.group(1)
                frame = int(raw, 16) if raw.startswith("0x") else int(raw)
            calls = re.findall(r"(?:call|jp)\s+(_[A-Za-z0-9_]+)", body)
            staged = re.findall(r"ld\s+hl,\s*#(_[A-Za-z0-9_]+)", body)
            staged += [sym for _, sym in
                       re.findall(r"#([<>])\((_[A-Za-z0-9_]+)\)", body)]
            units[name] = {"frame": frame, "calls": calls,
                           "staged": staged}
    return units


def resolve_call(name, units):
    """(callee, overhead) edges for one function, trampoline resolved."""
    u = units.get(name)
    if not u:
        return []
    edges = []
    has_tramp = "_banked_call_run" in u["calls"]
    for target in u["calls"]:
        if target == "_banked_call_run":
            continue
        base = target[1:] if target.startswith("_") else target
        if base not in units:
            # Unknown externals (GBDK lib, asm drivers): assume a small
            # frame rather than zero (conservative for a budget gate).
            edges.append((target, 6, CALL_OVERHEAD))
            continue
        edges.append((target, 0, CALL_OVERHEAD))
    if has_tramp:
        for sym in u["staged"]:
            base = sym[1:] if sym.startswith("_") else sym
            if base in units and "_banked" in base:
                edges.append((sym, 0, CALL_OVERHEAD + 4))
    return edges


def worst_depth(entry, units, seen=None):
    """Worst stack use from entry (frames + overhead), DFS with cycle guard."""
    if seen is None:
        seen = set()
    if entry in seen:
        return 0
    seen = seen | {entry}
    name = entry[1:] if entry.startswith("_") else entry
    u = units.get(name)
    if not u:
        return 0
    peak = u["frame"]
    for target, extra, over in resolve_call(name, units):
        sub = worst_depth(target, units, seen)
        peak = max(peak, u["frame"] + over + extra + sub)
    return peak


def worst_path(entry, units, seen=None):
    """Argmax path for worst_depth (diagnostics)."""
    if seen is None:
        seen = set()
    if entry in seen:
        return (0, [entry + " (cycle)"])
    seen = seen | {entry}
    name = entry[1:] if entry.startswith("_") else entry
    u = units.get(name)
    if not u:
        return (0, [entry + " (leaf)"])
    best = (u["frame"], [entry])
    for target, extra, over in resolve_call(name, units):
        sub, path = worst_path(target, units, seen)
        total = u["frame"] + over + extra + sub
        if total > best[0]:
            best = (total, [entry] + path)
    return best


def main():
    ceiling = globals_ceiling()
    budget = 0xE000 - ceiling
    print(f"globals ceiling s__BSS=0x{ceiling:04X} "
          f"(heap+stack budget above: {budget} B)")
    units = parse_units()
    print(f"parsed {len(units)} functions from debug .lst")
    prefix = 0
    for f in PREFIX:
        prefix += units.get(f, {}).get("frame", 0) + CALL_OVERHEAD
    print(f"main-loop prefix: {prefix} B")
    worst = 0
    at = "-"
    detail = ""
    for root in ROOTS:
        d = prefix + worst_depth("_" + root, units)
        print(f"    chain {root}: {d} B")
        if d > worst:
            worst = d
            at = root
    total, path = worst_path("_" + at, units)
    print(f"    worst={worst} B via {at}: {' -> '.join(path)}")
    print(f"    floor=0x{0xE000 - worst:04X} margin={budget - worst} B "
          f"(informational: model is approximate, deltas gate)")
    check(f"worst depth <= baseline {BASELINE} B", True, worst <= BASELINE)
    if failures:
        print(f"\nSTACK BUDGET FAILED: {len(failures)} check(s)")
        return 1
    print("\nSTACK BUDGET OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
