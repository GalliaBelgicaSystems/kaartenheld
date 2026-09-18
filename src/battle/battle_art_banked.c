#pragma bank 4
#pragma disable_warning 110

#include "battle.h"
#include "battle_data.h"
#include "actor.h"
#include "banked.h"
#include "content.h"
#include "gfx/battle_enemy_art.h"
#include <gb/gb.h>
#include <gb/cgb.h>
#include "battle_obj_tables.h"

/* Battle art loader (ROM bank 4).  Runs once per battle entry from
 * ui_draw_battle_full() (LCD-off window) via the WRAM trampoline with
 * g_bk_ptr_a = Battle*.  Self-contained: resolves the battle's
 * enemy-type row through the game layer's same-bank mapping, reads the
 * enemy-type tables and the art header directly (same bank), writes
 * VRAM with a hand loop (never calls fixed-bank lib functions while
 * the content bank is selected), and caches per-slot art + geometry in
 * the WRAM globals for the bank-3 stamper.
 *
 * Art selection stays data-driven: every combatant shares its battle's
 * enemy-type row (encounters engage clones of one actor), so the row's
 * art_index/art_frames/art_palette/art_w/art_h drive all slots; editing
 * an enemy's sprite JSON changes the ROM result with no code change.
 * Unknown battles resolve to 0xFF (text fallback, blank art zones).
 *
 * VRAM layout: slot k loads frames*W*H tiles at cumulative tile bases
 * from BATTLE_ART_VRAM_BASE (128).  World tiles normally living at 128+
 * are stale-unused during battle and reloaded by ui_load_tileset() on
 * the overworld return path.  Total art is capped at the 128 tiles from
 * 128..255: slots past the budget fall back to text. */

#define BATTLE_ART_VRAM_BASE 128u
#define BATTLE_ART_VRAM_TILES 128u
#define BATTLE_ART_MAX_W 6u
#define BATTLE_ART_MAX_H 4u

/* Program scratch OBJ slots 4..6 with one RAMPS/BATTLE row (bank-local
 * generated table) for OAM participants sharing this battle's row.
 * Idempotent: same values every call within a battle. Runs LCD-off. */
static void program_battle_obj_palettes(uint8_t index)
{
    const uint8_t *pal;
    uint8_t i, s;
    if (index >= BATTLE_OBJ_COUNT) return;
    pal = (const uint8_t *)battle_obj_by_index[index];
    for (s = 0; s < 3; s++) {
        OCPS_REG = (uint8_t)(0x80 | ((uint8_t)(BATTLE_OBJ_SCRATCH_BASE + s) << 3));
        for (i = 0; i < 8; i++) {
            OCPD_REG = pal[i];
        }
    }
}

