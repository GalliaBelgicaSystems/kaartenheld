#ifndef BANKED_H
#define BANKED_H

#include <stdint.h>
#include <gb/gb.h>

/* Live MBC5 ROM-bank mirror (WRAM, always mapped).  The WRAM trampolines
 * (crt0.s) maintain this: target bank on entry, HOME_BANK on exit.  The
 * 256 Hz timer ISR (audio_update) reads it to restore the interrupted
 * bank after its own selects, so banked bodies run with interrupts
 * ENABLED and the music clock never stalls (the verify-music gate).
 * Initialized to the home bank by main() before enable_interrupts(). */
extern volatile uint8_t g_rom_bank_live;

/* PPU-safe VRAM/CRAM data-store for banked bodies (LCD-on overlays:
 * dialogue boxes, menus, battle HUD; LCD-on gate-crossing loads: tilesets,
 * CRAM, wipes).  Bounded Mode-2/3 wait + di/ei around a single store: the
 * di window is microseconds (no music-clock stall), and the ei cannot
 * misroute -- the timer ISR saves/restores g_rom_bank_live.  LCD-off
 * skips the wait (STAT is idle).  Address and value stage into locals
 * BEFORE the LCDC check/di: evaluating them after the STAT sample
 * (array subscripts, ROM table loads) costs 84-124 T-states and exceeds
 * the 80-T-state Mode-2 buffer, letting a late-Mode-0 sample roll into
 * Mode 3.  Use for every banked VRAM/CRAM data
 * write that can execute with the LCD on (gate crossings repaint
 * with the LCD on: ui_draw_world_transition never blanks).  Rerun-
 * converged anim-swap cells use the same macro (a dropped access is
 * invisible, but atomicity is free). */
#define BANKED_VRAM_WRITE(dst, v) \
    do { \
        volatile uint8_t *_bvw_dst = (volatile uint8_t *)(dst); \
        uint8_t _bvw_v = (uint8_t)(v); \
        uint8_t _bvw_t; \
        if (LCDC_REG & 0x80) { \
            __asm di __endasm; \
            _bvw_t = 200; \
            while (_bvw_t--) { \
                if (!(STAT_REG & 0x02)) break; \
            } \
            *_bvw_dst = _bvw_v; \
            __asm ei __endasm; \
        } else { \
            *_bvw_dst = _bvw_v; \
        } \
    } while (0)

/* PPU-safe VRAM data-read for banked bodies (map-entry animation-cell
 * scan).  Same window discipline as the write macro -- including
 * pre-staged address: a Mode-3-eclipsed read returns 0xFF, and for the
 * one-shot entry scan that permanently loses the cell's animation for
 * the whole map stay. */
#define BANKED_VRAM_READ(dst, src) \
    do { \
        volatile uint8_t *_bvr_src = (volatile uint8_t *)(src); \
        uint8_t _bvr_t; \
        if (LCDC_REG & 0x80) { \
            __asm di __endasm; \
            _bvr_t = 200; \
            while (_bvr_t--) { \
                if (!(STAT_REG & 0x02)) break; \
            } \
            (dst) = *_bvr_src; \
            __asm ei __endasm; \
        } else { \
            (dst) = *_bvr_src; \
        } \
    } while (0)

/* RAM-resident MBC5 bank-switch + copy (src/crt0.s).  banked_copy() copies
 * `n` bytes from a banked ROM source into a WRAM destination, running the
 * MBC5 bank switch from a WRAM trampoline so it is safe regardless of the
 * caller's currently mapped ROM bank.  The trampoline must be resident
 * before any banked content is read: game_init() calls banked_copy_init()
 * once at boot (the harness skips CRT0, so the copy cannot live there).
 *
 * The bank register is always restored to the project's home bank 1 (the
 * -yo8 layout maps fixed-bank code above CPU 0x4000 through ROMB=1; see
 * crt0.s) before returning, so banked content must be fully copied out
 * before the call returns.  Never read banked data after the call.  `n` is
 * a uint8_t, so tables (rows or strings) must be smaller than 256 bytes.
 *
 * The trampoline itself lives in the linker-allocated WRAM buffer
 * g_banked_tramp[] (declared here so the engine and crt0.s agree on its
 * symbol): a hardcoded WRAM address (e.g. 0xC940) would collide with
 * linker-placed _INITIALIZED variables such as _g_boot_phase. */
