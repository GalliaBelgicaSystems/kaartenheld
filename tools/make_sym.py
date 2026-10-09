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
                            if name.startswith("_g_snap_b") or name.startswith("g_snap_b"):
                                symbols["g_snap_buf"] = addr
                                symbols["_g_snap_buf"] = addr
                            elif name.startswith("_g_scen_l") or name.startswith("g_scen_l"):
                                symbols["g_scen_load"] = addr
                                symbols["_g_scen_load"] = addr
                            elif name.startswith("_g_inp_m") or name.startswith("g_inp_m"):
                                symbols["g_inp_mask"] = addr
                                symbols["_g_inp_mask"] = addr
                            elif name.startswith("_g_game") or name.startswith("g_game"):
                                symbols["g_game"] = addr
                                symbols["_g_game"] = addr
                            elif name.startswith("_g_ui_sc") or name.startswith("g_ui_sc"):
                                symbols["g_ui_screen_buf"] = addr
                                symbols["_g_ui_screen_buf"] = addr
                    except ValueError:
                        pass

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
