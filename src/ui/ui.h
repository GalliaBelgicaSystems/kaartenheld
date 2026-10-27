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

/* Weapon & UI icon tile indices (VRAM Block 1, 0x8800).  Element riders
 * are OAM sprites (rider_tiles_generated.h), never BG tiles. */
#define UI_TILE_CARD_SWORD       104u
#define UI_TILE_CARD_SHIELD      105u
#define UI_TILE_CARD_BOW         106u
#define UI_TILE_CARD_DAGGER      107u
#define UI_TILE_CARD_RING        108u
/* Turn-timer bar segment tiles (HUD skin; generated card_frame_tiles.h
 * sheet row 3, loaded explicitly at these ids by ui_init).  Together with
 * the card frames these fully allocate the 117-127 BG window between the
 * icon block (104-116) and the world / battle-art blocks (128+). */
#define UI_TILE_TIMER_FILLED     117u
#define UI_TILE_TIMER_EMPTY      127u
/* Up-arrow select icons (combat tileset "arrow pointing up light/dark",
 * card_frames sheet tiles 14/15): the light arrow is the cursor on a plain
 * card (and the enemy-target caret); the dark arrow stays on a card that is
 * already in the combo (no auto-advance after select).  Both replace the
 * '^' font caret on the battle marker / target rows.  BG tile ids 96-103
 * are the only free block-1 slots (font 0-95, atlas 104-116); the BG never
 * referenced them before. */
#define UI_TILE_SELECT_ARROW     96u
#define UI_TILE_SELECT_ARROW_DARK 110u
/* Battle AP-badge dot (combat_ap_dot, card-frames sheet cell 16): tiny
 * brown square stamped after each AP number (energy and max). */
#define UI_TILE_AP_DOT 111u
/* Ring power-slot X (combat_x_icon, card-frames sheet tile 23): rings are
 * jokers with no fixed value, so the hand-card middle row shows X while
 * the floor row keeps the remaining-uses counter (material-band uses).
 * Slot 115 is the retired coin tile (gold prices render as font 'G'). */
#define UI_TILE_RING_X 115u
/* Dialogue continue chevron (down-pointing): the intrepid ">" glyph
 * (font tile 30) rotated 90 degrees clockwise, loaded at boot by the
 * bank-3 card-tile loader into this free BG slot.  Shown at the far
 * bottom right (col 19, row 17) of the third full-width text row
 * whenever another dialogue page follows; hidden on the last page.  Slot 109 is the only free id in 96-127
 * (96 light arrow, 97-103 bow counters, 104-108 weapons, 110-112 dark
 * arrow + blanks, 113/114/116 HUD, 115 coin, 117/127 bar, 118-126
 * frames): its same-numbered OAM id lives in the 0x8000 sprite block,
 * a different physical address (AGENTS.md 52.22).  The semantic screen
 * buffer keeps 'v' for harness text assertions. */
#define UI_TILE_DIALOGUE_CONTINUE 109u
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

/* Chuck-a-luck / battle dice digits 0-9 (assets/dice.png, digit order
 * 0-9; NOT boot-loaded -- those ids sit in the battle-art block).  The
 * bank-8 gamble body streams them on demand to the high end of the
 * battle-art block (246-255), clear of the 128+ enemy-art growth (max
 * ~36 tiles for 3 enemies).  Tile id = UI_TILE_DICE_BASE + digit. */
#define UI_TILE_DICE_BASE        246u
#define UI_TILE_DICE_COUNT       10u

/* Bank-8 body streaming gfx/dice_tiles.h (assets/dice.png, digit order
 * 0-9) to UI_TILE_DICE_BASE (246-255).  Same-bank data; called by the
 * gamble entry body while the LCD is off (plain same-bank call, no
 * trampoline). */
void dice_tiles_load_banked(void);

/* Overworld actor OAM sprite tile bases (VRAM Block 0, 0x8000).  Collision
 * rules: must not overlap the font-duplicate block 0..95 used for ASCII
 * actor glyphs, PLAYER (102) or HERO (98). */
