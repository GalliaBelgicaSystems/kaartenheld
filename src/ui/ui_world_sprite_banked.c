#pragma bank 4

#include <stdint.h>
#include "world.h"
#include "actor.h"
#include "ui.h"
#include "banked.h"
#include "battle_data.h"
#include "gfx/rpg_tile_lookup.h"
#include "gfx/enemy_ow_tiles.h"
#include "anim_pairs.h"

/* ── Overworld actor sprites + castle boss background (bank-3 body) ──
 * The data-driven SPRITE_KIND_* OAM choice, the per-actor shadow-OAM write
 * (position + tile + prop) and the castle-boss 2x2 background-tile draw live
 * in bank 3 (like ui_color_banked) so the fixed-bank ui.c stays under
 * 0x8000 (AGENTS.md §52.18 / §55.5).  ui_draw_actors_sprites() dispatches
 * this one banked pass per frame via the staging globals; the caller only
 * stages the World pointer and reads nothing back (shadow OAM 0xC000 is
 * WRAM, always mapped). */

#ifdef DEBUG_BUILD
extern uint8_t g_tilemap_mirror[32 * 32];
#endif

/* Bat OAM sprite tile data (2 frames), shared by the desolate and castle
 * tileset variants (identical pixel art).  Lives in bank 3 so the fixed
 * bank's _CODE does not carry 64 bytes of sprite data; ui_init() copies it
 * into WRAM via banked_copy and set_sprite_data()s both tile IDs. */
const uint8_t s_bat_tiles[32] = {
    0x00, 0x00, 0xA0, 0xA0, 0x4A, 0x4A, 0x04, 0x04,
    0x00, 0x00, 0x28, 0x28, 0x10, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x0A, 0x0A, 0xA4, 0xA4, 0x40, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x28, 0x28, 0x10, 0x10
};

/* Shadow OAM WRAM base (GBDK's global `shadow_OAM` at 0xC000).  Each entry
 * is 4 bytes: y, x, tile, attr.  Written directly (WRAM is always mapped)
 * so the fixed-bank caller need not loop over actors. */
#define SHADOW_OAM_BASE 0xC000u
#define OAM_SLOT_ACTOR0 1u  /* slot 0 -> OAM entry 1 (entry 0 is the player) */
/* (Static actors follow the hostile block via a running cursor; their base
 * is OAM_SLOT_ACTOR0 + the sum of each hostile's w*h sprite cells, which
 * equals OAM_SLOT_ACTOR0 + MAX_WORLD_ACTORS when every hostile is 1x1.) */

/* Sub-tile position of an actor mid-step (replica of world_actor_px/py,
 * see src/world/px_banked.c).  bank-3 bodies must not call fixed-bank code. */
static uint8_t spr_axis_px(const WorldActorRuntime *a, uint8_t axis)
{
    uint8_t ax = (axis == 0) ? a->x : a->y;
    uint8_t tx = (axis == 0) ? a->move_target_x : a->move_target_y;
    uint8_t pr = a->move_progress;
    uint8_t px = (uint8_t)(ax << 3);
    if (a->move_state) {
        if (tx > ax) return (uint8_t)(px + pr);
        if (tx < ax) return (uint8_t)(px - pr);
    }
    return px;
}

/* SPRITE_KIND_* -> OAM tile/prop.  Single source for both loops below.
 * Bats use the grey ramp (black bodies); kobolds and chests use the brown
 * wood palette (OBJ palette 2); the chest art is 1 frame in both anim
 * slots, so + anim stays uniform.  SPRITE_KIND_ENEMY reads the actor's
 * enemy-type row (same bank: direct read) for its shared OAM base,
 * frames, and palette; unknown rows fall back to the ASCII glyph. */
static uint8_t sprite_tile_for(uint8_t kind, uint8_t visual, uint8_t castle,
                                uint8_t anim, uint8_t ow_type, uint8_t *prop)
{
    switch (kind) {
        case SPRITE_KIND_KOBOLD:
            *prop = 2;
            return (uint8_t)(KOBOLD_SPRITE_TILE_ID + anim);
        case SPRITE_KIND_BAT:
            *prop = 0;
            return (uint8_t)((castle ? BAT_CASTLE_SPRITE_TILE_ID
                                     : BAT_DESOLATE_SPRITE_TILE_ID) + anim);
        case SPRITE_KIND_CHEST:
            *prop = 2;
            return (uint8_t)(CHEST_SPRITE_TILE_ID + anim);
        case SPRITE_KIND_ENEMY:
            if (ow_type < g_enemy_type_count) {
                const EnemyTypeDef *t = g_enemy_types[ow_type];
                if (t->ow_tile != 0xFF) {
                    *prop = t->ow_palette;
                    if (t->ow_frames > 1) {
                        return (uint8_t)(t->ow_tile + anim);
                    }
                    return t->ow_tile;
                }
            }
            *prop = 0;
            return (uint8_t)(ui_font_tile_base + (uint8_t)(visual - ' '));
        case SPRITE_KIND_BOSS:
        case SPRITE_KIND_TILE: /* background-art: cell owns the visuals */
            *prop = 0;
            return 0;
        default: /* SPRITE_KIND_ASCII */
            *prop = 0;
            return (uint8_t)(ui_font_tile_base + (uint8_t)(visual - ' '));
    }
}

