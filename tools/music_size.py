#!/usr/bin/env python3
"""Fast pre-link feedback for tracker songs: ROM bytes + playback length.

Rebuilding the ROM to answer "will this song fit?" / "how long is it?"
costs a full compile+link per iteration.  This compiles each
generated/music/*.c to a scratch object (same CC/INCLUDES as the Makefile
music rule, passed via the environment) and sums its code areas, plus
parses order_cnt/tempo for the playback length that chaining constants
(e.g. the mimic intro -> loop swap) are derived from.

Usage:
    CC="lcc" INCLUDES="..." python3 tools/music_size.py [song ...]
    make music-size [SONG=mimic_intro]

Song args are generated-file stems (mimic_intro); default is all songs.
Exit non-zero if any song's bank total exceeds 16 KB.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GEN_MUSIC = REPO_ROOT / "generated" / "music"

BANK_BYTES = 16 * 1024
TRACKER_HZ = 64      # hUGE_dosound rate (256 Hz timer / 4)
ROWS_PER_ORDER = 64


def fail(msg):
    print(f"music_size.py: error: {msg}", file=sys.stderr)
    sys.exit(2)


def parse_song(path):
    text = path.read_text(encoding="utf-8")
    m = re.search(r"#pragma bank (\d+)", text)
    bank = int(m.group(1)) if m else 0
    m = re.search(r"static const unsigned char order_cnt = (\d+);", text)
    if not m:
        fail(f"{path.name}: no order_cnt")
    order_cnt = int(m.group(1))
    m = re.search(r"order1\[\] = \{([^}]*)\};", text, re.S)
    orders = len(re.findall(r"P\d+", m.group(1))) if m else order_cnt // 2
    if orders != order_cnt // 2:
        print(f"music_size.py: warning: {path.name}: order1 has {orders} "
              f"orders but order_cnt is {order_cnt}", file=sys.stderr)
    m = re.search(r"const hUGESong_t song_\w+ = \{(\d+),", text)
    if not m:
        fail(f"{path.name}: no song descriptor tempo")
    tempo = int(m.group(1))
    tracker_ticks = orders * ROWS_PER_ORDER * tempo
    return {
        "name": path.stem,
        "bank": bank,
        "orders": orders,
        "tempo": tempo,
        "tracker_ticks": tracker_ticks,
        "seconds": tracker_ticks / TRACKER_HZ,
        "isr_ticks": tracker_ticks * 4,
    }


def object_code_size(cc, includes, path):
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "song.o")
        try:
            subprocess.run([cc, "-c"] + includes.split() +
                           ["-o", obj, str(path)],
                           check=True, capture_output=True, text=True)
        except FileNotFoundError:
            fail(f"compiler '{cc}' not found on PATH "
                 "(run inside `nix develop`)")
        except subprocess.CalledProcessError as e:
            fail(f"compiling {path.name} failed:\n{e.stderr}")
        total = 0
        areas = {}
        with open(obj, "rb") as f:
            content = f.read().decode("ascii", errors="replace")
        # SDCC .rel area records: `A <name> size <hex> flags ...`
        for m in re.finditer(r"^A (\S+) size ([0-9A-Fa-f]+)",
                             content, re.M):
            name, size = m.group(1), int(m.group(2), 16)
            if name.startswith("_CODE") or name == "_HOME":
                areas[name] = areas.get(name, 0) + size
                total += size
        return total, areas


def main():
    cc = os.environ.get("CC", "lcc")
    includes = os.environ.get("INCLUDES", "")
    if not shutil.which(cc):
        fail(f"compiler '{cc}' not found on PATH "
             "(run inside `nix develop`)")
    if not GEN_MUSIC.is_dir():
        fail(f"{GEN_MUSIC} missing (run `make music` first)")

    want = sys.argv[1:]
    paths = sorted(GEN_MUSIC.glob("*.c"))
    if want:
        paths = [p for p in paths if p.stem in want]
        missing = set(want) - {p.stem for p in paths}
        if missing:
            fail(f"unknown song(s): {sorted(missing)}")

    bank_totals = {}
    rows = []
    for path in paths:
        info = parse_song(path)
        size, _ = object_code_size(cc, includes, path)
        info["rom_bytes"] = size
        bank_totals[info["bank"]] = bank_totals.get(info["bank"], 0) + size
        rows.append(info)

    print(f"{'song':<20} {'bank':<4} {'rom':>6} "
          f"{'orders':>6} {'tempo':>5} {'ticks':>6} {'secs':>6} {'isr':>6}")
    for r in rows:
        print(f"{r['name']:<20} {r['bank']:<4} {r['rom_bytes']:>6} "
              f"{r['orders']:>6} {r['tempo']:>5} {r['tracker_ticks']:>6} "
              f"{r['seconds']:>6.1f} {r['isr_ticks']:>6}")
    print()
    failed = False
    for bank in sorted(bank_totals):
        total = bank_totals[bank]
        status = "OK" if total < BANK_BYTES else "OVERFLOW"
        if total >= BANK_BYTES:
            failed = True
        print(f"bank {bank}: songs total {total} B "
              f"(headroom vs 16 KB: {BANK_BYTES - total} B) [{status}] "
              f"-- songs only; use `make memmap` for the full-bank budget")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