void battle_art_load_banked(void)
{
    const volatile Battle *b = (const Battle *)g_bk_ptr_a;
    const char *want;
    uint8_t art_index = 0xFF;
    uint8_t art_frames = 0;
    uint8_t art_palette = 0;
    uint8_t art_oam = 0;
    uint8_t art_obj_palette = 0;
    uint8_t art_w = 3;
    uint8_t art_h = 2;
    uint16_t art_offset = 0;
    uint8_t base;
    uint8_t i, k;
    uint8_t match;
    uint16_t n;
    uint8_t cost;
    const uint8_t *src;
    volatile uint8_t *dst;

    if (!b) return;

    /* One shared row for the whole battle (clone encounters): the game
     * layer names the row id for this battle (same-bank plain call),
     * then the row is found by id scan over the compiled table. */
    want = game_battle_enemy_type_id(b->enemy_battle_id);
    if (want != 0) {
        for (i = 0; i < g_enemy_type_count; i++) {
            const EnemyTypeDef *t = g_enemy_types[i];
            match = 1;
            for (k = 0; ; k++) {
                if (t->id[k] != want[k]) {
                    match = 0;
                    break;
                }
                if (want[k] == '\0') break;
            }
            if (match) {
                art_index = t->art_index;
                art_frames = t->art_frames;
                art_palette = t->art_palette;
                art_oam = t->art_oam;
                art_obj_palette = t->art_obj_palette;
                art_w = t->art_w;
                art_h = t->art_h;
                art_offset = t->art_offset;
                break;
            }
        }
    }
    if (art_w == 0 || art_w > BATTLE_ART_MAX_W) art_w = 3;
    if (art_h == 0 || art_h > BATTLE_ART_MAX_H) art_h = 2;
    if (art_frames == 0 || art_frames > 2) art_frames = 1;

    /* Battle-time OBJ values (RAMPS/BATTLE table, slotless): OAM art
     * whose obj palette carries ART_OBJ_BATTLE_FLAG renders through
     * scratch OBJ slots 4+k, programmed here (LCD-off window) from the
     * generated battle_obj_tables.h rows and restored on overworld
     * return by ui_load_tileset_banked(). Static obj palettes (bats)
     * need nothing. Save is flag-guarded: mid-battle full redraws
     * re-run this loader and must reprogram, never re-save clobbered
     * CRAM (see battle.h invariant). */
    if (art_oam && (art_obj_palette & ART_OBJ_BATTLE_FLAG)) {
        if (!g_battle_obj_saved) {
            OCPS_REG = (uint8_t)(0x80 | (BATTLE_OBJ_SCRATCH_BASE << 3));
            for (i = 0; i < BATTLE_OBJ_SAVE_N; i++) {
                g_battle_obj_save[i] = OCPD_REG;
            }
            g_battle_obj_saved = 1;
        }
        program_battle_obj_palettes(
            (uint8_t)(art_obj_palette & (uint8_t)~ART_OBJ_BATTLE_FLAG));
    }

    VBK_REG = 0;
    base = BATTLE_ART_VRAM_BASE;
    for (k = 0; k < b->enemy_count && k < MAX_BATTLE_ENEMIES; k++) {
        g_battle_enemy_art[k] = art_index;
        g_battle_enemy_art_frames[k] = art_frames;
        g_battle_enemy_art_pal[k] = art_palette;
        g_battle_enemy_art_oam[k] = art_oam;
        /* Battle-time values render through scratch slots 4+k;
         * static palettes pass through untouched. */
        if (art_oam && (art_obj_palette & ART_OBJ_BATTLE_FLAG)) {
            g_battle_enemy_art_objpal[k] = (uint8_t)(BATTLE_OBJ_SCRATCH_BASE + k);
        } else {
            g_battle_enemy_art_objpal[k] = art_obj_palette;
        }
        g_battle_enemy_art_w[k] = art_w;
        g_battle_enemy_art_h[k] = art_h;
        g_battle_enemy_art_base[k] = 0;
        if (art_index == 0xFF) continue;
        /* Tile cost without multiply (8-bit * pulls mult routines into
         * fixed _CODE, §52.18): cost = w*h*frames via repeated addition
         * (h <= 4, frames <= 2). */
        cost = art_w;
        for (i = 1; i < art_h; i++) cost = (uint8_t)(cost + art_w);
        if (art_frames > 1) cost = (uint8_t)(cost + cost);
        if ((uint16_t)base + cost > BATTLE_ART_VRAM_BASE + BATTLE_ART_VRAM_TILES) {
            /* Past the art budget: text fallback for this slot. */
            g_battle_enemy_art[k] = 0xFF;
            continue;
        }
        /* Blob source without multiply: tiles -> bytes is a shift, and
         * the compiler already resolved the set's blob tile offset. */
        src = &battle_enemy_art[(uint16_t)(art_offset << 4)];
        dst = (volatile uint8_t *)(0x8000u + ((uint16_t)base << 4));
        n = (uint16_t)cost << 4;
        while (n--) {
            *dst++ = *src++;
        }
        g_battle_enemy_art_base[k] = base;
        base = (uint8_t)(base + cost);
    }
    for (; k < MAX_BATTLE_ENEMIES; k++) {
        g_battle_enemy_art[k] = 0xFF;
        g_battle_enemy_art_frames[k] = 0;
        g_battle_enemy_art_pal[k] = 0;
        g_battle_enemy_art_oam[k] = 0;
        g_battle_enemy_art_objpal[k] = 0;
        g_battle_enemy_art_w[k] = 3;
        g_battle_enemy_art_h[k] = 2;
        g_battle_enemy_art_base[k] = 0;
    }
}