#define HERO_DESOLATE_SPRITE_TILE_ID 98u
#define PLAYER_SPRITE_TILE_ID    HERO_DESOLATE_SPRITE_TILE_ID
/* Standing-hero idle frames (hero_idle_1/2, streamed at boot beside the
 * walk frames): OAM 96-97, time-shared with the battle AP pips (boot and
 * overworld show idle; battle entry streams pips over them, overworld
 * entry restores idle -- see ui_draw_actors_sprites).  Battle never needs
 * idle art (hero hidden), overworld never needs pips. */
#define HERO_IDLE_SPRITE_TILE_ID 96u
/* Pickup-chest OAM sprite art (shared overworld blob, base + open frame;
 * owned by screens/enemy_types/treasure_chest.json).  Collision rules:
 * must not overlap the font-duplicate block 0..95 used for ASCII
 * actor glyphs, PLAYER (102) or HERO (98). */
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

/* Overworld-entry OAM restore (see ui.c): restreams time-shared overworld
 * art (hero idle + blob tail) after battles.  Called from screen_change()
 * on overworld entries. */
void ui_restore_ow_oam(void);

/* Bank-5 no-arg body behind ui_draw_actors_sprites(): swaps background
 * cells showing an animation frame tile to the current anim step's frame
 * (fire flicker / water shimmer, data-driven from generated
 * anim_pairs.h).  Staged inputs ride along (g_bk_ptr_a = World,
 * g_bk_byte_b = anim_step).  Walks the recorded index list (see
 * g_anim_cells), NOT a viewport scan.  Lives in bank 8 for both builds
 * (bank 7 filled up with the end-credits theme; the release world bank
 * filled up when the castle tile window grew past 17 slots; bank 4,
 * where its caller lives, is full, and bank 2 overflows with it);
 * dispatched as a second pass from fixed code
 * because a banked body must never switch banks mid-function. */
void world_anim_swap_banked(void);

/* Bank-8 no-arg rebuild behind ui_draw_world_map(): re-records the ring
 * indices of animation-frame cells after every full world redraw (the
 * ring's only writer).  Needs no staging.  Same bank/why as the swap. */
void world_anim_rebuild_banked(void);

/* Bank-5 viewport-union black wipe behind ui_draw_world_transition():
 * blackens CGB slot 4, then paints the union of the displayed and the
 * incoming viewport (see tiles_content.c).  The fixed driver runs it
 * once; the writes themselves span a display frame, sweeping top-down
 * in write order by themselves. */
void ui_wipe_black_banked(void);

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
void ui_sprite_begin_transition(void);
/* Aliases (macros, not second fixed-bank functions: the debug fixed bank
 * has single-digit headroom).  hide == the transition hide; commit == the
 * frame-boundary shadow-OAM DMA. */
#define ui_sprite_hide() ui_sprite_begin_transition()
#define ui_sprite_commit() refresh_OAM()
void ui_sprite_hide_actors_below(uint8_t screen_y);
#ifdef DEBUG_BUILD
/* Transition-hide execution counter (verify-oam): incremented by
 * ui_sprite_begin_transition().  Lets the OAM gate assert the hide RAN
 * without halting inside a banked ROM window -- mGBA matches those
 * breakpoints bank-agnostically (false hits) AND cannot resume past
 * them (`c`/`frame` re-trap instantly), so window-address breaks are
 * unusable here (AGENTS.md 52.26).  DEBUG-only: the release ROM carries
 * no hook. */
extern volatile uint8_t g_debug_hide_count;
#endif
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
/* Active tileset palette table, nibble-packed (two 0-7 indices per byte):
 * 40 WRAM bytes for the 80 world-block slots, not 80 (stack-gap budget,
 * AGENTS.md §52.24 -- a full byte per slot was part of the +16 B WRAM
 * growth that zeroed the battle stack margin).  Written by the bank-5
 * tileset loader, read by ui_cell_palette below.  Grew 32 -> 40 for the
 * forest row-4 extension (hut/mailbox, slots 64..68); the sentinels +
 * full harness pin the margin. */
extern uint8_t g_active_tile_palette[40];

/* Toggle LCDC bit 7 directly (harness-safe: no GBDK display_off VBlank
 * wait).  Full-screen redraws span several display sweeps and cannot fit
 * in one VBlank, so every full redraw runs with the LCD off and all VRAM
 * writes land deterministically. */
