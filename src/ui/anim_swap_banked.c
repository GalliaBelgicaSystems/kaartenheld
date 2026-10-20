#pragma bank 8

#include <gb/gb.h>
#include <stdint.h>
#include "game.h"
#include "world.h"
#include "banked.h"
#include "gfx/rpg_tile_lookup.h"
#include "anim_pairs.h"

#ifdef DEBUG_BUILD
extern uint8_t g_tilemap_mirror[32 * 32];
#endif

/* -- Recorded animated background cells --------------------------------
 * Ring indices (0..575) of cells holding an animation frame tile
 * (campfire flicker / water shimmer).  Rebuilt by
 * world_anim_rebuild_banked() after every full world redraw (the only
 * writer of the ring); the swap pass walks this list instead of
 * scanning.  8 entries cover the current maps (<=5 animated cells)
 * with headroom; beyond that extras stay on frame A (graceful,
 * documented).  17 BSS bytes -- the stack-budget ratchet
 * (make stack-budget) and the layout sentinels pin the cost
 * (16 entries tripped the 126 B stack-gap floor). */
#define ANIM_CELL_CAP 8
uint16_t g_anim_cells[ANIM_CELL_CAP];
uint8_t g_anim_count;

/* -- Background animation bodies (bank 7) ------------------------------
 * Campfire flicker / water shimmer, data-driven (generated
 * anim_pairs.h): cells showing the step's other frame take this step's
 * frame (A on even steps, B on odd).  Dispatched from
 * ui_draw_actors_sprites() (swap) and ui_draw_world_map() (rebuild) in
 * fixed bank with staging (g_bk_ptr_a = World, g_bk_byte_b = anim_step
 * for the swap; the rebuild needs no staging), i.e. overworld +
 * dialogue-over-overworld only -- battle never calls these paths, so
 * battle art tiles can't collide.  Frame pairs share one
 * glyph/category, so the semantic screen buffer is untouched; the DEBUG
 * tilemap mirror is kept in sync for the harness.  VRAM bank is set
 * explicitly (tilemap ring lives in VRAM bank 0).
 *
 * Both bodies live in bank 8 (moved from bank 7 for the end-credits
 * theme: bank 7 filled up with the seventh song; bank 8 has room, and
 * these bodies are pure trampoline-dispatched code with no same-bank
 * data dependencies -- kAnimPairs travels with them via anim_pairs.h.
 * Previously bank 7 (bank 5 -- the world bank -- filled up when
 * the castle tile window grew the tileset past 17 slots; debug bank 4,
 * where the OAM pass lives, is full, and bank 2 overflows with them),
 * and a banked body must never switch banks mid-function, so the
 * fixed-bank callers run these as separate passes instead.
 * Pure (WRAM + regs + same-bank table reads only): no fixed-bank calls
 * while mapped.
 *
 * PERF (Oct 2026): the swap used to nest the pair loop OUTSIDE the
 * viewport loop (5 pairs x 20x18 cells, recomputing scroll->ring per
 * cell at ~492 cycles) -- ~5 VBlanks of CPU every 4th frame, halving
 * release speed (vsync-gated main loop; measured 40 ticks/flip vs 16
 * at full speed).  A full-ring single scan (576 cells) recovered to
 * ~80% -- not enough.  Now the swap walks the recorded list (<=8
 * entries every 4th frame, <1% of a VBlank): measured parity with the
 * no-animation baseline (16.84 ticks/flip both).  A VRAM-index list
 * was chosen over in-draw
 * recording because the record hook would live in fixed-bank
 * ui_draw_world_cell (~38 B of _HOME margin left); the post-redraw
 * rebuild scan needs no fixed-bank loop and no per-cell history.
 * The ring is static between full redraws (hardware SCX/SCY scrolls
 * it, no per-frame redraws) and every redraw rebuilds, so the list is
 * always valid; scrolled-in cells are already on-step, no convergence
 * lag.  Pair cap 2/kind in the swap is generator-guaranteed (fire +
 * water patterns only).  Font tiles (ids < 128) can never false-match
 * animation VRAM ids (>= 128), so the dialogue box rows stamped after
 * a rebuild are safely skipped.  Stateless swap (no last-step byte):
 * value-matched writes only, idempotent.
 *
 * PPU gating (Oct 2026): the swap runs LCD-on at a fixed point in the
 * render path, so with a constant per-frame update cost it executes at
 * a fixed PPU phase every run.  A Mode-3 phase drops every raw access
 * (reads return 0xFF, writes are ignored): the animation froze while
 * idle and only advanced when input jittered the phase, while OAM
 * actors (WRAM + VBlank DMA) kept animating.  Every swap cell access
 * therefore waits for an accessible window first (see anim_read_cell);
 * PyBoy, which does not gate VRAM, exits the wait at once and behaves
 * exactly as before. */
