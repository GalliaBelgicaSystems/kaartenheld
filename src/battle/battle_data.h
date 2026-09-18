#ifndef BATTLE_DATA_H
#define BATTLE_DATA_H

#include <stdint.h>

/* Battle screen definition (matches generated battle_screens.c) */
typedef struct BattleScreenDef {
    const char *id;
    const char *label;
    uint8_t max_enemies;
    uint8_t allowed_categories;  // bit 0=minion, 1=elite, 2=boss
    uint8_t enemy_positions[3][2];
    uint8_t timer_overworld_ticks;
    uint8_t timer_battle_ticks;
    uint8_t turn_banner_row;
    uint8_t enemy_hp_row;
    uint8_t enemy_sprite_row;
    uint8_t enemy_cursor_row;
    uint8_t enemy_col_start;
    uint8_t enemy_col_step;
    uint8_t hero_label_row;
    uint8_t hero_label_col;
    uint8_t hero_hp_row;
    uint8_t hero_hp_col;
    uint8_t deck_row;
    uint8_t deck_col;
    uint8_t ap_row;
    uint8_t ap_col;
    uint8_t combo_row;
    uint8_t cards_row;
    uint8_t card_cursor_row;
    uint8_t card_desc_row;
    uint8_t timer_row;
    uint8_t timer_col;
    uint8_t timer_width;
    uint8_t hud_enemy_row_start;
    uint8_t hud_enemy_row_step;
    uint8_t hud_deck_row;
    uint8_t hud_combo_row_start;
    uint8_t hud_combo_row_step;
    uint8_t hud_timer_row;
    uint8_t hud_caret_x;
} BattleScreenDef;

/* Enemy type definition (matches generated battle_types.c) */
typedef struct EnemyTypeDef {
    const char *id;
    const char *label;
    uint8_t category;  // 0=minion, 1=elite, 2=boss
    const char *name;
    uint8_t base_hp;
    uint8_t base_max_hp;
    const char *battle_id;
    uint8_t gold_reward;
    uint8_t reward_currency;
    uint8_t art_index;  // battle art set (order position in screens/combat_art), 0xFF = text fallback
    uint8_t art_frames;  // animation frames (1..2, 0 when art_index is 0xFF)
    uint8_t art_palette;  // CGB battle palette (ui_color_* index; DMG ignores)
    uint8_t art_w;  // combat art width in tiles (3 for the built-in sets; up to 6)
    uint8_t art_h;  // combat art height in tiles (2 for the built-in sets; up to 4)
    uint16_t art_offset;  // blob tile offset of this set's frame0 in battle_enemy_art.h
    uint8_t ow_tile;  // shared overworld OAM base tile (ENEMY_OW_BASE + blob offset), 0xFF = legacy SPRITE_KIND path
    uint8_t ow_w;  // overworld sprite width in tiles (1 = single-tile sprite, 2 = 2x2 grid)
    uint8_t ow_h;  // overworld sprite height in tiles (1 = single-tile sprite, 2 = 2x2 grid)
    uint8_t ow_frames;  // overworld animation frames (cells / (ow_w*ow_h), 0 when ow_tile is 0xFF)
    uint8_t ow_palette;  // CGB OAM palette index
    uint8_t art_oam;  // combat art renders as OAM sprites (small enemies; boss stays BG-stamped)
    uint8_t art_obj_palette;  // CGB OAM palette index for OAM art (SLOTS/OBJ)
} EnemyTypeDef;

/* art_obj_palette with this bit set is an index into the RAMPS/BATTLE
 * values table (battle_obj_tables.h, programmed per battle entry into
 * scratch OBJ slots 4..6) instead of a static SLOTS/OBJ slot. Single
 * source of truth: battle_compile.py reads this value back with a
 * regex (hard failure if missing) so the two sides cannot drift. */
#define ART_OBJ_BATTLE_FLAG 0x80

/* WRAM cache for the active battle screen's HUD layout.
 * Staged from the bank-4 BattleScreenDef by game_battle_hud_load()
 * at every battle entry; read by the bank-3 renderer.
 * All fields are uint8_t.  Field names AND order mirror BattleScreenDef
 * from enemy_positions onward on purpose: the bank-4 loader copies
 * field-by-field with identical names, so a reorder/insert on either
 * side is a review-visible mismatch, not a silent shift.  Keep the two
 * structs in sync (the file-scope asserts in battle_hud_load_banked.c
 * and the battle_compile.py field-order check enforce this). */