/* Campfire flicker, data-driven (generated anim_pairs.h): every frame,
 * cells showing a *_fire_frame tile take the step's frame (A on even
 * steps, B on odd).  Idempotent full-coverage write, so freshly scrolled
 * cells settle within one frame.  Runs from ui_actors_sprites_banked,
 * i.e. overworld + dialogue-over-overworld only -- battle never calls
 * this path, so battle art tiles can't collide.  Frame pairs share one
 * glyph/category, so the semantic screen buffer is untouched; the DEBUG
 * tilemap mirror is kept in sync for the harness.  VRAM bank is left
 * alone (tilemap writes assume bank 0, like the boss block below). */
static void world_anim_swap_banked(const World *w, uint8_t anim)
{
    (void)w;
    (void)anim;
}

/* Compute the OAM tile/prop/position for every active non-boss actor (from
 * its SPRITE_KIND_* and sub-tile position) and write it straight into shadow
 * OAM; static (non-hostile) actors with sprite art follow in the entries
 * after the hostile slots, while ASCII-kind statics keep their background
 * glyph (see ui_draw_world_cell) and stay hidden here.  The castle boss is
 * drawn into the background tilemap as a 2x2 block instead.
 * Args: g_bk_ptr_a = const World * (WRAM), g_bk_byte_a = castle flag,
 * g_bk_byte_b = anim_step.  Inactive and SPRITE_KIND_BOSS actors get their
 * OAM entries hidden (y=0).
 *
 * OAM layout: a running cursor starts at OAM_SLOT_ACTOR0 and advances by
 * the actor's sprite size (1 for single-tile actors, w*h for a SPRITE_KIND
 * _ENEMY grid like the 2x2 boss).  When every hostile is 1x1 the cursor
 * reproduces the historic fixed layout exactly (statics at
 * OAM_SLOT_ACTOR0 + MAX_WORLD_ACTORS). */
