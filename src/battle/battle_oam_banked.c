#pragma bank 5

#include <stdint.h>
#include "battle.h"
#include "battle_data.h"
#include "banked.h"

/* Battle enemy OAM sprites (Florent's model), bank-5 body.  Dispatch from
 * the fixed-bank ui_update_battle wrapper right after the bank-3 render
 * (sequential trampoline calls, never nested: the trampoline contract
 * forbids banked->banked calls, AGENTS.md banked.h).  Bank 3 was full in
 * the release layout; bank 5 has room, and this pass needs no bank-3
 * helpers -- only WRAM caches (always mapped) plus the staged Battle*.
 *
 * Per enemy slot: live OAM-capable enemy -> draw stride; otherwise hide
 * stride (dead/absent/blink telegraph).  Stride k lives at entries
 * 1+6k (overworld region, hidden by ui_sprite_begin_transition on entry;
 * nothing else writes OAM during battle).  Tiles are the same VRAM ids
 * the BG stamp would use (sprites fetch the 0x8000 block, AGENTS.md
 * 52.22); prop is the per-battle scratch OBJ slot programmed at entry.
 * Boss stays a BG stamp (3x3 exceeds the stride) and never reaches here
 * (its oam flag is 0). */

/* OAM entries for battle sprites. Slots 1-26 are the overworld region
 * (hidden by ui_sprite_begin_transition on entry, so no stale art
 * survives); battle enemies take fixed strides below. */
#define BATTLE_OAM_BASE 1u
#define BATTLE_OAM_STRIDE 6u

static uint8_t battle_oam_x(uint8_t x, uint8_t w)
{
    if (w == 0 || w > 6) w = 3;
    return (uint8_t)(x + ((6 - w) >> 1));
}

void battle_oam_draw_banked(void)
{
    const volatile Battle *battle = (const volatile Battle *)g_bk_ptr_a;
    uint8_t k;

    if (!battle) return;
    for (k = 0; k < MAX_BATTLE_ENEMIES; k++) {
        uint8_t x, w, h;
        uint8_t live;
        uint8_t blink;
        w = g_battle_enemy_art_w[k];
        h = g_battle_enemy_art_h[k];
        if (w == 0 || w > 6) w = 3;
        if (h == 0 || h > 4) h = 2;
        live = (uint8_t)(k < battle->enemy_count &&
                         battle->enemies[k].hp != 0 &&
                         g_battle_enemy_art[k] != 0xFF &&
                         g_battle_enemy_art_oam[k]);
        blink = (uint8_t)(battle->phase == BATTLE_PHASE_PLAYER_DEFEND &&
                          (((battle->timer_ticks >> 4) & 1) == 0 &&
                           k == battle->attacking_enemy_idx));
        if (!live || blink) {
            uint8_t n;
            uint8_t e0 = BATTLE_OAM_BASE;
            if (k >= 1) e0 = (uint8_t)(e0 + BATTLE_OAM_STRIDE);
            if (k >= 2) e0 = (uint8_t)(e0 + BATTLE_OAM_STRIDE);
            for (n = 0; n < BATTLE_OAM_STRIDE; n++) {
                volatile uint8_t *e = (volatile uint8_t *)(0xC000u +
                    ((uint16_t)(e0 + n) << 2));
                e[0] = 0;
            }
            continue;
        }
        {
            uint8_t frame = 0;
            uint8_t base;
            uint8_t cx, cy;
            uint8_t ftiles, t, n;
            uint8_t e0 = BATTLE_OAM_BASE;
            uint8_t art_row = g_battle_hud.enemy_sprite_row;
            volatile uint8_t *e;
            if (k >= 1) e0 = (uint8_t)(e0 + BATTLE_OAM_STRIDE);
            if (k >= 2) e0 = (uint8_t)(e0 + BATTLE_OAM_STRIDE);
            x = battle_oam_x(g_battle_hud.enemy_positions[k][0], w);
            if (g_battle_enemy_art_frames[k] > 1) {
                frame = (uint8_t)((battle->timer_ticks >> 4) & 1);
            }
            base = g_battle_enemy_art_base[k];
            ftiles = 0;
            for (cx = 0; cx < h; cx++) ftiles = (uint8_t)(ftiles + w);
            t = base;
            if (frame) t = (uint8_t)(t + ftiles);
            n = 0;
            for (cy = 0; cy < h; cy++) {
                for (cx = 0; cx < w; cx++) {
                    e = (volatile uint8_t *)(0xC000u +
                        ((uint16_t)(e0 + n) << 2));
                    e[0] = (uint8_t)(((art_row + cy) << 3) + 16);
                    e[1] = (uint8_t)(((x + cx) << 3) + 8);
                    e[2] = t;
                    e[3] = BATTLE_OBJ_SCRATCH;
                    t++;
                    n++;
                    if (n >= BATTLE_OAM_STRIDE) break;
                }
                if (n >= BATTLE_OAM_STRIDE) break;
            }
            for (; n < BATTLE_OAM_STRIDE; n++) {
                e = (volatile uint8_t *)(0xC000u +
                    ((uint16_t)(e0 + n) << 2));
                e[0] = 0;
            }
        }
    }
}
