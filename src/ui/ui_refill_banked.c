#pragma bank 10

#include <stdint.h>
#include "world/world.h"
#include "ui/ui.h"
#include "core/banked.h"
#include "tile_glyph.h"

/* ── Debug semantic-buffer refill (bank 10) ─────────────────────────
 * DEBUG-only. Moved out of ui.c so the fixed bank stays under 0x8000
 * (same pattern as scenario_load_apply_banked / debug_action_leaf_banked
 * / combo_content.c). Runs on every full world redraw via the thin fixed
 * wrapper ui_refill_semantic() in ui.c (staging + trampoline only).
 *
 * Self-contained: WRAM-only reads/writes, the generated glyph inline
 * (header, inlined -- never a fixed-bank call), and a duplicated 8-byte
 * glyph map (banked bodies must not call fixed-bank functions, so the
 * map rides along instead of referencing ui.c's). No multiplies/divides
 * anywhere (SDCC lowers them to fixed-bank lib routines): map rows
 * advance by shift, the screen buffer and actor rows by pointer walk. */
static const char g_sem_map_banked[8] = { '.', '#', '>', 'B', '1', '2', '3', '4' };

void ui_refill_semantic_banked(void)
{
    const World *w;
    uint8_t x, y, col, row, slot;
    const WorldActorRuntime *a;
    uint8_t sx, sy;
    uint8_t t;
    char *p;
    const uint8_t *mrow;
    uint8_t i;

    w = (const World *)g_bk_ptr_a;
    p = &g_ui_screen_buf[0][0];
    for (y = 0; y < WORLD_VIEW_H; y++) {
        row = (uint8_t)(w->scroll_y + y);
        mrow = &w->map[0][0] + ((uint16_t)row << 5);
        for (x = 0; x < WORLD_VIEW_W; x++) {
            col = (uint8_t)(w->scroll_x + x);
            if (col >= w->width || row >= w->height) {
                *p++ = '.';
            } else {
                t = mrow[col];
                if ((uint8_t)(t - 8) <= (uint8_t)(253 - 8))
                    *p++ = tile_landscape_glyph(t);
                else
                    *p++ = (t < 8) ? g_sem_map_banked[t] : '.';
            }
        }
        /* Skip the NUL terminator column: rows are WORLD_VIEW_W + 1
         * wide (the tail stamps NULs before this body runs). */
        p++;
    }

    a = &w->actors[0];
    for (slot = 0; slot < MAX_WORLD_ACTORS; slot++, a++) {
        if (!a->active) continue;
        sx = (uint8_t)(a->x - w->scroll_x);
        sy = (uint8_t)(a->y - w->scroll_y);
        if (sx < WORLD_VIEW_W && sy < WORLD_VIEW_H) {
            /* Row stride is WORLD_VIEW_W + 1 (NUL terminator column). */
            uint8_t *arow = (uint8_t *)&g_ui_screen_buf[0][0];
            for (i = 0; i < sy; i++) arow += (WORLD_VIEW_W + 1);
            arow[sx] = (uint8_t)a->visual;
        }
    }
}
