#!/usr/bin/env python3
"""Print a reproducible memory budget for the debug ROM from its linker map.

Usage:
    python3 tools/memmap.py build/kaartenheld_debug.map

Reports the fixed code area (_CODE), the non-bankable _HOME area (which must
stay below 0x8000 on MBC5 -- CPU addresses >= 0x8000 alias VRAM), and the WRAM
_DATA area.  Exits non-zero if any documented invariant is violated.
"""
import os
import re
import sys


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    areas = {}
    with open(sys.argv[1]) as f:
        for line in f:
            m = re.match(r"\s*(\w+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+=\s+(\d+)\.\s+bytes", line)
            if not m:
                continue
            name, start, size, nbytes = m.groups()
            if name in ("_CODE", "_HOME", "_INITIALIZER", "_DATA"):
                areas[name] = (int(start, 16), int(size, 16), int(nbytes))
            elif re.fullmatch(r"_CODE_[0-9]+", name):
                # Switchable ROM bank content: each bank is 16 KB.  An
                # overfull bank makes rgblink spill into the next bank and
                # silently corrupt it (bank 6 music overflowed ~1 KB into
                # bank 7's icon table when song_castle was added) -- fail
                # here instead.  Tracked separately so the fixed-area logic
                # above is untouched.
                areas[name] = (int(start, 16), int(size, 16), int(nbytes))

    # The RAM-resident banked trampolines (crt0.s) are byte-copied at boot
    # into fixed-size WRAM buffers (g_banked_tramp[64], g_banked_call_tramp[64]
    # in src/core/banked.c).  If the ROM trampoline body ever grew past the
    # buffer, the copy would silently overwrite the WRAM globals that follow it
    # (the banked-call staging area / the call trampoline).  Verify the bodies
    # fit now, at build time, instead of corrupting WRAM at runtime.  Symbol
    # addresses come from the matching .noi symbol listing next to the .map.
    tramp_buf_size = 64
    tramp_bodies = {
        "banked_copy": ("_banked_copy_tramp", "_banked_copy_tramp_end",
                        "g_banked_tramp"),
        "banked_call": ("_banked_call_tramp", "_banked_call_tramp_end",
                        "g_banked_call_tramp"),
    }
    symbols = {}
    noi = os.path.splitext(sys.argv[1])[0] + ".noi"
    if os.path.exists(noi):
        with open(noi) as f:
            for line in f:
                m = re.match(r"\s*DEF\s+(\w+)\s+0x([0-9A-Fa-f]+)", line)
                if m:
                    symbols[m.group(1)] = int(m.group(2), 16)

    ok = True

    print("Game Boy ROM memory budget")
    print(f"Map: {os.path.basename(sys.argv[1])}")
    print("--------------------------")

    if "_CODE" in areas:
        start, size, nbytes = areas["_CODE"]
        print(f"_CODE (fixed bank code/data) : {nbytes:>6} B  @ 0x{start:04X}-0x{start + size:04X}")
    else:
        print("_CODE : (not found)")
        ok = False

    if "_HOME" in areas:
        start, size, nbytes = areas["_HOME"]
        end = start + size
        # The linker appends the _INITIALIZER table (initialized-WRAM image
        # the CRT0 copies to _INITIALIZED at boot) directly after _HOME in
        # the same non-bankable fixed area.  Both must stay below 0x8000.
        init_size = 0
        if "_INITIALIZER" in areas:
            init_start = areas["_INITIALIZER"][0]
            init_size = areas["_INITIALIZER"][1]
            init_end = init_start + init_size
            end = max(end, init_end)
        headroom = 0x8000 - end
        status = "OK" if end <= 0x8000 else "VIOLATION (>= 0x8000 aliases VRAM)"
        if end > 0x8000:
            ok = False
        print(f"_HOME (non-bankable)          : {nbytes:>6} B  @ 0x{start:04X}-0x{end:04X}  "
              f"headroom to 0x8000: {headroom} B  [{status}]")
        if init_size:
            print(f"  (includes {init_size} B _INITIALIZER table after _HOME)")
            # AGENTS.md 52.27: the custom CRT0 empties WRAM but never copies
            # this table, so every byte in it is silently IGNORED at boot.
            # `= 0` entries are harmless (WRAM is already zero), so judge the
            # IMAGE, not its size: the table lives in the bank-0 file area
            # (address == ROM file offset) and can be read back.
            rom = os.path.splitext(sys.argv[1])[0] + ".gb"
            nonzero = []
            try:
                with open(rom, "rb") as fh:
                    fh.seek(init_start)
                    image = fh.read(init_size)
                nonzero = [i for i, b in enumerate(image) if b]
            except OSError:
                print("  (note: ROM image not found; cannot verify initializer bytes)")
            if init_size and not nonzero:
                print("  (initializer image is all zero: harmless, but it still "
                      "costs fixed-bank bytes)")
            if nonzero:
                print(f"  [VIOLATION] _INITIALIZER holds {len(nonzero)} non-zero byte(s) "
                      f"(first at +{nonzero[0]}): the custom CRT0 never copies it "
                      f"(AGENTS.md 52.27). Zero-init the global (BSS) and set it "
                      f"explicitly at runtime.")
                ok = False
    else:
        print("_HOME : (not found)")
        ok = False

    if "_DATA" in areas:
        start, size, nbytes = areas["_DATA"]
        end = start + size
        ram_headroom = 0xE000 - end
        print(f"_DATA (WRAM)                  : {nbytes:>6} B  @ 0x{start:04X}-0x{end:04X}  "
              f"headroom to 0xE000: {ram_headroom} B")
        # True stack gap: _DATA end is NOT the stack floor -- the
        # _INITIALIZED image + BSS globals (input mask, telemetry,
        # scenario state, ...) live above it up to s__HEAP, and the
        # stack grows down from 0xE000 into whatever is below.  The old
        # headroom figure overstated the gap by that ~66 B, which is
        # exactly how a +23 B WRAM shift looked safe while smashing
        # input (ghost START; see the canary post-mortem in
        # docs/roadmap.md).  Report the gap that matters.
        if "s__HEAP" in symbols:
            heap = symbols["s__HEAP"]
            gap = 0xE000 - heap
            # Hard floor (ratchet): the battle path measures ~125-126 B
            # of real stack reach against this gap (canary scenarios
            # game_over_quit + battle_multi_enemy_cycle_kill read
            # margin 0-1).  ANY shrinkage risks smashing the topmost
            # globals (the +23 B ghost-input class).  Growing WRAM must
            # first land the battle frame diet (docs/roadmap.md §52.24);
            # lowering this floor without that diet just re-arms silent
            # corruption.  Debug map only -- the harness runs debug.
            gap_min = 126
            gstatus = "OK" if gap >= gap_min else \
                "VIOLATION (WRAM grew into the zero-margin stack gap)"
            if gap < gap_min:
                ok = False
            print(f"stack gap (s__HEAP..0xE000)   : {gap:>6} B  @ 0x{heap:04X}-0xE000  [{gstatus}]")
        else:
            print("stack gap: s__HEAP not found in .noi")
            ok = False
    else:
        print("_DATA : (not found)")
        ok = False

    print()
    print("Switchable ROM bank size checks (16 KB each)")
    print("--------------------------------------------")
    bank_names = sorted((n for n in areas if re.fullmatch(r"_CODE_[0-9]+", n)),
                        key=lambda n: int(n.rsplit("_", 1)[1]))
    if not bank_names:
        print("(no banked areas found)")
        ok = False
    for name in bank_names:
        start, size, nbytes = areas[name]
        headroom = 0x4000 - size
        status = "OK" if size <= 0x4000 else "VIOLATION (spills into next bank)"
        if size > 0x4000:
            ok = False
        print(f"{name:<26} : {size:>6} B  headroom: {headroom:>5} B  [{status}]")

    print()
    print("RAM-resident trampoline size checks")
    print("-----------------------------------")
    for name, (start_sym, end_sym, buf_sym) in tramp_bodies.items():
        if start_sym in symbols and end_sym in symbols:
            size = symbols[end_sym] - symbols[start_sym]
            status = "OK" if size <= tramp_buf_size else "VIOLATION (overflows buffer)"
            if size > tramp_buf_size:
                ok = False
            print(f"{name:<26} : {size:>3} B / {tramp_buf_size} B buffer  [{status}]")
        else:
            print(f"{name:<26} : symbols not found in .noi")
            ok = False

    # Banked target address validation: symbols that run through the
    # banked-call trampoline (crt0.s) must resolve into the CPU's
    # switchable ROM window (0x4000-0x7FFF).  The trampoline maps the
    # target's low 16 bits to runtime 0x4000 | (target & 0x3FFF), so a
    # symbol placed outside this window by the linker would produce a
    # silent jump into garbage.  Catch this at build time instead of
    # under the harness at runtime.
    banked_targets = {
        "_combo_resolve_banked": "combo eval + effect dispatch (bank 3)",
        "_effect_resolve_into": "effect scaling body (bank 3)",
        "_battle_init_deck_banked": "battle deck bridge (bank 2)",
        "_status_apply_banked": "status apply body (bank 3)",
        "_status_tick_banked": "status tick body (bank 3)",
        "_save_op_banked": "SRAM save/load body (bank 8)",
        "_world_gate_check_banked": "edge predicate + walkability (bank 2)",
        "_world_edge_spawn_banked": "edge-crossing entry spawn (bank 2)",
        "_dialogue_start_def_banked": "dialogue start (bank 7)",
        "_dialogue_choice_nav_banked": "dialogue YES/NO nav + resolve (bank 8)",
        "_deck_init_default_banked": "starter deck unpacker (bank 2)",
        "_deck_reshuffle_banked": "discard reshuffle (bank 7)",
        "_music_play_banked": "track -> song/bank resolve (bank 8)",
        "_jingle_full_banked": "chest-jingle edge + restore clock (bank 8)",
        "_scene_load_tiles_banked": "scene tile loader (bank 5)",
        "_enemy_deck_setup_banked": "enemy deck setup (bank 2)",
        "_battle_card_undo_banked": "battle card undo (bank 3)",
        "_ui_update_battle_banked": "battle render (bank 3)",
        "_actor_load_scene_banked": "scene actor loader (bank 2)",
        "_chest_is_open_banked": "chest-sprite open resolver (actor tables bank)",
        "_loot_synth_banked": "loot def synthesis (bank 8)",
        "_game_loot_drop_banked": "victory drop roll (bank 9)",
        "_world_patrol_slot_banked": "world actor patrol step (bank 3 release / bank 5 debug)",
        "_battle_nav_banked": "battle hand/target nav cluster (bank 4)",
        "_title_content_render": "title screen render (bank 4)",
        "_tutorial_content_render": "tutorial screen render (bank 4)",
        "_thanks_content_render": "thanks screen render (bank 4)",
        "_ending_content_render": "ending screen render (bank 4)",
        "_game_over_content_render": "game over screen render (bank 4)",
        "_item_screen_render_banked": "item screen render (bank 2)",
        "_item_quest_render_banked": "quest tab render (bank 8)",
        "_item_quest_nav_banked": "quest tab nav (bank 8)",
        "_credits_content_render": "credits screen render (bank 5)",
        "_intro_content_render": "intro slides render (bank 4)",
        "_title_menu_step_banked": "title menu caret step (bank 4)",
        "_ui_load_cram_banked": "CGB CRAM palette loader (bank 5)",
        "_ui_load_tileset_banked": "tileset VRAM & palette loader (bank 5)",
    }
    print()
    print("Banked target address validation")
    print("--------------------------------")
    for sym, desc in banked_targets.items():
        if sym in symbols:
            addr = symbols[sym]
            # Banked symbols have a 5-digit address like 0x24000
            # (bank 2, offset 0x4000).  The trampoline uses the low 16 bits.
            low16 = addr & 0xFFFF
            in_window = 0x4000 <= low16 <= 0x7FFF
            status = "OK" if in_window else "VIOLATION (not in switchable ROM 0x4000-0x7FFF)"
            if not in_window:
                ok = False
            print(f"{sym:<28}: 0x{addr:05X} (window 0x{low16:04X})  ({desc})  [{status}]")
        else:
            print(f"{sym:<28}: not found in .noi")
            ok = False

    # ISR-ROM0 placement: every fixed-bank function that writes the MBC5
    # bank register (0x2000) directly must live in ROM0 (<0x4000, always
    # mapped).  The trampolines run banked bodies with IME enabled, so the
    # timer ISR can fire while another ROM bank is mapped; a _HOME
    # (>0x4000) direct-switcher would then unmap its own code fetch when
    # it selects another bank.  That covers the ISR path (audio_update's
    # tail restore relies on this: no 0x4000 fetch between the inline
    # selects and the restore) AND the user-context players (play/mute
    # select the song bank around the driver call under __critical).
    # Fail loudly if link order ever pushes one of these past 0x4000.
    print("Timer-ISR ROM0 placement checks")
    print("-------------------------------")
    isr_rom0 = {
        "_audio_update": "music clock ISR entry",
        "_huge_music_update": "tracker tick (ISR only caller)",
        "_huge_music_mute_channel_isr": "SFX-end unmute (ISR only caller)",
        "_huge_music_play_banked": "track switch (user, __critical)",
        "_huge_music_mute_channel": "channel mute (user, __critical)",
    }
    for sym, desc in isr_rom0.items():
        if sym in symbols:
            addr = symbols[sym]
            in_rom0 = addr < 0x4000
            status = "OK" if in_rom0 else "VIOLATION (must live in ROM0 <0x4000)"
            if not in_rom0:
                ok = False
            print(f"{sym:<28}: 0x{addr:05X}  ({desc})  [{status}]")
        else:
            print(f"{sym:<28}: not found in .noi")
            ok = False
    print()
    # Debug-harness-only trampoline targets (absent from the release
    # link): checked when present, skipped when absent, so the same
    # gate runs against both build/kaartenheld_debug.map and
    # build/kaartenheld.map.
    optional_banked_targets = {
        "_debug_snapshot_banked": "core snapshot builder (bank 2)",
        "_debug_state_snapshot_banked": "debug state snapshot (bank 2)",
        "_debug_action_leaf_banked": "debug-action leaf cases (bank 5)",
    }
    for sym, desc in optional_banked_targets.items():
        if sym in symbols:
            addr = symbols[sym]
            low16 = addr & 0xFFFF
            in_window = 0x4000 <= low16 <= 0x7FFF
            status = "OK" if in_window else "VIOLATION (not in switchable ROM 0x4000-0x7FFF)"
            if not in_window:
                ok = False
            print(f"{sym:<28}: 0x{addr:05X} (window 0x{low16:04X})  ({desc})  [{status}]")
        else:
            print(f"{sym:<28}: absent (build-conditional)  ({desc})  [OK]")

    print()
    if ok:
        print("memory budget: invariants OK")
    else:
        print("memory budget: INVARIANT VIOLATION")
        sys.exit(1)


if __name__ == "__main__":
    main()
