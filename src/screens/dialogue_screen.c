#include "game.h"
#include "screen.h"
#include "dialogue.h"
#include "interaction.h"
#include "telemetry.h"
#include "ui.h"
#include "banked.h"

void dialogue_screen_update(Game *g)
{
    if (!g || !g->dialogue.active) return;

    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
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
    } else {
        ui_draw_dialogue_page(d, w->scroll_x, w->scroll_y);
    }

    telemetry_emit(EVENT_RENDER_DIALOGUE, (uint8_t)d->id,
                   d->current_line, 0, 0);
    rc->prev_dialogue_line = d->current_line;
    rc->prev_dialogue_id = d->id;
}