extern uint8_t g_banked_tramp[64];

void banked_copy(uint8_t bank, void *dst, const void *src, uint8_t n);
void banked_copy_init(void);

/* ── Banked-call trampoline (src/crt0.s) ─────────────────────────────
 * Runs a fixed no-arg function that lives in a banked ROM bank from a
 * fixed-bank caller (used to host self-contained engine modules such as
 * src/battle/combo.c out of the fixed bank).
 *
 * ── ABI / reentrancy contract ──────────────────────────────────────
 * banked_call_run() is:
 *   * SYNCHRONOUS: it does not return until the banked target returns.
 *   * NON-REENTRANT: it dispatches through the shared staging globals
 *     below (g_bk_call_bank/target/ptr_a/ptr_b/byte_a/byte_b).  There is
 *     no nesting: a banked target must NOT call another banked function,
 *     and a caller must not invoke banked_call_run() from within a
 *     banked target.  The staging globals are a one-shot transfer, not a
 *     persistent API — overwrite them before each call.
 *   * INTERRUPT-SAFE: the WRAM trampoline enables interrupts for the
 *     body (ei after the bank select, di before the restore), so the
 *     256 Hz music clock keeps running across long bodies.  The timer
 *     ISR saves/restores the live bank via g_rom_bank_live, so the body
 *     resumes on its own bank.  Do NOT add di/ei (or __critical) inside
 *     the target: a held di reintroduces music-clock stalls, and the
 *     lint gate rejects bare interrupt ops in banked files.  LCD-on
 *     VRAM/CRAM data accesses must use BANKED_VRAM_WRITE / BANKED_VRAM_READ
 *     (di + bounded Mode wait per access); the ISR can otherwise eclipse
 *     the wait->access window and drop the access into Mode 3.
 *   * The target runs with a CONTENT ROM BANK SELECTED, not the caller's
 *     bank.  It may access only (a) its own bank-local code/data and (b)
 *     the staged _DATA globals below.  It must NOT call any fixed-bank
 *     function or return through a fixed-bank pointer while executing.
 *   * On return the home bank (HOME_BANK, crt0.s) is restored and
 *     g_rom_bank_live tracks it again, so the caller's execution ring
 *     is unchanged.
 *
 * The fixed-bank wrapper stages the target bank + logical address and its
 * arguments into the _DATA globals below, then calls banked_call_run()
 * (asm) so the WRAM trampoline never parses SDCC's stack layout.  The
 * banked target reads the staged globals.  g_bk_ptr_a/b carry pointer
 * arguments; g_bk_byte_a/b carry byte arguments.  The target's logical
 * address must be a bank-relative offset in 0x0000-0x3FFF, because the
 * trampoline maps it to runtime 0x4000 | (target & 0x3FFF).  The
 * fixed-bank wrapper takes the address of the banked symbol, whose low 14
 * bits are the bank-relative offset by construction of the linker layout. */
extern uint8_t g_bk_call_bank;
extern uint16_t g_bk_call_target;
extern void *g_bk_ptr_a;
extern void *g_bk_ptr_b;
extern uint8_t g_bk_byte_a;
extern uint8_t g_bk_byte_b;
extern uint8_t g_bk_byte_c;
extern uint8_t g_bk_byte_d;

/* Linker-allocated home for the banked-call trampoline (WRAM), copied by
 * banked_call_init() at boot (game_init calls it, like banked_copy_init). */
extern uint8_t g_banked_call_tramp[64];

extern void banked_call_run(void);
void banked_call_init(void);

#endif /* BANKED_H */