typedef struct BattleHudCache {
    uint8_t enemy_positions[3][2];
    uint8_t timer_overworld_ticks;
    uint8_t timer_battle_ticks;
    uint8_t turn_banner_row;
    uint8_t enemy_hp_row;
    uint8_t enemy_sprite_row;
    uint8_t enemy_cursor_row;
    uint8_t enemy_col_start;
    uint8_t enemy_col_step;
    uint8_t hero_label_row;
    uint8_t hero_label_col;
    uint8_t hero_hp_row;
    uint8_t hero_hp_col;
    uint8_t deck_row;
    uint8_t deck_col;
    uint8_t ap_row;
    uint8_t ap_col;
    uint8_t combo_row;
    uint8_t cards_row;
    uint8_t card_cursor_row;
    uint8_t card_desc_row;
    uint8_t timer_row;
    uint8_t timer_col;
    uint8_t timer_width;
    uint8_t hud_enemy_row_start;
    uint8_t hud_enemy_row_step;
    uint8_t hud_deck_row;
    uint8_t hud_combo_row_start;
    uint8_t hud_combo_row_step;
    uint8_t hud_timer_row;
    uint8_t hud_caret_x;
} BattleHudCache;

/* WRAM cache for the active battle screen's HUD layout.
 * Staged from the bank-4 BattleScreenDef by game_battle_hud_load()
 * at every battle entry; read by the bank-3 renderer. */
extern BattleHudCache g_battle_hud;

/* Battle hand-card skin (screens/cards_skin.json via battle_compile.py):
 * per battle-card-type weapon icon tile + CGB palette, per element-status
 * icon tile + palette, and the card box geometry.  The generated const
 * lives in bank 4 (battle_types.c); battle_hud_load_banked() stages it
 * into this WRAM mirror at battle entry, and the bank-3 renderer reads
 * only the mirror (banked code must not call across banks). */
typedef struct CardSkinDef {
    uint8_t box_w;             /* card box width in tiles (3; hand stride is 4) */
    uint8_t box_h;             /* card box height in tiles (4: top..bottom rows) */
    uint8_t weapon_tile[5];    /* BATTLE_CARD_TYPE 0..4 -> VRAM weapon icon tile */
    uint8_t weapon_color[5];   /* per-type box/icon CGB palette (UI_COLOR_*) */
    uint8_t elem_tile[4];      /* status 0=NONE(blank) 1=POISON 2=BURN 3=FREEZE -> icon tile */
    uint8_t elem_color[4];     /* status icon CGB palette */
    uint8_t uses_type;         /* BATTLE_CARD_TYPE whose finite-use cards draw
                                  the arrow counter on the floor row; 0xFF = none */
    uint8_t uses_tile[5];      /* remaining uses 0..4 (clamped) -> arrow icon tile */
    uint8_t uses_power_tile;   /* power-row glyph for the uses_type (nine icon) */
} CardSkinDef;

extern const CardSkinDef g_card_skin;   /* generated (bank 4) */
extern CardSkinDef g_card_skin_wram;    /* staged mirror (WRAM) */

/* Battle HUD skin (screens/battle_hud.json via battle_compile.py):
 * hero-HP / AP / deck icon tiles + CGB palettes, and the turn-timer bar
 * geometry (segment tiles, color, row, width).  Same staging contract as
 * the card skin: generated const in bank 4 (hud_skin.c), mirrored into
 * WRAM by battle_hud_load_banked() at battle entry; the fixed-bank timer
 * draw and the bank-3 renderer read only the mirror. */
typedef struct HudSkinDef {
    uint8_t hp_icon_tile;      /* heart icon (VRAM tile) */
    uint8_t hp_icon_color;     /* UI_COLOR_* */
    uint8_t ap_icon_tile;      /* lightning-bolt icon */
    uint8_t ap_icon_color;
    uint8_t deck_icon_tile;    /* deck-stack icon */
    uint8_t deck_icon_color;
    uint8_t bar_filled_tile;   /* segment tile when the timer has time left */
    uint8_t bar_empty_tile;    /* segment tile when drained */
    uint8_t bar_color;         /* palette for the filled span */
    uint8_t bar_row;           /* tilemap row 0..17 */
    uint8_t bar_width;         /* segments, 1..20 */
} HudSkinDef;

extern const HudSkinDef g_hud_skin;     /* generated (bank 4) */
extern HudSkinDef g_hud_skin_wram;      /* staged mirror (WRAM) */


/* Generated data declarations (bank 4) */
extern const BattleScreenDef* const g_battle_screens[];
extern const uint8_t g_battle_screen_count;

extern const EnemyTypeDef* const g_enemy_types[];
extern const uint8_t g_enemy_type_count;
/* Shared overworld enemy OAM blob size in tiles (for the ui_init stream). */
extern const uint8_t g_enemy_ow_tile_count;

#endif /* BATTLE_DATA_H */