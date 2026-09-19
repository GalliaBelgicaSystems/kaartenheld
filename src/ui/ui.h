#ifndef UI_H
#define UI_H

#include "world.h"
#include "battle.h"
#include "dialogue.h"
#include <stdint.h>

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;

/* CGB present flag (detected at ui_init).  Gates VRAM bank-1 attribute
 * writes; DMG = 0. */
extern uint8_t g_is_cgb;

/* Weapon, element & UI icon tile indices (VRAM Block 1, 0x8800) */
#define UI_TILE_CARD_SWORD       104u
#define UI_TILE_CARD_SHIELD      105u
#define UI_TILE_CARD_BOW         106u
#define UI_TILE_CARD_DAGGER      107u
#define UI_TILE_CARD_RING        108u
#define UI_TILE_CARD_AMULET      109u
#define UI_TILE_CARD_ELEM_FIRE   110u
#define UI_TILE_CARD_ELEM_ICE    111u
#define UI_TILE_CARD_ELEM_POISON 112u
#define UI_TILE_HEART            113u
#define UI_TILE_BOLT             114u
#define UI_TILE_COIN             115u
#define UI_TILE_DECK             116u
/* Turn-timer bar segment tiles (HUD skin; generated card_frame_tiles.h
 * sheet row 3, loaded explicitly at these ids by ui_init).  Together with
 * the card frames these fully allocate the 117-127 BG window between the
 * icon block (104-116) and the world / battle-art blocks (128+). */
#define UI_TILE_TIMER_FILLED     117u
#define UI_TILE_TIMER_EMPTY      127u
/* Up-arrow select icon (combat tileset "arrow pointing up", card_frames
 * sheet tile 14): replaces the '^' font caret on the battle marker /
 * target rows.  BG tile ids 96-103 are the only free block-1 slots
 * (font 0-95, atlas 104-116); the BG never referenced them before. */
#define UI_TILE_SELECT_ARROW     96u
/* Battle hand-card frame tiles (VRAM Block 1, 0x8800): 9 tiles in frame
 * order TL TM TR / L C R / BL BM BR (generated card_frame_tiles.h from
 * assets/card_frames.png).  Sits between the icon block (104-116) and the
 * world / battle-art blocks (128+). */
#define UI_TILE_CARD_FRAME_BASE  118u
#define UI_TILE_CARD_FRAME_TL    118u
#define UI_TILE_CARD_FRAME_TM    119u
#define UI_TILE_CARD_FRAME_TR    120u
#define UI_TILE_CARD_FRAME_L     121u
#define UI_TILE_CARD_FRAME_C     122u
#define UI_TILE_CARD_FRAME_R     123u
#define UI_TILE_CARD_FRAME_BL    124u
#define UI_TILE_CARD_FRAME_BM    125u
#define UI_TILE_CARD_FRAME_BR    126u

/* Overworld actor OAM sprite tile bases (VRAM Block 0, 0x8000).  Collision
 * rules: must not overlap the font-duplicate block 0..95 used for ASCII
 * actor glyphs, PLAYER (102), HERO (98), or the kobold/bat bases. */
#define HERO_DESOLATE_SPRITE_TILE_ID 98u
#define PLAYER_SPRITE_TILE_ID    HERO_DESOLATE_SPRITE_TILE_ID
#define KOBOLD_SPRITE_TILE_ID    96u
#define BAT_DESOLATE_SPRITE_TILE_ID 88u
#define BAT_CASTLE_SPRITE_TILE_ID   92u
/* Chest pickup sprite (single-frame art loaded into both anim slots). */
#define CHEST_SPRITE_TILE_ID     94u
/* Shared per-enemy overworld sprites (assets/enemy_sprites.png, base must
 * match ENEMY_OW_BASE in tools/screen_compiler/battle_compile.py).  The
 * blob holds concatenated per-enemy frames; ids 128+ alias BG tiles. */
#define ENEMY_OW_BASE            100u
#define ENEMY_OW_LIMIT           128u

/* Max shadow-OAM entries the actor pipeline can touch: player (entry 0),
 * hostile actors up to MAX_WORLD_ACTORS each a w*h sprite (max 2x2 = 4),
 * then static actors.  Used by the transition-hide sweep. */
#define OAM_MAX_ACTOR_ENTRIES     (1u + (MAX_WORLD_ACTORS * 4u) + MAX_STATIC_ACTORS)