static uint8_t anim_read_cell(uint16_t cell);

void world_anim_rebuild_banked(void)
{
    uint8_t slots[4];
    uint8_t nslots;
    uint8_t p;
    uint8_t sa;
    uint8_t sb;
    uint8_t i;
    uint8_t row;
    uint8_t col;
    uint8_t v;
    uint8_t n;
    uint8_t s;
    uint8_t k;
    uint8_t found;
    uint8_t base;
    uint16_t idx;
    /* Distinct pair-endpoint slots across all kinds (fire 37/38, water
     * 60/61 -- shared by every kind, so the rebuild is kind-agnostic;
     * castle maps can never hold these VRAM ids anyway). */
    nslots = 0;
    slots[0] = 0;
    slots[1] = 0;
    slots[2] = 0;
    slots[3] = 0;
    for (p = 0; p < (uint8_t)(ANIM_PAIR_COUNT * 3); p = (uint8_t)(p + 3)) {
        sa = kAnimPairs[(uint8_t)(p + 1)];
        sb = kAnimPairs[(uint8_t)(p + 2)];
        for (i = 0; i < 2; i++) {
            s = (i == 0) ? sa : sb;
            found = 0;
            for (k = 0; k < nslots; k++) {
                if (slots[k] == s) {
                    found = 1;
                    break;
                }
            }
            if (!found && nslots < 4) {
                slots[nslots] = s;
                nslots = (uint8_t)(nslots + 1);
            }
        }
    }
    /* Ring lives in VRAM bank 0.  Gate crossings repaint with the LCD
     * on, so reads go through the bounded helper (mode-3 returns 0xFF,
     * which can only under-record, never mis-record: 0xFF decodes past
     * every animation slot).  LCD-off full redraws skip the wait at once.
     * Every map fits rows 0..17 x cols 0..31 (<=24 wide, <=18 tall). */
    n = 0;
    idx = 0;
    for (row = 0; row < 18; row++) {
        for (col = 0; col < 32; col++) {
            v = anim_read_cell(idx);
            base = (uint8_t)(v - RPG_TILE_BASE_WORLD);
            /* v is an animation id iff v >= 128 and its slot is listed.
             * The subtraction is unsigned: font tiles (v < 128) wrap
             * high and never match a small slot. */
            if (v >= RPG_TILE_BASE_WORLD) {
                for (i = 0; i < nslots; i++) {
                    if (base == slots[i]) {
                        if (n < ANIM_CELL_CAP) {
                            g_anim_cells[n] = idx;
                            n = (uint8_t)(n + 1);
                        }
                        break;
                    }
                }
            }
            idx++;
        }
    }
    g_anim_count = n;
}

/* -- PPU-gated VRAM cell access (swap only) -----------------------------
 * The swap runs LCD-on at a fixed point in the render path, so with a
 * constant per-frame update cost it executes at a fixed PPU phase every
 * run.  A Mode-3 phase drops every raw access (reads return 0xFF,
 * writes are ignored): the animation freezes while idle and only
 * advances when input jitters the phase, while OAM actors (WRAM +
 * VBlank DMA) keep animating.  Every swap cell access therefore waits
 * for an accessible window first (Modes 0-1; Mode 2 is too short to be
 * useful).  Same contract as fixed-bank ui_vram_sync_write (which a
 * banked body must not call -- the bank is switched away), plus a
 * bound: a frozen PPU (paused debugger) can never hang the ROM
 * (~200 x ~6 cycles far exceeds one scanline; on timeout the access
 * still executes and reruns converge).  Pauses landing at the
 * game_render breakpoint in VBlank exit the waits at once.
 * LCD-off reads STAT idle (mode 0) and skips instantly; PyBoy, which
 * does not gate VRAM, exits at once too.
 *
 * Deliberately di/ei around the window via BANKED_VRAM_WRITE /
 * BANKED_VRAM_READ (like fixed-bank ui_vram_sync_write): the old
 * no-di/ei rule died with the trampoline rework.  Pre-rework, any ei
 * mid-body admitted the timer ISR, which restored home bank 1 and
 * resumed the body on the wrong bank -- wild execution that looked
 * exactly like the reported PyBoy freeze (rapid di/ei pairs wedge the
 * guest; byte-identical NOPs run fine because they never enable
 * interrupts).  The g_rom_bank_live save/restore removes the hazard,
 * and the per-cell swap itself is still rerun-convergent on top.  The
 * one-shot entry-scan rebuild additionally NEEDS the atomicity: a
 * Mode-3-eclipsed read returns 0xFF and that cell never animates for
 * the whole map stay. */
