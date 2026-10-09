#!/usr/bin/env python3
"""Real-boot VRAM ground-truth check for the mode-3 write-drop fix.

Boots the DEBUG ROM in mGBA WITHOUT harness mode, so the real main loop runs
including the vsync() before game_render and the LCD-off boot redraw path.
Compares the real background tilemap ring (0x9800) against the DEBUG-only
WRAM mirror (g_tilemap_mirror), which records exactly what the game wrote.
With the fix every write lands, so VRAM == mirror.

This is a PPU-level property with no semantic representation, so a real VRAM
read is the correct tool (the harness's frame-stepped mGBA runs with vsync
skipped, so it cannot exercise the write-drop path; g_ui_screen_buf only
proves intent).

Checks:
  1. ly-in-vblank: at game_render entry LY must be inside VBlank (the main
     loop waits for LY == 145 before rendering).  Catches a regression that
     moves vsync() out of the loop or before game_update.
  2. bg-mirror-match: every background ring cell in the view window written
     to the mirror must read back identical in VRAM.  Catches dropped writes
     (LCD-off boot redraw reverted, vsync moved, PPU timing changed).

Run inside the Nix dev shell:
    make verify-vram
or: nix develop --command python3 tools/verify_vram.py

Exits non-zero if any check fails.
"""
import os
import pty
import re
import sys
import termios
import tty
import fcntl
import time
import subprocess

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
ROM = os.path.join(ROOT, "build", "kaartenheld_debug.gb")
SYM = os.path.join(ROOT, "build", "kaartenheld_debug.sym")
BG_TM = 0x9800
VIEW_ROWS = 12
VIEW_COLS = 20
TARGET_HITS = 30


def ow_blob_tile_bytes(index):
    """Tile-index bytes of the shared overworld OAM blob, parsed from the
    generated header (make gfx). VRAM tile 100+i must equal blob tile i:
    proves the OAM sprite stream actually loads (regression: sprite
    pointing at font glyphs outside the sprite block renders invisible).
    Returns None when the header cannot be parsed (caller fails loudly)."""
    header = os.path.join(ROOT, "src", "gfx", "enemy_ow_tiles.h")
    try:
        text = open(header).read()
    except OSError:
        return None
    m = re.search(r"/\* tile %d \*/((?:\s*0x[0-9A-Fa-f]{2}, 0x[0-9A-Fa-f]{2},?.*\n){8})" % index, text)
    if not m:
        return None
    pairs = re.findall(r"0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2})", m.group(1))
    if len(pairs) != 8:
        return None
    out = []
    for lo, hi in pairs:
        out += [int(lo, 16), int(hi, 16)]
    return out

failures = []


def check(label, ok, detail=""):
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {label}" + (f" -- {detail}" if detail and not ok else ""))
    if not ok:
        failures.append(label)


def get_symbol(addr_name):
    if not os.path.isfile(SYM):
        raise SystemExit(f"error: no symbol file {SYM} (build the debug ROM first)")
    for line in open(SYM):
        line = line.strip()
        if not line:
            continue
        parts = line.split(":")
        if len(parts) < 2:
            continue
        bank, rest = parts[0], parts[1].split()
        if len(rest) < 2:
            continue
        addr, name = rest[0], rest[1]
        if not re.match(r"^[0-9A-Fa-f]{4}$", addr):
            continue
        if bank == "00" and name == addr_name:
            return int(addr, 16)
    raise SystemExit(f"error: symbol {addr_name} not found in {SYM}")