/* Bank-4 no-arg body behind ui_draw_actors_sprites(): writes each active
 * non-boss actor's shadow-OAM entry (position/tile/prop from SPRITE_KIND_*)
 * and the castle boss 2x2 background block.  Lives in bank 4 to keep the
 * fixed bank under 0x8000. */
void ui_actors_sprites_banked(void);

/* Per-tile background palette indices (CGB VRAM bank-1 attributes):
 * 0 = default grayscale, 1 = fire, 2 = iron (steel blue), 3 = field green
 * (forest ground/foliage; repurposed from the unused heal slot), 4 = poison
 * (emerald), 5 = wood (brown), 6 = gold (mythril), 7 = dim (poison grey-out
 * and desolate wasteland ground). */
#define UI_COLOR_NONE   0
#define UI_COLOR_FIRE   1
#define UI_COLOR_IRON   2
#define UI_COLOR_ICE    2
#define UI_COLOR_FIELD  3
#define UI_COLOR_POISON 4
/* Paper: CRAM slot 4 re-programmed to a white/black document ramp while a
 * dialogue box is open (no world tileset assigns slot 4 to any tile, and
 * the quick screen -- its only other consumer -- cannot be open during a
 * dialogue).  Every screen transition re-programs CRAM, which restores
 * the set's own slot-4 ramp. */
#define UI_COLOR_PAPER  4
#define UI_COLOR_WOOD   5
#define UI_COLOR_GOLD   6
#define UI_COLOR_DIM    7

/* Select-arrow cell (enemy target caret + card cursor marker).  The arrow
 * tile is brown on white, which only the field ramp fits.  Single source
 * for the two battle render sites AND the host palette compiler
 * (tools/palette_compiler.py reads this define for the card display
 * mapping), so encode and display cannot drift.  Kept as an engine
 * constant rather than a HUD-skin field: growing a battle WRAM struct
 * shifts BSS and flips an SDCC layout-sensitive miscompile (AGENTS.md
 * 52.19), which broke the game_over_quit sentinel. */
#define UI_COLOR_ARROW  UI_COLOR_FIELD

/* Effect color for a card (status_id = on-hit rider element, is_heal =
 * ring/heal role).  Returns a UI_COLOR_* palette index.  status_id uses
 * the STATUS_* enum values (STATUS_POISON=1, STATUS_BURN=2, STATUS_FREEZE=3)
 * -- keep in sync with battle_card_color() in ui_battle_content.c. */
#define ui_color_card(bt, st, ih) ( \
    ((st) == STATUS_BURN) ? (uint8_t)UI_COLOR_FIRE : \
    ((st) == STATUS_POISON) ? (uint8_t)UI_COLOR_POISON : \
    ((st) == STATUS_FREEZE) ? (uint8_t)UI_COLOR_ICE : \
    (((bt) == 1 /* BATTLE_CARD_TYPE_SHIELD */) || (ih)) ? (uint8_t)UI_COLOR_WOOD : \
    ((bt) == 0 /* BATTLE_CARD_TYPE_SWORD */) ? (uint8_t)UI_COLOR_IRON : \
    ((bt) == 2 /* BATTLE_CARD_TYPE_BOW */) ? (uint8_t)UI_COLOR_GOLD : \
    ((bt) == 4 /* BATTLE_CARD_TYPE_DAGGER */) ? (uint8_t)UI_COLOR_POISON : (uint8_t)UI_COLOR_NONE \
)

#define ui_color_class(st, ih) ui_color_card(0, (st), (ih))

/* Set the CGB per-tile palette for a horizontal span of background tiles
 * (no-op on DMG; palette 0 resets to grayscale). */
void ui_color_span(uint8_t x, uint8_t y, uint8_t len, uint8_t palette);

/* Bank-3 no-arg body dispatched by ui_lcd_off() (src/ui/ui_color_banked.c);
 * clears every BG tilemap attribute byte back to palette 0. */
void ui_clear_atts_banked(void);

/* Bank-3 DEBUG-only body behind ui_draw_font_test(); prints the full ASCII
 * font to the tilemap.  Lives in bank 3 to keep the debug fixed bank under
 * 0x8000. */
void ui_draw_font_test_banked(void);

