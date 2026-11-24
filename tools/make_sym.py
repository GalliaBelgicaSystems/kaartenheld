#!/usr/bin/env python3
"""
Convert GBDK/sdld .noi and .map files to RGBDS .sym file for SameBoy debugger symbol lookup.
Automatically resolves truncated map symbol names like _g_scen_l, _g_inp_ma, and _g_snap_b to exact RAM addresses.
"""

import sys
import os
import re

def convert_symbols(noi_path, map_path, sym_path):
    symbols = {}
    collisions = []
    alias_seen = {}

    def _alias(key, addr, source):
        # Truncation aliases fill ONLY gaps: an exact entry (from .noi
        # or .map) always wins, so an 8-char-truncated map form can never
        # clobber a short exact name (e.g. _g_game_c, the truncated form
        # of _g_game_completed, once overwrote g_game, sending every host
        # tool reading g_game to the flag byte -- AGENTS.md 52.30; the
        # same latent off-by-one hit g_scen_load via _g_scen_l).
        # The alias_seen check comes first: a previous ALIAS must not
        # mask a conflicting one (only an EXACT entry wins silently).
        # Two different addresses aliasing one key with no exact entry
        # is ambiguous: fail loudly.
        if key in alias_seen:
            if alias_seen[key] != addr:
                collisions.append(
                    f"{source} -> {key}: "
                    f"alias {addr:#06x} clashes with earlier "
                    f"{alias_seen[key]:#06x} (no exact entry)")
            return
        if key in symbols:
            return
        alias_seen[key] = addr
        symbols[key] = addr

    # Parse .noi file if present
    if os.path.exists(noi_path):
        with open(noi_path, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith("DEF "):
                    parts = line.split()
                    if len(parts) >= 3:
                        name = parts[1]
                        try:
                            addr = int(parts[2], 16)
                            symbols[name] = addr
                            if name.startswith("_"):
                                symbols[name[1:]] = addr
                        except ValueError:
                            pass

    # Parse .map file for DATA/BSS/INITIALIZED symbols
    if os.path.exists(map_path):
        with open(map_path, 'r') as f:
            for line in f:
                matches = re.findall(r'([0-9A-Fa-f]{8})\s+([._A-Za-z0-9]+)', line)
                for addr_str, name in matches:
                    try:
                        addr = int(addr_str, 16)
                        if addr > 0:
                            symbols[name] = addr
                            if name.startswith("_"):
                                symbols[name[1:]] = addr
                            
                            # Map unique truncated 8-character GBDK symbol names
                            # through _alias (clash-guarded: a truncated form
                            # of a longer full name must never steal a short
                            # exact name's address).
                            if name.startswith("_g_snap_b") or name.startswith("g_snap_b"):
                                _alias("g_snap_buf", addr, name)
                                _alias("_g_snap_buf", addr, name)
                            elif name.startswith("_g_scen_l") or name.startswith("g_scen_l"):
                                _alias("g_scen_load", addr, name)
                                _alias("_g_scen_load", addr, name)
                            elif name.startswith("_g_inp_m") or name.startswith("g_inp_m"):
                                _alias("g_inp_mask", addr, name)
                                _alias("_g_inp_mask", addr, name)
                            # NOTE: exact match only (no startswith): the
                            # 8-char-truncated map form of _g_game_completed
                            # is _g_game_c, which a prefix rule would mistake
                            # for g_game and clobber its address (every host
                            # tool reading g_game would land on the flag
                            # byte instead). The exact _g_game entry exists
                            # in both .noi and .map, so nothing is lost.
                            elif name == "_g_game" or name == "g_game":
                                _alias("g_game", addr, name)
                                _alias("_g_game", addr, name)
                            elif name.startswith("_g_ui_sc") or name.startswith("g_ui_sc"):
                                _alias("g_ui_screen_buf", addr, name)
                                _alias("_g_ui_screen_buf", addr, name)
                    except ValueError:
                        pass

    if collisions:
        for c in collisions:
            print(f"make_sym: ERROR: {c}", file=sys.stderr)
        sys.exit(1)

    with open(sym_path, 'w') as f_out:
        for name, addr in sorted(symbols.items()):
            bank = 0
            addr_hex = f"{addr:04X}"
            f_out.write(f"{bank:02X}:{addr_hex} {name}\n")

if __name__ == "__main__":
    noi = sys.argv[1] if len(sys.argv) > 1 else "build/kaartenheld_debug.noi"
    sym = sys.argv[2] if len(sys.argv) > 2 else "build/kaartenheld_debug.sym"
    map_p = noi.replace(".noi", ".map")
    convert_symbols(noi, map_p, sym)
