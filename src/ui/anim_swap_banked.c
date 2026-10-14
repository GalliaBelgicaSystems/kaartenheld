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

/* ── Background animation swap body (bank 5) ─────────────────────────
 * Campfire flicker / water shimmer, data-driven (generated
 * anim_pairs.h): every visible cell showing the step's other frame
 * takes this step's frame (A on even steps, B on odd).  Idempotent
 * viewport-covering write: freshly scrolled cells settle within one
 * frame.  Dispatched from ui_draw_actors_sprites() (fixed bank) right
 * after staging (g_bk_ptr_a = World, g_bk_byte_b = anim_step), i.e.
 * overworld + dialogue-over-overworld only -- battle never calls this
 * path, so battle art tiles can't collide.  Frame pairs share one
 * glyph/category, so the semantic screen buffer is untouched; the DEBUG
 * tilemap mirror is kept in sync for the harness.  VRAM bank is set
 * explicitly (tilemap ring lives in VRAM bank 0).
 *
 * Lives in bank 5 (the world bank has room; debug bank 4, where the
 * OAM pass lives, is full, and bank 2 overflows with this body), and a
 * banked body must never switch banks mid-function, so the caller
 * cannot trampoline here from inside its own banked body -- the
 * fixed-bank dispatcher runs this as a second pass instead.
 * Pure (WRAM + regs + same-bank table reads only): no fixed-bank calls
 * while mapped.
 *
 * VBLANK budget: LCD-on tilemap writes past VBlank are dropped by the
 * PPU, so the fixed dispatcher runs this FIRST (ahead of the WRAM-only
 * OAM work, which is timing-safe anywhere) and this covers only the
 * visible viewport (not the whole map): value-matched writes only, so
 * cells outside the world (floor/font, never equal to a pair slot +
 * base) are inherently skipped with no extra bounds checks.  Throttled
 * to every 4th game frame (the full scan every frame overruns the main
 * loop): steps last 16 frames, so convergence lags a flip by at most 4
 * frames (invisible).  Stateless and idempotent: no per-cell history
 * and no WRAM -- the bird rule applies to scratch state everywhere,
 * not just audio.  Every pair runs every phase: staggering pairs
 * across phases cannot toggle (a skipped pair's cells never converge
 * back). */
void world_anim_swap_banked(void)
{
    const World *w = (const World *)g_bk_ptr_a;
    uint8_t anim = g_bk_byte_b;
    uint8_t p;
    uint8_t sx;
    uint8_t sy;
    volatile uint8_t *tilemap;
    if (!w) return;
    /* Throttle: every 4th game frame is plenty -- steps last 16 frames.
     * g_game.frame advances once per game_update on both builds. */
    if (((uint8_t)g_game.frame & 3) != 0) {
        return;
    }
    /* Tilemap ring lives in VRAM bank 0 (callers leave VBK anywhere). */
    VBK_REG = 0;
    tilemap = (volatile uint8_t *)0x9800;
    /* Pairs are (tileset-kind, VRAM-slot-A, VRAM-slot-B); both VRAM bases
     * are 128 (WORLD and DESOLATE), so slot + base never leaves uint8. */
    for (p = 0; p < (uint8_t)(ANIM_PAIR_COUNT * 3); p = (uint8_t)(p + 3)) {
        uint8_t want;
        uint8_t other;
        if (kAnimPairs[p] != (uint8_t)w->tileset_kind) {
            continue;
        }
        if (anim) {
            want = kAnimPairs[p + 2];
            other = kAnimPairs[p + 1];
        } else {
            want = kAnimPairs[p + 1];
            other = kAnimPairs[p + 2];
        }
        want = (uint8_t)(want + RPG_TILE_BASE_WORLD);
        other = (uint8_t)(other + RPG_TILE_BASE_WORLD);
        for (sy = 0; sy < WORLD_VIEW_H; sy++) {
            for (sx = 0; sx < WORLD_VIEW_W; sx++) {
                uint8_t col;
                uint8_t row;
                uint16_t cell;
                col = (uint8_t)(w->scroll_x + sx);
                row = (uint8_t)(w->scroll_y + sy);
                cell = (uint16_t)(((uint16_t)(row & 31) << 5) +
                                  (uint8_t)(col & 31));
                if (tilemap[cell] == other) {
                    tilemap[cell] = want;
#ifdef DEBUG_BUILD
                    g_tilemap_mirror[cell] = want;
#endif
                }
            }
        }
    }
}
