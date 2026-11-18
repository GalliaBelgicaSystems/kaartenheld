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
 * icon block (104-116) and the world / battle-art blocks (128+).  The
 * EMPTY segment's art is blank on purpose: a drained bar leaves nothing
 * behind (no hollow pip grid), just the screen background. */
#define UI_TILE_TIMER_FILLED     117u
#define UI_TILE_TIMER_EMPTY      127u
/* Up-arrow select icons (combat tileset "arrow pointing up light/dark",
 * card_frames sheet tiles 14/15): the light arrow is the cursor on a plain
 * card (and the enemy-target caret); the dark arrow stays on a card that is
 * already in the combo (no auto-advance after select).  Both replace the
 * '^' font caret on the battle marker / target rows.  BG tile ids 96-103
 * were the free block-1 slots (font 0-95, atlas 104-116); slot 112 (the
 * former menu blank, never stamped) now carries the 6-arrows floor glyph. */
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
 * whenever another dialogue page follows; hidden on the last page.  Slot 109 was the last free id in 96-127
 * (96 light arrow, 97-103 bow counters, 104-108 weapons, 110/111 dark
 * arrow + AP dot, 112 six-arrows counter, 113/114/116 HUD, 115 coin, 117/127
 * bar, 118-126 frames): its same-numbered OAM id lives in the 0x8000 sprite
 * block,
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

/* Menu AP pip BG tiles (assets/ap_pips_bg.png, cost order 1-5; NOT
 * boot-loaded -- those ids sit in the high block-1 window).  The bank-8
 * menu body streams them on demand to the window below dice (236-240),
 * clear of the 128+ enemy-art growth (max ~36 tiles) and of the world
 * tileset (128-255, reloaded on overworld entry which wipes them -- no
 * restore needed).  Tile id = UI_TILE_AP_PIP_BASE + (cost - 1). */
#define UI_TILE_AP_PIP_BASE      236u
#define UI_TILE_AP_PIP_COUNT     5u

/* Card-menu element rider icons as BG tiles (assets/menu_riders.png, cells
 * 0-3: fire, ice, poison, health).  Streamed on demand by the bank-2
 * menu bodies into the time-shared high block-1 window below the AP pips
 * (overworld reloads its own tileset on exit, and menus/battle never render
 * world tiles), and stamped over the card's element cell -- the menu used to
 * recycle enemy-sprite OAM art for these, whose 8x8 crop was off-center.
 * The cell keeps its element palette slot (fire/ice/poison, health ->
 * UI_COLOR_HEALTH), so one encoding per icon shows the right color.
 * The finger cursor (assets/menu_cursor.png, cells 0/1: right / up) is a
 * separate sheet: the boot card-tile loader pastes it over the font's
 * '>' and '^' glyph bitmaps (see ui_card_tiles_banked.c). */
#define UI_TILE_MENU_RIDER_BASE  228u
#define UI_TILE_MENU_RIDER_COUNT 4u
#define UI_TILE_MENU_RIDER_FIRE  228u
#define UI_TILE_MENU_RIDER_ICE   229u
#define UI_TILE_MENU_RIDER_POISON 230u
#define UI_TILE_MENU_RIDER_HEALTH 231u

/* Card-menu rider icon streamer (src/screens/menu_riders_banked.c, bank 2
 * with the menu render bodies that call it).  One-shot per menu stay; the
 * flag is re-armed on every BATTLE/ITEM/SHOP entry by the bank-11 time-share
 * invalidate body (same contract as the AP pip stream). */
extern uint8_t g_menu_riders_loaded;
void ic_menu_riders_load(void);


/* Menu DECK-row chrome (all boot-loaded by ui_card_tiles_load_banked,
 * same loader as the weapon icons, so no streaming needed): the AP unit
 * icon (combat_ap_icon, HUD gold) stamped after the cost pip, and the
 * deck icon (combat_deck_icon, HUD iron) replacing the DECK text.  Spacer
 * cells are plain font spaces.  The text buffer keeps the DECK letters,
 * so harness screen_row asserts are unchanged.  (Former UI_TILE_MENU_BLANK
 * 112 now carries the 6-arrows floor glyph -- do not stamp it as blank.) */
#define UI_TILE_AP_ICON          114u
#define UI_TILE_DECK_ICON        116u

/* Menu chrome (assets/menus-tile.png): 12 single-cell UI icons (row 0,
 * cols 0-11) plus the 4x4 hero-status portrait (cols 12-15, rows 0-3,
 * "hero status RC" sheet order, blank transparent corners included).
 * Singles stream to 208-219 in scan order; the 16 hero cells land on
 * the orphaned box slots (220-227, 232-235, 242-244) plus dice slot
 * 246 (the gamble entry restreams all dice, and every ITEM entry
 * restreams hero, so the time-share is safe).  Streamed eagerly on
 * every BATTLE/ITEM/SHOP entry by the bank-11 invalidate body (entry
 * is the once-per-stay re-arm, no WRAM flag); the slot table lives
 * with that streamer.
 *
 * Single-cell order is sheet scan order (row 0, cols 0-11 = CSV order:
 * 0 bottom box border, 1 inventory separator, 2 inventory type arrow,
 * 3 cursor category, 4 category border, 5 category corner, 6 category
 * separator, ...).  The slice script's TILE_MENUS_* editor names follow
 * the CSV grid directly; the defines below follow the same order. */