/* Player rendered as a real OAM sprite.  Background stays console-font
 * ASCII; only the player is a hardware sprite.  Deliberately scoped to the
 * ui layer: no other module needs to know the player is a sprite instead
 * of a printed '@'. */
void ui_sprite_init(void);
void ui_sprite_move(uint8_t px, uint8_t py);
void ui_sprite_hide(void);
void ui_sprite_hide_actors_below(uint8_t screen_y);
void ui_sprite_begin_transition(void);
void ui_sprite_commit(void);
void oam_dma_init(void);

void ui_init(void);
void ui_clear_screen(void);
void ui_set_cram_palette(uint8_t overworld);
void ui_load_cram_banked(void);
void ui_load_tileset_banked(void);
/* Title-logo loader (build-dependent bank: 5 debug / 2 release): streams the
 * 48 logo tiles (assets/title-red.png,
 * make gfx) into the world BG block (ids 128-175).  Runs in the title
 * screen's LCD-off full redraw; AGENTS.md 52.22 signed 0x8800 fetch. */
void ui_title_logo_load_banked(void);
/* Bank-2 studio splash body: streams the deduped logo tiles
 * (assets/gallia_belgica_systems.png, make gfx) into the world BG block
 * (ids 128-192), stamps the 13x5 map, and programs CGB palette 1 to the
 * logo's fixed ramp. */
void ui_splash_logo_render_banked(void);
extern uint8_t g_active_tile_palette[48];

/* Toggle LCDC bit 7 directly (harness-safe: no GBDK display_off VBlank
 * wait).  Full-screen redraws span several display sweeps and cannot fit
 * in one VBlank, so every full redraw runs with the LCD off and all VRAM
 * writes land deterministically. */
void ui_lcd_off(void);
void ui_lcd_on(void);

void ui_load_tileset(uint8_t tileset);
void ui_invalidate_tileset(void);
void ui_draw_world_map(const World *world);
void ui_draw_world_full(const World *world);
void ui_draw_actors_sprites(const World *world);
void ui_draw_text_line(uint8_t x, uint8_t y, const char *text, uint8_t max_chars);
void ui_draw_num2(uint8_t x, uint8_t y, uint8_t val);
void ui_draw_hline(uint8_t y, char ch);

/* Set SCX/SCY from the overworld camera pixel position.  Called every
 * overworld frame so the background glides smoothly. */
void ui_update_camera(const World *world);

/* Write value as a decimal string into out (at least 7 bytes).  Avoids the
 * stdio/console chain so _HOME stays under 0x8000. */
void ui_format_int(int16_t value, char *out);

/* Write "XYn" — two-letter battle code ("SW"/"SH"/"BO"/"HE"/"DA" per
 * BattleCardType in src/battle/card.h) plus power digit — into out
 * (at least 4 bytes); "??" when battle_type is out of range. */
void ui_card_code_str(uint8_t battle_type, uint8_t power, char *out);

/* Ones digit of `v` as a character, computed by subtraction: SM83 has
 * no hardware divide and the SDCC divmod library would land in the
 * tight fixed bank (AGENTS.md 52.18).  Use instead of `% 10`. */
char ui_ones_digit(uint8_t v);

/* Banked no-arg body (ROM bank 2) dispatched by ui_format_int(). */
void ui_format_int_banked(void);

/* Bank-3 no-arg body dispatched by ui_color_span() (src/ui/ui_color_banked.c);
 * reads its staged args from g_bk_ptr_a. */
void ui_color_span_banked(void);

void ui_draw_dialogue(const DialogueState *dialogue, uint8_t scroll_x, uint8_t scroll_y);
void ui_draw_dialogue_line(uint8_t x, uint8_t y, const char *text,
                           uint8_t max_chars, uint8_t ox, uint8_t oy);
void ui_draw_battle_full(const Battle *battle);
void ui_update_battle(const Battle *battle);
void ui_update_battle_banked(void);
void ui_draw_battle_timer(const Battle *battle);
void ui_draw_battle_timer_banked(void);
uint8_t ui_calc_timer_bar(uint16_t t);

/* Bank-3 no-arg body behind ui_init's battle-UI tile load (card frames +
 * bar segments + HUD icons from card_frame_tiles.h); dispatched via
 * banked_call_run() with the LCD off. */
void ui_card_tiles_load_banked(void);

void ui_draw_font_test(void);

#endif /* UI_H */