void ui_actors_sprites_banked(void)
{
    const World *w = (const World *)g_bk_ptr_a;
    uint8_t castle = g_bk_byte_a;
    uint8_t anim = g_bk_byte_b;
    uint8_t slot;
    uint8_t i;
    uint8_t oam_slot = OAM_SLOT_ACTOR0;  /* running OAM entry cursor */

    if (!w) return;
    for (slot = 0; slot < MAX_WORLD_ACTORS; slot++) {
        const WorldActorRuntime *a = &w->actors[slot];
        uint8_t tile = 0;
        uint8_t prop = 0;
        uint8_t px, py;
        uint8_t gw, gh;
        uint8_t gx, gy;

        if (a->active) {
            tile = sprite_tile_for((uint8_t)a->sprite_kind, a->visual,
                                   castle, anim, a->ow_type, &prop);
        }
        /* Multi-tile shared enemy sprite: replace the single tile with the
         * grid base + palette and write w*h OAM entries.  The blob is
         * frame-major: the frame offset is anim*cell_count. */
        gw = 1;
        gh = 1;
        if ((int)a->sprite_kind == SPRITE_KIND_ENEMY &&
            a->ow_type < g_enemy_type_count) {
            const EnemyTypeDef *t = g_enemy_types[a->ow_type];
            if (t->ow_tile != 0xFF && (t->ow_w > 1 || t->ow_h > 1)) {
                uint8_t ccount;
                gw = t->ow_w;
                gh = t->ow_h;
                tile = t->ow_tile;
                prop = t->ow_palette;
                /* cell_count = gw*gh without mult (w,h <= 2) */
                ccount = (gh == 2) ? (gw == 2 ? 4 : 2) : gw;
                if (t->ow_frames > 1 && anim) {
                    tile = (uint8_t)(tile + ccount);
                }
            }
        }
        if (tile) {
            px = (uint8_t)(spr_axis_px(a, 0) - w->camera_px_x);
            py = (uint8_t)(spr_axis_px(a, 1) - w->camera_px_y);
            if ((int)a->sprite_kind != SPRITE_KIND_BOSS) {
                uint8_t row = 0;
                for (gy = 0; gy < gh; gy++) {
                    uint8_t col = 0;
                    for (gx = 0; gx < gw; gx++) {
                        volatile uint8_t *e = (volatile uint8_t *)(SHADOW_OAM_BASE +
                                                                   ((uint16_t)(oam_slot + row + col) << 2));
                        uint8_t tx = (uint8_t)(px + (gx << 3));
                        uint8_t ty = (uint8_t)(py + (gy << 3));
                        if (tx < 160 && ty < 144) {
                            e[0] = (uint8_t)(ty + 16);
                            e[1] = (uint8_t)(tx + 8);
                            e[2] = (uint8_t)(tile + row + col);
                            e[3] = prop;
                        } else {
                            e[0] = 0;
                        }
                        col = (uint8_t)(col + 1);
                    }
                    row = (uint8_t)(row + gw);
                }
                /* w*h entries (no mult: w,h <= 2) */
                oam_slot = (uint8_t)(oam_slot + (gh == 2 ? (gw == 2 ? 4 : 2) : gw));
                continue;
            }
        }
        {
            volatile uint8_t *e = (volatile uint8_t *)(SHADOW_OAM_BASE +
                                                       ((uint16_t)oam_slot << 2));
            e[0] = 0;  /* hidden */
        }
        oam_slot = (uint8_t)(oam_slot + (gh == 2 ? (gw == 2 ? 4 : 2) : gw));
    }
    for (i = 0; i < MAX_STATIC_ACTORS; i++) {
        const StaticActorDefinition *d;
        volatile uint8_t *e = (volatile uint8_t *)(SHADOW_OAM_BASE +
                                                   ((uint16_t)(oam_slot + i) << 2));
        uint8_t tile;
        uint8_t prop = 0;
        uint8_t px, py;
        if (i >= g_static_actor_count) {
            e[0] = 0;  /* no static here: hide any stale sprite */
            continue;
        }
        d = &g_static_actors[i];
        if (d->sprite_kind == SPRITE_KIND_ASCII ||
            d->sprite_kind == SPRITE_KIND_BOSS ||
            d->sprite_kind == SPRITE_KIND_TILE) {
            e[0] = 0;  /* glyph / boss / background-art actors own these */
            continue;
        }
        tile = sprite_tile_for((uint8_t)d->sprite_kind, d->visual,
                               castle, anim, d->ow_type, &prop);
        if (!tile) {
            e[0] = 0;
            continue;
        }
        px = (uint8_t)((uint8_t)(d->x << 3) - w->camera_px_x);
        py = (uint8_t)((uint8_t)(d->y << 3) - w->camera_px_y);
        if (px < 160 && py < 144) {
            e[0] = (uint8_t)(py + 16);
            e[1] = (uint8_t)(px + 8);
            e[2] = tile;
            e[3] = prop;
            continue;
        }
        e[0] = 0;  /* hidden */
    }
    /* Castle boss is drawn directly into the background tilemap as a 2x2
     * block (castle tile indexes 7,8,16,17), not an OAM sprite. */
    if (castle) {
        for (slot = 0; slot < MAX_WORLD_ACTORS; slot++) {
            const WorldActorRuntime *a = &w->actors[slot];
            uint8_t ox, oy;
            volatile uint8_t *tilemap = (volatile uint8_t *)0x9800;
            if (!a->active) continue;
            if ((int)a->sprite_kind != SPRITE_KIND_BOSS) continue;
            ox = a->x;
            oy = a->y;
            {
                uint8_t sx = (uint8_t)(ox - w->scroll_x);
                uint8_t sy = (uint8_t)(oy - w->scroll_y);
                tilemap[(oy & 31) * 32 + (ox & 31)] = (uint8_t)(RPG_TILE_BASE_WORLD + 7);
                tilemap[(oy & 31) * 32 + ((ox + 1) & 31)] = (uint8_t)(RPG_TILE_BASE_WORLD + 8);
                tilemap[((oy + 1) & 31) * 32 + (ox & 31)] = (uint8_t)(RPG_TILE_BASE_WORLD + 16);
                tilemap[((oy + 1) & 31) * 32 + ((ox + 1) & 31)] = (uint8_t)(RPG_TILE_BASE_WORLD + 17);
#ifdef DEBUG_BUILD
                g_tilemap_mirror[(oy & 31) * 32 + (ox & 31)] = (uint8_t)(RPG_TILE_BASE_WORLD + 7);
                g_tilemap_mirror[(oy & 31) * 32 + ((ox + 1) & 31)] = (uint8_t)(RPG_TILE_BASE_WORLD + 8);
                g_tilemap_mirror[((oy + 1) & 31) * 32 + (ox & 31)] = (uint8_t)(RPG_TILE_BASE_WORLD + 16);
                g_tilemap_mirror[((oy + 1) & 31) * 32 + ((ox + 1) & 31)] = (uint8_t)(RPG_TILE_BASE_WORLD + 17);
                if (sx < (uint8_t)(WORLD_VIEW_W - 1) && sy < (uint8_t)(WORLD_VIEW_H - 1)) {
                    g_ui_screen_buf[sy][sx] = (char)a->visual;
                    g_ui_screen_buf[sy][sx + 1] = (char)a->visual;
                    g_ui_screen_buf[sy + 1][sx] = (char)a->visual;
                    g_ui_screen_buf[sy + 1][sx + 1] = (char)a->visual;
                }
#endif
            }
            break;
        }
    }
    /* Data-driven campfire flicker (generated anim_pairs.h). */
    world_anim_swap_banked(w, anim);
}