#define UI_TILE_MENUS_BASE       208u
#define UI_TILE_MENUS_COUNT      12u
#define UI_TILE_MENUS_HERO_COUNT 16u
/* Sheet (0,0) horizontal segment: the row-15 bottom rule.  Named for
 * what it is, not for the slice script's CSV[1] label. */
#define UI_TILE_MENUS_BOTTOM_BORDER (UI_TILE_MENUS_BASE + 0u)
#define UI_TILE_MENUS_INV_SEPARATOR (UI_TILE_MENUS_BASE + 1u)
#define UI_TILE_MENUS_INV_ARROW     (UI_TILE_MENUS_BASE + 2u)
#define UI_TILE_MENUS_CURSOR_CAT    (UI_TILE_MENUS_BASE + 3u)
#define UI_TILE_MENUS_CAT_BORDER    (UI_TILE_MENUS_BASE + 4u)
#define UI_TILE_MENUS_CAT_CORNER    (UI_TILE_MENUS_BASE + 5u)
#define UI_TILE_MENUS_CAT_SEPARATOR (UI_TILE_MENUS_BASE + 6u)
/* Sheet (0,7) list scroll arrow: all tabs' scroll indicators (rows
 * 2/4 up, rows 14/15 down).  The up caret uses the 180-degree rotated
 * copy in slot 245 (the sheet art points down); the semantic buffer
 * keeps '^'/'v'.  Sheet (0,8) quests separator: the rule between
 * quest/gem blocks.  All stream with the chrome sheet on every
 * BATTLE/ITEM/SHOP entry, so no per-tab streaming is needed. */
#define UI_TILE_MENUS_LIST_SCROLL_ARROW (UI_TILE_MENUS_BASE + 7u)
#define UI_TILE_MENUS_LIST_SCROLL_UP 245u
#define UI_TILE_MENUS_QUESTS_SEPARATOR (UI_TILE_MENUS_BASE + 8u)
/* Sheet (0,9) heart / (0,10) star: the STATUS-tab HP/AP icons.
 * Streamed with the chrome sheet, so no per-tab streaming is needed. */
#define UI_TILE_MENUS_HP_ICON (UI_TILE_MENUS_BASE + 9u)
#define UI_TILE_MENUS_AP_ICON (UI_TILE_MENUS_BASE + 10u)

/* Overworld status-bar icons (menus-tile.png row 1, cols 0-3: full
 * heart, half heart, AP, arrows; header indices 28-31, appended after
 * the chrome singles + hero portrait so those indices never move).
 * Streamed to VRAM 208-211 by the bank-11 status body on every
 * overworld full redraw (ui_world_tail): the slots time-share with
 * the menu chrome singles (same ids) -- each entry restores its own,
 * same pattern as the OAM time-shares (AP pips vs hero art).  Painted
 * with UI_COLOR_PAPER attrs (slot 4 = fight3 ramp, programmed by the
 * same body): opaque white bar on every tileset.  DMG-safe (BGP 0xE4:
 * white bg, dark ink). */
#define UI_TILE_STATUS_HEART_FULL 208u
#define UI_TILE_STATUS_HEART_HALF 209u
#define UI_TILE_STATUS_AP         210u
#define UI_TILE_STATUS_ARROWS      211u

/* Menu AP pip tiles (gfx/ap_pip_tiles.h) stream inside the bank-2 item
 * render body on DECK-tab full redraws (same-bank data, LCD-off window).
 * No standalone loader: the fixed bank is full (AGENTS.md 52.18), so no
 * extra trampoline dispatch lives in item_screen.c. */

/* Overworld actor OAM sprite tile bases (VRAM Block 0, 0x8000).  Collision
 * rules: must not overlap the font-duplicate block 0..95 used for ASCII
 * actor glyphs, PLAYER (102) or HERO (98). */
#define HERO_DESOLATE_SPRITE_TILE_ID 98u
#define PLAYER_SPRITE_TILE_ID    HERO_DESOLATE_SPRITE_TILE_ID
/* Standing-hero idle frames (hero_idle_1/2, streamed at boot beside the
 * walk frames): OAM 96-97, time-shared with battle AP pips 1-2 (boot
 * and overworld show idle; battle entry streams pips over them,
 * overworld entry restores idle -- see ui_draw_actors_sprites).
 * Walk frames at 98-99 time-share the same way with AP pips 3-4.
 * Battle never needs hero art (hero hidden), overworld never needs
 * pips. */
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

