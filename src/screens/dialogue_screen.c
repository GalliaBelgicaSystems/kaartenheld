#include "game.h"
#include "screen.h"
#include "dialogue.h"
#include "interaction.h"
#include "telemetry.h"
#include "ui.h"
#include "banked.h"

/* Bank-8 choice nav body (src/game/heal_mage_banked.c): evaluates pad
 * edges, toggles/confirms, and repaints the cursor cells inline, so the
 * fixed bank pays just the dispatch (AGENTS.md 52.11.1/52.18). */
void dialogue_choice_nav_banked(void);

void dialogue_screen_update(Game *g)
{
    if (!g || !g->dialogue.active) return;

    /* YES/NO choice arbitration lives in the game-owned banked body
     * (fixed-bank cost: staging + call -- the fixed bank sits hard
     * against 0x8000, AGENTS.md §52.18).  It reports 3 to open the
     * staged follow-up (stay), 2 to confirm (advance), 6 for a
     * confirmed heal (jingle plus the staged HEALED / STORY_FLAG_SET
     * telemetry are still owed here -- banked code cannot reach the
     * fixed-bank audio/telemetry -- then the staged thanks opens
     * instead of advancing), and 0/1/5 to stay/advance plainly.  The
     * staged ids are opaque data to the engine (never game branches). */
    g_bk_call_bank = 8;
    g_bk_call_target = (uint16_t)&dialogue_choice_nav_banked;
    banked_call_run();
    if (g_bk_byte_a == 5) {
        /* Plain advance outside any live choice. */
    } else {
        if (g_bk_byte_a < 2) return;
        telemetry_emit(EVENT_DIALOGUE_CHOICE, (uint8_t)g->dialogue.id,
                       g->dialogue.choice_index, 0, 0);
        if (g_bk_byte_a == 7) {
            /* Kobold's table invitation accepted (the banked choice
             * body closed the dialogue and staged this code): open the
             * chuck-a-luck table.  The code is staged data from the
             * game-owned body, never a fixed-bank game branch (55.1). */
            screen_change(g, SCREEN_GAMBLE);
            return;
        }
        if (g_bk_byte_a == 3) {
            dialogue_start_def(&g->dialogue, (DialogueId)g_bk_byte_c);
            return;
        }
        if (g_bk_byte_a == 6) {
#ifdef DEBUG_BUILD
            if (g_bk_byte_b) telemetry_emit(EVENT_HEALED, g_bk_byte_b, 3, 0, 0);
#endif
            /* Mark the one-shot used through the real flag API (which
             * owns the STORY_FLAG_SET telemetry); the id arrives staged
             * like the event engine's action args, never a game branch. */
            game_flag_set(&g->state, (FlagId)g_bk_byte_c);
            audio_play_music(MUSIC_CHEST);
            /* Healed: open the ring-tip thanks instead of advancing. */
            dialogue_start_def(&g->dialogue, (DialogueId)g_bk_byte_d);
            return;
        }
    }

    if (!dialogue_next(&g->dialogue)) {
        interaction_on_dialogue_end(&g->dialogue, &g->state);
        /* Stashed spar: closing a lesson that detoured from an
         * encounter (Carl) starts the battle instead of returning
         * to the overworld.  The slot is set only by the encounter
         * path (movement/patrol bump or hostile interaction) and
         * cleared by every battle exit, so no other dialogue can
         * trip this. */
        if (g->world.encounter_actor_index != NO_ACTOR_INDEX) {
            start_battle_from_world(g);
            return;
        }
        screen_change(g, SCREEN_OVERWORLD);
    }
}

void dialogue_screen_render(Game *g)
{
    RenderCache *rc;
    World *w;
    DialogueState *d;
    uint8_t first_enter;

    if (!g) return;
    rc = &g->render_cache;
    w = &g->world;
    d = &g->dialogue;

    first_enter = (!rc->valid || rc->prev_screen != SCREEN_DIALOGUE);
    if (!first_enter &&
        d->current_line == rc->prev_dialogue_line &&
        d->id == rc->prev_dialogue_id) {
        return;
    }

    if (first_enter) {
        /* From battle (or any non-overworld): full LCD-off redraw to
         * recreate VRAM Block 1 after enemy art.  From overworld: overlay,
         * no LCD-off, paint the box over the live map. */
        if (g->prev_screen != SCREEN_OVERWORLD) {
            ui_lcd_off();
            /* Battle enemy art shares VRAM slots 128+ with world tiles and
             * the loader bypasses ui_load_tileset's cache, so force the
             * reload: without this the world redraw keeps enemy art. */
            ui_invalidate_tileset();
            ui_draw_world_full(w);
            ui_draw_dialogue(d, w);
            ui_lcd_on();
        } else {
            ui_draw_dialogue(d, w);
        }

        ui_sprite_move((uint8_t)(world_player_px(w) - w->camera_px_x),
                       (uint8_t)(world_player_py(w) - w->camera_px_y));
        ui_draw_actors_sprites(w);
        /* The box is background tiles at rows 14-17; hide any actor sprite
         * (e.g. the town fire/dog) that would draw over the text. */
        ui_sprite_hide_actors_below((uint8_t)(14 * 8));
        rc->valid = true;
        rc->prev_screen = SCREEN_DIALOGUE;
        rc->prev_dialogue_active = true;
    } else if (d->id != rc->prev_dialogue_id) {
        /* In-place follow-up dialogue (YES/NO decline path): same screen,
         * new id.  Redraw the box (header + page) over the live world
         * with the LCD on -- never the LCD-off full redraw above, which
         * is only for entering the screen. */
        ui_draw_dialogue(d, w);
    } else {
        ui_draw_dialogue_page(d, w->scroll_x, w->scroll_y);
    }

    telemetry_emit(EVENT_RENDER_DIALOGUE, (uint8_t)d->id,
                   d->current_line, 0, 0);
    rc->prev_dialogue_line = d->current_line;
    rc->prev_dialogue_id = d->id;
}