def main():
    if not os.path.isfile(ROM):
        raise SystemExit("error: ROM not found: %s (make debug first)" % ROM)

    game_render = get_symbol("game_render")
    mirror = get_symbol("g_tilemap_mirror")
    print(f"game_render @ 0x{game_render:04X}, g_tilemap_mirror @ 0x{mirror:04X}")

    master, slave = pty.openpty()
    tty.setraw(master, termios.TCSANOW)
    fl = fcntl.fcntl(master, fcntl.F_GETFL)
    fcntl.fcntl(master, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    proc = subprocess.Popen(["xvfb-run", "--auto-servernum", "mgba",
                             "-C", "audioSync=false", "-C", "videoSync=false",
                             "-d", ROM],
                            stdin=slave, stdout=slave, stderr=slave,
                            close_fds=True, start_new_session=True)
    os.close(slave)

    def drain():
        while True:
            try:
                os.read(master, 4096)
            except (BlockingIOError, OSError):
                break

    def send(c):
        drain()
        os.write(master, (c + "\n").encode())

    def read_until(timeout=15.0, marker=b"> "):
        buf = b""
        start = time.time()
        while time.time() - start < timeout:
            try:
                data = os.read(master, 4096)
                if data:
                    buf += data
                    if marker in buf:
                        return buf
            except (BlockingIOError, OSError):
                time.sleep(0.001)
        return buf

    def cmd(c, timeout=5.0):
        send(c)
        return read_until(timeout=timeout).decode(errors="ignore")

    try:
        deadline = time.time() + 30
        prompt = False
        while time.time() < deadline:
            if b"> " in read_until(timeout=1.0):
                prompt = True
                break
        drain()
        if not prompt:
            raise SystemExit("FAIL: no mGBA debugger prompt")

        out = cmd(f"break 0x{game_render:04X}")
        if "Added breakpoint" not in out:
            raise SystemExit("FAIL: could not arm game_render breakpoint")

        send("c")
        hits = 0
        deadline = time.time() + 10
        while time.time() < deadline and hits < TARGET_HITS:
            out = read_until(timeout=1.0)
            if not out:
                continue
            if re.search(r"Hit breakpoint \d+ at 0x%08X" % game_render,
                         out.decode(errors="ignore")):
                hits += 1
                if hits < TARGET_HITS:
                    send("c")
        if hits < TARGET_HITS:
            raise SystemExit(f"FAIL: game_render only hit {hits}/{TARGET_HITS} times")

        ly_vals = re.findall(r"0x([0-9A-Fa-f]+)", cmd("r/1 0xFF44"))
        ly = int(ly_vals[-1], 16) if len(ly_vals) >= 2 else None
        check("ly-in-vblank (game_render entry)", ly is not None and ly >= 144,
              f"LY={ly}")

        mismatches = []
        skipped = 0
        for y in range(VIEW_ROWS):
            for x in range(VIEW_COLS):
                m = cmd(f"r/1 0x{mirror + y * 32 + x:04X}")
                v = cmd(f"r/1 0x{BG_TM + y * 32 + x:04X}")
                mv = re.findall(r"0x([0-9A-Fa-f]+)", m)
                vv = re.findall(r"0x([0-9A-Fa-f]+)", v)
                if len(mv) >= 2 and len(vv) >= 2:
                    mb = int(mv[-1], 16)
                    vb = int(vv[-1], 16)
                    if mb == 0:
                        # Bulk paths (screen clear, text lines) write VRAM
                        # without mirroring; the mirror only covers the
                        # incremental world-cell + battle paths. Skip those.
                        skipped += 1
                        continue
                    if mb != vb:
                        mismatches.append((y, x, mb, vb))
        detail = "; ".join(f"({y},{x}) mirror={m:02X} vram={v:02X}"
                           for y, x, m, v in mismatches[:8])
        if len(mismatches) > 8:
            detail += f"; ... (+{len(mismatches) - 8} more)"
        check("bg-mirror-match (ring tilemap == writes)", not mismatches, detail)

        # Shared overworld blob tiles must equal the generated header bytes:
        # proves the OAM sprite stream loads real art into sprite-addressable
        # VRAM. Expected bytes come from make gfx output, never hardcoded art.
        # Blob order (battle_compile --ow-coords): bat 0, kobold 2, mimic 4,
        # slime 6, boss 8, spider 12, dog 14, NPC guard 16, mayor 17,
        # merchant 18, wizard 19 (append-only; pinned prefix never moves).
        for blob_idx, vram_base, label in (
                (2, 0x8660, "bat"), (16, 0x8740, "npc_guard"),
                (17, 0x8750, "npc_mayor"), (18, 0x8760, "npc_merchant"),
                (19, 0x8770, "npc_wizard")):
            want = ow_blob_tile_bytes(blob_idx)
            bad = []
            if want is None:
                bad = [(-1, None)]
                detail = f"cannot parse src/gfx/enemy_ow_tiles.h tile {blob_idx}"
            else:
                for i in range(16):
                    v = cmd(f"r/1 0x{vram_base + i:04X}")
                    vv = re.findall(r"0x([0-9A-Fa-f]+)", v)
                    vb = int(vv[-1], 16) if len(vv) >= 2 else None
                    if vb != want[i]:
                        bad.append((i, vb))
                detail = "; ".join(f"byte {i} vram={v:02X}" for i, v in bad[:8])
                if len(bad) > 8:
                    detail += f"; ... (+{len(bad) - 8} more)"
            check(f"ow-blob-tile {label} (VRAM {vram_base:#x} == tile data)",
                  not bad, detail)
    finally:
        proc.kill()

    print()
    if failures:
        print(f"{len(failures)} check(s) failed: {', '.join(failures)}")
        return 1
    print("All VRAM checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
