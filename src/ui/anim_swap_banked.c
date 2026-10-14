#pragma bank 5

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

/* -- Background animation bodies (bank 5) ------------------------------
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
 * Both bodies live in bank 5 (the world bank has room; debug bank 4,
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
 * ~80% -- not enough.  Now the swap walks the recorded list (<=16
 * entries, ~100 cycles each every 4th frame ~= 0.5k avg, <1% of a
 * VBlank): measured full speed again (16.0 ticks/flip, same as the
 * no-animation baseline).  A VRAM-index list was chosen over in-draw
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
 * value-matched writes only, idempotent. */
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
    volatile uint8_t *tilemap;
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
    /* Ring lives in VRAM bank 0.  Runs LCD-off inside the full redraw,
     * so every read lands (no PPU Mode-3 drops).  Every map fits rows
     * 0..17 x cols 0..31 (<=24 wide, <=18 tall). */
    VBK_REG = 0;
    tilemap = (volatile uint8_t *)0x9800;
    n = 0;
    idx = 0;
    for (row = 0; row < 18; row++) {
        for (col = 0; col < 32; col++) {
            v = *tilemap;
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
            tilemap++;
            idx++;
        }
    }
    g_anim_count = n;
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
    volatile uint8_t *tilemap;
#ifdef DEBUG_BUILD
    uint8_t *mirror;
#endif
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
    /* Tilemap ring lives in VRAM bank 0 (callers leave VBK anywhere). */
    VBK_REG = 0;
    tilemap = (volatile uint8_t *)0x9800;
#ifdef DEBUG_BUILD
    mirror = &g_tilemap_mirror[0];
#endif
    n = (g_anim_count <= ANIM_CELL_CAP) ? g_anim_count : ANIM_CELL_CAP;
    for (i = 0; i < n; i++) {
        cell = g_anim_cells[i];
        v = tilemap[cell];
        if (v == o0) {
            tilemap[cell] = w0;
#ifdef DEBUG_BUILD
            mirror[cell] = w0;
#endif
        } else if (npairs > 1 && v == o1) {
            tilemap[cell] = w1;
#ifdef DEBUG_BUILD
            mirror[cell] = w1;
#endif
        }
    }
}
