#include "game.h"
#include "screen.h"
#include "dialogue.h"
#include "interaction.h"
#include "telemetry.h"
#include "ui.h"
#include "banked.h"
#include "content.h"
#include "rpg/deck.h"

/* Bank-10 choice nav body (src/game/choice_nav_banked.c): evaluates pad
 * edges, toggles/confirms, and repaints the cursor cells inline, so the
 * fixed bank pays just the dispatch (AGENTS.md 52.11.1/52.18).  One body
 * arbitrates every YES/NO dialogue (Heal Mage, kobold table invite,
 * Carl's spar offer); what an answer means is game-owned data below. */
void dialogue_choice_nav_banked(void);

void dialogue_screen_update(Game *g)
{
    if (!g || !g->dialogue.active) return;

    /* YES/NO choice arbitration lives in the game-owned bank-10 choice
     * body (fixed-bank cost: staging + call -- the fixed bank sits hard
     * against 0x8000, AGENTS.md §52.18).  It reports 3 to open the
     * staged follow-up (stay), 2 to confirm (advance), 6 for a confirmed
     * heal (jingle plus the staged HEALED / STORY_FLAG_SET telemetry are
     * still owed here -- banked code cannot reach the fixed-bank
     * audio/telemetry -- then the staged thanks opens instead of
     * advancing), 7 for a confirmed table invite (close through the real
     * API so DIALOGUE_ENDED stays paired, then the bank-8 tick opens the
     * table), 8 for a confirmed staged grant (Carl's skip-the-tutorial
     * offer: variable add + one-shot flag through the real APIs, which
     * own the VARIABLE_SET / STORY_FLAG_SET telemetry, then the staged
     * follow-up line), 9 for a confirmed item handover (skeleton's
     * bone: staged item add + one-shot flag + follow-up; a failed add
     * falls through to the plain advance so the offer can be retried),
     * 10 for a confirmed item swap (dog's key trade: staged add, then
     * staged remove, then the staged thanks whose completion_flag owns
     * the one-shot flag), and 0/1/5 to stay/advance plainly.  The staged
     * ids are opaque data to the engine (never game branches). */
    g_bk_call_bank = 10;
    g_bk_call_target = (uint16_t)&dialogue_choice_nav_banked;
    banked_call_run();
    if (g_bk_byte_a == 5) {
        /* Plain advance outside any live choice. */
    } else {
        if (g_bk_byte_a < 2) return;
        telemetry_emit(EVENT_DIALOGUE_CHOICE, (uint8_t)g->dialogue.id,
                       g->dialogue.choice_index, 0, 0);
        if (g_bk_byte_a == 7) {
            /* Table invitation accepted: close the dialogue through the
             * real API (emits DIALOGUE_ENDED, runs end-of-dialogue hooks)
             * and return.  The bank-8 per-frame tick opens SCREEN_GAMBLE
             * from the staged enter request, so no screen call here. */
            dialogue_end(&g->dialogue);
            return;
        }
        if (g_bk_byte_a == 3) {
            dialogue_start_def(&g->dialogue, (DialogueId)g_bk_byte_c);
            return;
        }
        if (g_bk_byte_a == 8) {
            /* Staged grant resolved by a choice dialogue (Carl's
             * skip-the-tutorial offer): apply the variable add and the
             * one-shot flag through the real APIs -- they own the
             * VARIABLE_SET / STORY_FLAG_SET telemetry -- then open the
             * staged follow-up line.  Ids are opaque data, never engine
             * game branches (AGENTS.md 55.1). */
            game_variable_add(&g->state, (VariableId)g_bk_byte_b, 1);
            game_flag_set(&g->state, (FlagId)g_bk_byte_c);
            dialogue_start_def(&g->dialogue, (DialogueId)g_bk_byte_d);
            return;
        }
        if (g_bk_byte_a == 9) {
            /* Item handover resolved by a choice dialogue (skeleton's
             * bone): grant through the real collection API -- it owns
             * caps and the add result -- then the one-shot flag and the
             * staged follow-up, like code 8's variable grant.  A failed
             * add (full shelf) falls through to the plain advance below:
             * the ask just closes with nothing set, so it can be
             * retried. */
            /* No per-grant telemetry: DIALOGUE_CHOICE (emitted above)
             * plus the inventory snapshot cover the handover -- the
             * fixed bank has no room for three more emit sites (the
             * debug _HOME sits 27 B past 0x8000 with them). */
            if (deck_collection_add(&g->state.cards, (CardId)g_bk_byte_b, 1)) {
                game_flag_set(&g->state, (FlagId)g_bk_byte_c);
                dialogue_start_def(&g->dialogue, (DialogueId)g_bk_byte_d);
                return;
            }
        } else if (g_bk_byte_a == 10) {
            /* Item swap resolved by a choice dialogue (dog's key trade):
             * add the key first so a full shelf keeps the bone (the
             * trade can be retried), then remove it and open the staged
             * thanks.  The thanks dialogue's completion_flag owns the
             * one-shot flag, so no fourth staging byte is needed. */
            if (deck_collection_add(&g->state.cards, (CardId)g_bk_byte_c, 1)) {
                deck_collection_remove(&g->state.cards, (CardId)g_bk_byte_b, 1);
                dialogue_start_def(&g->dialogue, (DialogueId)g_bk_byte_d);
                return;
            }
        } else if (g_bk_byte_a == 6) {
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
        /* Mid-battle Carl box (msg 9 parked by the banked tick when it
         * staged the box, and by nothing else): return to the fight --
         * never restart it through the stashed-slot path below.  This
         * also covers the fled-lesson farewell (battle already over):
         * it lands back on the FLED! result screen, whose exit press
         * then runs the normal fled exit.  Clearing consumes the
         * one-shot. */
        if (g->battle.msg_id == 9) {
            g->battle.msg_id = 0;
            screen_change(g, SCREEN_BATTLE);
            return;
        }
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
        /* Demo outro: the slime-lord wake dialogue ("Where am I?")
         * routes to the ENDING screen (white document page) -- the run is
         * over, win or lose; the wake area's track keeps playing
         * underneath.  Every other dialogue returns to the overworld
         * (game-over quits show the try-again text).  The despair flag is
         * cleared here; battle_start clears it for the next fight.
         * Single screen_change call (fixed-bank budget, AGENTS.md 52.18). */
        screen_change(g, g_slime_despair ? (g_slime_despair = 0, SCREEN_ENDING) : SCREEN_OVERWORLD);
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
            /* Battle OAM time-shares tile ids with overworld art (AP pips
             * 1-4 over the hero idle/walk frames at 96-99, pip 5 plus
             * rider icons over the blob tail at 122-125): the ids are
             * right but VRAM still holds battle pixels, so the hero
             * would render as pips behind the box.  Restores the
             * overworld data (LCD-safe); the return trip to battle
             * restreams via the cleared s_battle_tiles_loaded flag on
             * the next battle tick. */
            ui_restore_ow_oam();
            ui_draw_dialogue(d, w);
            if (g_slime_despair) {
                /* Wake-entry fade: the base paint above snapshotted the
                 * target and whitened CRAM (LCD off).  Position sprites
                 * and commit OAM before turning the LCD on, so the hero
                 * is visible from the first faded frame instead of
                 * popping in after the 24-frame block below. */
                ui_sprite_move((uint8_t)(world_player_px(w) - w->camera_px_x),
                               (uint8_t)(world_player_py(w) - w->camera_px_y));
                ui_draw_actors_sprites(w);
                ui_sprite_hide_actors_below((uint8_t)(14 * 8));
                ui_sprite_commit();
            }
            ui_lcd_on();
            /* Despair outro (demo finale): the defeat faded out to white
             * on the result screen -- fade the wake area back in over the
             * just-redrawn world.  The box body carries the fade (same
             * bank, no new dispatch): with the flag set this repaint runs
             * only the fade steps (no repaint -- the base above already
             * painted and whitened CRAM), so normal battle->dialogue
             * entries pay only one redundant box paint. */
            ui_draw_dialogue(d, w);
        } else {
            ui_draw_dialogue(d, w);
        }

        if (!g_slime_despair) {
        ui_sprite_move((uint8_t)(world_player_px(w) - w->camera_px_x),
                       (uint8_t)(world_player_py(w) - w->camera_px_y));
        ui_draw_actors_sprites(w);
        /* The box is background tiles at rows 14-17; hide any actor sprite
         * (e.g. the town fire/dog) that would draw over the text. */
        ui_sprite_hide_actors_below((uint8_t)(14 * 8));
        }
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