void ui_lcd_off(void);
void ui_lcd_on(void);

extern int8_t g_loaded_tileset;
void ui_load_tileset(uint8_t tileset);
void ui_invalidate_tileset(void);
void ui_draw_world_map(const World *world);
/* Full world redraw for a screen that may have clobbered VRAM block 1
 * (battle/menu -> dialogue backdrop): same body as ui_draw_world_map, so
 * it is an alias rather than a second fixed-bank function (the debug
 * fixed bank has no headroom to spare).  The map rect overwrites every
 * ring cell and the tail resets the semantic NULs, so no ui_clear_screen
 * is needed. */
#define ui_draw_world_full(world) ui_draw_world_map(world)
void ui_draw_world_transition(const World *world);
void ui_restore_dialogue_rows(const World *world);
void ui_draw_actors_sprites(const World *world);
void ui_draw_text_line(uint8_t x, uint8_t y, const char *text, uint8_t max_chars);
void ui_draw_hline(uint8_t y, char ch);

/* Set SCX/SCY from the overworld camera pixel position.  Called every
 * overworld frame so the background glides smoothly. */
void ui_update_camera(const World *world);

/* Bank for ui_dialogue_box_banked (src/ui/ui_dialogue_box_banked.c):
 * per-build split (debug 5 / release 4) so release bank 3 keeps room for
 * battle HUD work.  The fixed-bank wrapper stages through this macro, so
 * pragma and dispatch can never disagree. */
#ifdef DEBUG_BUILD
#define UI_DIALOGUE_BOX_BANK 5
#else
#define UI_DIALOGUE_BOX_BANK 4
#endif

/* Banked no-arg body dispatched by ui_draw_dialogue() (src/ui/ui_dialogue_box_banked.c):
 * paints the 4 box rows FULL-WIDTH with the dialogue paper palette and
 * re-syncs SCX/SCY to the world camera (scroll_y in g_bk_byte_b selects the
 * base rows for vertically scrolled maps; camera pixels in g_bk_byte_c =
 * SCX and g_bk_byte_a = SCY -- the camera glides in pixels, so SCY must be
 * the staged value, not scroll_y << 3).  Full-width is exact
 * where it matters: the viewport always shows a 20-col slice of those rows,
 * and the rows ARE the box (text rows 14-17 + scroll_y).  Painting hidden
 * cols too is safe: ui_lcd_off() wipes the whole attribute map on every
 * transition and the next full redraw repaints world attrs, so stale paper
 * can never scroll into view.  The SCX/SCY sync fixes the post-battle
 * victory dialogue, which arrives with SCX = SCY = 0 (screen_change zeroes
 * them for battle) while its text is drawn at scrolled tilemap cells --
 * without this the box shifts right/clips, or on a tall map lands off the
 * bottom of the screen. */
void ui_dialogue_box_banked(void);

void ui_draw_dialogue(const DialogueState *dialogue, const World *world);
/* LCD-safe page redraw (text rows + continue chevron, no paper paint):
 * used by the dialogue screen's line-change path while the LCD is on.
 * The full box (top bar + paper, no side or bottom borders) is drawn
 * once by ui_draw_dialogue while the LCD is off. */
void ui_draw_dialogue_page(const DialogueState *dialogue,
                           uint8_t scroll_x, uint8_t scroll_y);
void ui_draw_dialogue_line(uint8_t x, uint8_t y, const char *text,
                           uint8_t max_chars, uint8_t ox, uint8_t oy);
void ui_draw_battle_full(const Battle *battle);
void ui_update_battle(const Battle *battle);
void ui_update_battle_banked(void);
void ui_draw_battle_timer(const Battle *battle);
void ui_draw_battle_timer_banked(void);
uint8_t ui_calc_timer_bar(uint16_t t);

/* Bank-5 no-arg body behind ui_init's battle-UI tile load (card frames +
 * bar segments + HUD icons from card_frame_tiles.h, living in
 * ui_card_tiles_banked.c next to this loader); dispatched via
 * banked_call_run() with the LCD off. */
void ui_card_tiles_load_banked(void);

void ui_draw_font_test(void);

#endif /* UI_H */