static uint8_t anim_read_cell(uint16_t cell)
{
    uint8_t v;
    /* The rebuild runs once per map entry -- including LCD-on gate
     * crossings -- so a Mode-3-eclipsed read (0xFF) would permanently
     * lose that cell's animation for the whole map stay.  The di window
     * is safe now: the pre-rework PyBoy freeze was the old ISR restoring
     * home bank 1 mid-body (misrouted execution that looked like a
     * freeze), fixed by the g_rom_bank_live save/restore. */
    VBK_REG = 0;
    BANKED_VRAM_READ(v, ((volatile uint8_t *)0x9800) + cell);
    return v;
}

static void anim_write_cell(uint16_t cell, uint8_t tile)
{
    VBK_REG = 0;
    BANKED_VRAM_WRITE(((volatile uint8_t *)0x9800) + cell, tile);
#ifdef DEBUG_BUILD
    g_tilemap_mirror[cell] = tile;
#endif
}

void world_anim_swap_banked(void)
{
    const World *w = (const World *)g_bk_ptr_a;
    uint8_t anim = g_bk_byte_b;
    uint8_t o0;
    uint8_t w0;
    uint8_t o1;
    uint8_t w1;
    uint8_t npairs;
    uint8_t p;
    uint8_t sa;
    uint8_t sb;
    uint8_t other;
    uint8_t want;
    uint8_t i;
    uint8_t n;
    uint8_t v;
    uint16_t cell;
    if (!w) return;
    /* Throttle: every 4th game frame is plenty -- steps last 16 frames
     * and the walk is tiny now.  g_game.frame advances once per
     * game_update on both builds. */
    if (((uint8_t)g_game.frame & 3) != 0) {
        return;
    }
    /* Collect this tileset's (other -> want) frame mapping for the
     * current step.  Pairs are (tileset-kind, VRAM-slot-A, VRAM-slot-B);
     * both VRAM bases are 128 (WORLD and DESOLATE), so slot + base never
     * leaves uint8. */
    npairs = 0;
    o0 = 0;
    w0 = 0;
    o1 = 0;
    w1 = 0;
    for (p = 0; p < (uint8_t)(ANIM_PAIR_COUNT * 3); p = (uint8_t)(p + 3)) {
        if (kAnimPairs[p] != (uint8_t)w->tileset_kind) {
            continue;
        }
        sa = kAnimPairs[(uint8_t)(p + 1)];
        sb = kAnimPairs[(uint8_t)(p + 2)];
        if (anim) {
            other = (uint8_t)(sa + RPG_TILE_BASE_WORLD);
            want = (uint8_t)(sb + RPG_TILE_BASE_WORLD);
        } else {
            other = (uint8_t)(sb + RPG_TILE_BASE_WORLD);
            want = (uint8_t)(sa + RPG_TILE_BASE_WORLD);
        }
        if (npairs == 0) {
            o0 = other;
            w0 = want;
            npairs = 1;
        } else if (npairs == 1) {
            o1 = other;
            w1 = want;
            npairs = 2;
        }
    }
    if (npairs == 0) {
        return;
    }
    /* Walk the recorded list through PPU-gated cell access (above):
     * phase-independent on every target, idle or moving. */
    n = (g_anim_count <= ANIM_CELL_CAP) ? g_anim_count : ANIM_CELL_CAP;
    for (i = 0; i < n; i++) {
        cell = g_anim_cells[i];
        v = anim_read_cell(cell);
        if (v == o0) {
            anim_write_cell(cell, w0);
        } else if (npairs > 1 && v == o1) {
            anim_write_cell(cell, w1);
        }
    }
}
