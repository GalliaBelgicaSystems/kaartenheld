#ifndef GAME_H
#define GAME_H

#include "screen.h"
#include "world.h"
#include "battle.h"
#include "audio.h"
#include "input.h"
#include "ui.h"

#include "dialogue.h"
#include "rpg/state.h"
#include "rpg/progression.h"

/* Targeted menu updates (AGENTS.md 36): a cursor-only UP/DOWN in the ITEM
 * and SHOP screens repaints just the `>` glyph via the bank-2 cursor body
 * (screen_content.c, mirrored into g_ui_screen_buf) instead of invalidating
 * the render cache (which forces a whole-screen LCD-off redraw + banked
 * calls that mask the 256 Hz music ISR).  Window-scroll moves (which also
 * shift the card list) still take the rare full-redraw path. */

typedef struct {
    bool valid;
    ScreenId prev_screen;
    MapId prev_map_id;
    uint8_t prev_player_x;
    uint8_t prev_player_y;
    bool prev_dialogue_active;
    uint8_t prev_dialogue_line;
    DialogueId prev_dialogue_id;
    uint8_t prev_battle_timer_bar;
    uint8_t prev_game_over_choice;
} RenderCache;

typedef struct Game {
    ScreenId screen;      /* currently active screen */
    ScreenId prev_screen; /* previous screen (for transitions) */
    GameState state;      /* canonical persistent RPG state */
    World world;
    Battle battle;
    DialogueState dialogue;
    uint32_t frame;
    uint8_t game_over_choice;  /* 0 = YES, 1 = NO on the continue prompt */
    uint8_t item_menu_index;   /* cursor into the active tab's list (0 =
                                   FILTER/SORT row on the DECK tab) */
    uint8_t item_menu_tab;     /* 0 = DECK, 1 = QUEST, 2 = HERO, 3 = GEM */
    uint8_t item_menu_scroll;  /* first visible card position of the list */
    uint8_t item_menu_filter;  /* 0xFF = ALL, else a CardType value */
    uint8_t item_menu_sort;    /* 0 = none, 1 = type, 2 = power, 3 = cost,
                                  4 = power desc, 5 = cost desc */
    uint8_t item_menu_mode;      /* 0 = list, 1 = card detail, 2 = quest
                                    detail, 3 = filter/sort picker */
    uint8_t item_menu_message;   /* 0 = none, 1 = deck full, 2 = max copies */
    uint8_t item_menu_msg_ttl;   /* frames until the message clears */
    uint8_t item_menu_pick_row;  /* picker row: 0 = filter, 1 = sort */
    uint8_t item_menu_prev_filter; /* picker cancel restore */
    uint8_t item_menu_prev_sort;   /* picker cancel restore */
    uint8_t shop_message;      /* 0 = none, 1 = bought, 2 = not enough gold */
    uint8_t shop_id;           /* active shop (set when a shop actor is engaged) */
    uint8_t save_slot_index;   /* 0 = Slot 1, 1 = Slot 2, 2 = Slot 3 */
    uint8_t save_slot_mode;    /* 0 = LOAD, 1 = SAVE */
    uint8_t save_slot_message; /* 0 = none, 1 = saved, 2 = empty */
    uint8_t title_menu_showing; /* 0 = PRESS START state, 1 = main menu */
    uint8_t title_menu_index;  /* 0 = NEW GAME, 1 = CONTINUE, 2 = SOUND, 3 = TUTORIAL */
    uint8_t intro_slide;       /* current ASCII intro slide (0-2) */
    uint8_t tutorial_slide;    /* current tutorial slide (0-5) */
    RenderCache render_cache;
} Game;

extern Game g_game;

void game_init(Game *g);
void game_restart(Game *g);
void game_update(Game *g);
void game_render(Game *g);
void game_render_reset(Game *g);

#endif /* GAME_H */