/* Save-spin indicator: OAM entry 39 (actor slots use 0-37, markers
 * 30-37, so 39 is always free) with tile 241.  Tile 241 is free in
 * both fetch blocks (BG ids 241-244 sit between the AP-pip stream
 * 236-240 and the dice stream 246-255; OBJ 241 aliases the same bytes
 * no BG writer touches).  The per-phase glyph is re-streamed from the
 * font each phase change, so no boot-time load and no clobber state.
 * CGB uses prop 5 (OBJ palette slot 5, sprites7: white box at color 1,
 * dark brown ink at color 3); DMG uses prop 0x10 (OBP1 = 0xE0: shade 0
 * white box at color 1, shade 3 black ink at color 3). */
#define SPIN_OAM_ENTRY           39u
#define SPIN_TILE                241u
#define SPIN_TTL                 120u
#define SPIN_PX                  152u
#define SPIN_PY                  136u

/* Max shadow-OAM entries the actor pipeline can touch: player (entry 0),
 * hostile actors up to MAX_WORLD_ACTORS each a w*h sprite (max 2x2 = 4),
 * then static actors.  Used by the transition-hide sweep. */
#define OAM_MAX_ACTOR_ENTRIES     (1u + (MAX_WORLD_ACTORS * 4u) + MAX_STATIC_ACTORS)

/* Bank-4 no-arg body behind ui_draw_actors_sprites(): writes each active
 * non-boss actor's shadow-OAM entry (position/tile/prop from SPRITE_KIND_*)
 * and the castle boss 2x2 background block.  Lives in bank 4 to keep the
 * fixed bank under 0x8000. */
void ui_actors_sprites_banked(void);

/* OAM tile-resolve cache validity (WRAM, defined in
 * ui_world_sprite_banked.c).  Cleared by actor spawn/mutation sites in
 * other banks (scene loader, trio morph); the next OAM pass recomputes. */
extern uint8_t g_oam_tilecache_valid;

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
 * (emerald), 5 = wood (brown), 6 = gold (mythril), 7 = dim (ice grey-out
 * and desolate wasteland ground). */
#define UI_COLOR_NONE   0
#define UI_COLOR_FIRE   1
#define UI_COLOR_IRON   2
#define UI_COLOR_ICE    2
#define UI_COLOR_FIELD  3
#define UI_COLOR_POISON 4
/* Paper: CRAM slot 4 re-programmed to a white/black document ramp.
 * Owned persistently by the overworld status-bar HUD (opaque white
 * bottom row; the bank-11 status stream programs it in ui_world_tail
 * on every full redraw) and transiently by dialogue boxes (no world
 * tileset assigns slot 4 to any tile, and the quick screen -- its only
 * other consumer -- cannot be open during a dialogue or the HUD).
 * Every screen transition re-programs CRAM (tileset load, then the
 * HUD stream), so stale paper can never scroll into view. */
#define UI_COLOR_PAPER  4
#define UI_COLOR_WOOD   5
#define UI_COLOR_GOLD   6
#define UI_COLOR_DIM    7
/* Health-rider green: BG slot 7 (DIM) carries it while the card menu is
 * up.  No menu screen paints DIM (the battle grey-out is its only user),
 * and the bank-2 menu bodies program slot 7 with the fighthealth ramp
 * (white paper + COMBAT/slime_light, generated menu_palette_generated.h)
 * right after the UI CRAM load.  Battle and overworld full redraws
 * program their own set again, so the grey-out is untouched. */
#define UI_COLOR_HEALTH 7

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

/* Overworld camera: ui_update_camera updates semantic view state (debug
 * harness only; no-op macro in release), while ui_commit_camera writes
 * SCX/SCY hardware registers during VBlank. */
#ifdef DEBUG_BUILD
void ui_update_camera(const World *world);
#else
#define ui_update_camera(world) ((void)0)
#endif
void ui_commit_camera(const World *world);

/* Bank for ui_dialogue_box_banked (src/ui/ui_dialogue_box_banked.c):
 * ROM bank 10 in both builds (~2 KB free in release), alongside the
 * despair fades.  The fixed-bank wrapper stages through this macro,
 * so pragma and dispatch can never disagree. */
#define UI_DIALOGUE_BOX_BANK 10

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
/* Bank-8 no-arg body behind ui_draw_ow_hud(): paints the overworld
 * status bar onto Window tilemap row 0 (0x9C00, WX=7, WY=136, cols
 * 0-19; cols 18-19 are blank white under the spinner OAM) from live
 * party/AP/arrow/gold state on overworld transitions. */
void ui_ow_hud_banked(void);
void ui_draw_ow_hud(void);

/* Bank-11 no-arg body behind the overworld status-bar icon stream:
 * restreams the 4 status icons to VRAM 208-211 and programs the
 * paper-white CGB ramp into slot 4.  Dispatched from ui_world_tail()
 * on every overworld full redraw (gate crossings run LCD-on). */
void menus_status_stream_banked(void);

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
