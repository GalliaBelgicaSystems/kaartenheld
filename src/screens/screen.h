#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declaration; the full type is defined in game.h. */
typedef struct Game Game;

/*
 * ScreenId identifies WHAT KIND of screen is currently active.
 * It is distinct from SceneId (which specific location is loaded).
 */
typedef enum {
    SCREEN_OVERWORLD = 0,
    SCREEN_DIALOGUE  = 1,
    SCREEN_BATTLE    = 2,
    SCREEN_GAME_OVER = 3,
    SCREEN_THANKS    = 4,
    SCREEN_SHOP      = 5,
    SCREEN_ITEM      = 6,
    SCREEN_ENDING    = 7,
    SCREEN_SAVE_LOAD = 8,
    SCREEN_TITLE     = 9,
    SCREEN_INTRO     = 10,
    SCREEN_TUTORIAL  = 11,
    SCREEN_SPLASH    = 12
} ScreenId;

/*
 * SceneId identifies a specific overworld location.  Many scenes share the
 * same SCREEN_OVERWORLD screen implementation.  Like MapId, values come
 * from the generated src/world/scene_ids_generated.h (see world.h).
 */
typedef uint8_t SceneId;
#include "scene_ids_generated.h"

/* ── Screen manager ─────────────────────────────────────────────── */

/* Centralised screen transition: exits the old screen, sets the new screen,
 * and emits SCREEN_CHANGED telemetry. */
void screen_change(Game *g, ScreenId screen);
void screen_update(Game *g);
void screen_render(Game *g);
void scene_load(Game *g, SceneId scene, uint8_t spawn_x, uint8_t spawn_y);
void scene_update_from_map(Game *g);
void scene_sync_from_world(Game *g);

/* ── Screen module lifecycle entry points ────────────────────────── */

void overworld_screen_update(Game *g);
void overworld_screen_render(Game *g);
void start_battle_from_world(Game *g);
void dialogue_screen_update(Game *g);
void dialogue_screen_render(Game *g);
void battle_screen_update(Game *g);
void battle_screen_render(Game *g);
void game_over_screen_update(Game *g);
void game_over_screen_render(Game *g);
void thanks_screen_update(Game *g);
void thanks_screen_render(Game *g);
void shop_screen_update(Game *g);
void shop_screen_render(Game *g);
void item_screen_update(Game *g);
void item_screen_render(Game *g);
void item_screen_reset(Game *g);
void ending_screen_update(Game *g);
void ending_screen_render(Game *g);
void save_load_screen_update(Game *g);
void save_load_screen_render(Game *g);
void title_screen_update(Game *g);
void title_screen_render(Game *g);
void splash_screen_update(Game *g);
void splash_screen_render(Game *g);
void intro_screen_update(Game *g);
void intro_screen_render(Game *g);
void tutorial_screen_update(Game *g);
void tutorial_screen_render(Game *g);

/* Shared by tutorial_screen.c (RIGHT/LEFT wrap) and tutorial_content.c
 * (slide table end).  Generated from screens/tutorial.json — single
 * source, the two cannot drift.  Count-only header: the slide arrays
 * live in tutorial_text_generated.h, included once by
 * tutorial_content.c (static data must not duplicate per TU). */
#include "tutorial_count_generated.h"

/* Shared slide-show renderer (intro + tutorial + splash screens).
 * `bank` is the content body's ROM bank (4 = ASCII slides, 7 = splash). */
void slide_screen_render(Game *g, ScreenId scr, uint16_t content_target, uint8_t slide, uint8_t bank);

/* Bank-2 no-arg bodies dispatched by title_screen_render() (banked layout
 * constraint, AGENTS.md 52.11.1).  title_content_render() reads
 * g_bk_byte_a/b (menu showing + index); intro_content_render() reads
 * g_bk_byte_a (slide).  tutorial_content_render() reads g_bk_byte_a (slide). */
void title_content_render(void);
void intro_content_render(void);
void tutorial_content_render(void);

/* Bank-2 no-arg bodies dispatched by the simple_screens render wrappers
 * (thanks/ending/game-over).  game_over_content_render() reads
 * g_bk_byte_a (choice). */
void thanks_content_render(void);
void ending_content_render(void);
void game_over_content_render(void);

#endif /* SCREEN_H */
