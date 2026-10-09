#!/usr/bin/env python3
import os
import sys

ROM_DEBUG = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "build", "kaartenheld_debug.gb")
SYM = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "build", "kaartenheld_debug.sym")

def get_symbol(name):
    for line in open(SYM):
        if line.strip().endswith(name) or line.strip().endswith(f"_{name}"):
            parts = line.strip().split()
            addr_str = parts[0].split(":")[-1]
            return int(addr_str, 16)
    return None

from pyboy import PyBoy
from pyboy.utils import WindowEvent

pb = PyBoy(ROM_DEBUG, window="null")

hm_addr = get_symbol("g_harness_mode")
game_addr = get_symbol("g_game")
bp_addr = get_symbol("g_boot_phase")

if hm_addr:
    pb.memory[hm_addr] = 1

# Wait for boot completion (g_boot_phase == 4)
for t in range(200):
    pb.tick()
    if bp_addr and pb.memory[bp_addr] == 4:
        print(f"Boot complete at tick {t}. LCDC = 0x{pb.memory[0xFF40]:02X}")
        break

# Position hero at (10, 5) on Field
pb.memory[game_addr + 5] = 10
pb.memory[game_addr + 6] = 5

pb.send_input(WindowEvent.PRESS_ARROW_RIGHT)

prev_scx = pb.memory[0xFF43]
prev_vram = bytes([pb.memory[0x9800 + i] for i in range(32 * 18)])

scx_changes = 0
zero_vram_writes = 0
two_vram_writes = 0
other_vram_writes = 0
lcd_off_count = 0

for t in range(240):
    pb.tick()
    lcdc = pb.memory[0xFF40]
    if (lcdc & 0x80) == 0:
        lcd_off_count += 1

    scx = pb.memory[0xFF43]
    curr_vram = bytes([pb.memory[0x9800 + i] for i in range(32 * 18)])
    diffs = [i for i in range(32 * 18) if curr_vram[i] != prev_vram[i]]

    if scx != prev_scx:
        scx_changes += 1

    if len(diffs) == 0:
        zero_vram_writes += 1
    elif len(diffs) == 2:
        two_vram_writes += 1
    else:
        other_vram_writes += 1

    prev_scx = scx
    prev_vram = curr_vram

print(f"scx_changes = {scx_changes}")
print(f"zero_vram_writes = {zero_vram_writes}, two_vram_writes = {two_vram_writes}, other_vram_writes = {other_vram_writes}")
print(f"lcd_off_count during movement = {lcd_off_count}")
