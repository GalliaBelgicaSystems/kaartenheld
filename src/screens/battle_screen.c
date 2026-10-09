#include "game.h"
#include "screen.h"
#include "battle.h"
#include "scene.h"
#include "telemetry.h"
#include "audio.h"
#include "content.h"
#include "ui.h"
#include "rpg/cards.h"
#include "rpg/loot.h"

void battle_screen_update(Game *g)
{
    /* SDCC pointer-cache workaround (see battle_update in battle.c): the
     * optimizer reuses branch-local cached &g->field stack slots across
     * joins, producing wild stores/calls when a different path filled the
     * slot.  Every direct field access in this file therefore goes through
     * the volatile vg pointer; plain Battle views for callees are
     * recomputed at each call site via (Battle *)&vg->battle. */
    volatile Game *vg = g;

    if (!g) return;

    if (vg->battle.battle_over) {
        /* Grant combat loot exactly once, when the VICTORY screen first
         * appears (docs/loot.md §34.5) -- the drop was ROLLED at battle
         * start and waits in g_loot_id.  world_on_battle_end no-ops on
         * repeat calls (encounter index cleared); the exit path calls it
         * again harmlessly.  card_get_def re-populates g_card_scratch so
         * the bank-2 HUD can render the identity name.  No drop ->
         * silence. */
        if (vg->battle.result == BATTLE_RESULT_VICTORY &&
            vg->world.encounter_actor_index != NO_ACTOR_INDEX) {
            world_on_battle_end(g, true);
            if (g_loot_id != CARD_NONE) {
                (void)card_get_def(g_loot_id);
                vg->battle.msg_id = 4;      /* never expires during RESULT */
                vg->battle.msg_ttl = 0;
                vg->battle.dirty |= BATTLE_DIRTY_MSG;
            }
        }
        if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
            if (vg->battle.result == BATTLE_RESULT_VICTORY ||
                vg->battle.result == BATTLE_RESULT_FLED) {
                uint8_t exit_hp = vg->battle.player.hp;
                /* Mutual-KO survival: a victory earned with 0 HP (shared
                 * status-tick boundary killed both sides, victory shadows
                 * the death) exits breathing at 1 HP instead of stranding
                 * a corpse in the overworld. */
                if (vg->battle.result == BATTLE_RESULT_VICTORY && exit_hp == 0) {
                    exit_hp = 1;
                }
                vg->world.player.hp = exit_hp;
                vg->state.party.members[0].hp = exit_hp;
                /* Return to the current scene's area track (TOWN/DUNGEON
                 * get their own theme again instead of the field's). */
                {
                    const SceneDefinition *def = scene_definition_for_map(vg->world.map_id);
                    audio_play_music(def ? def->music : MUSIC_OVERWORLD);
                }
                if (vg->battle.result == BATTLE_RESULT_VICTORY) {
                    world_on_battle_end(g, true);
                    screen_change(g, game_screen_after_victory(g));
                } else {
                    world_on_battle_fled(g);
                    screen_change(g, SCREEN_OVERWORLD);
                }
            } else {
                world_on_battle_end(g, false);
                vg->game_over_choice = 0;
                screen_change(g, SCREEN_GAME_OVER);
            }
        }
        return;
    }

    if (vg->battle.phase == BATTLE_PHASE_PLAYER_SELECT ||
        vg->battle.phase == BATTLE_PHASE_PLAYER_DEFEND) {
        if (input_pressed(INPUT_START)) {
            item_screen_reset(g);
            screen_change(g, SCREEN_ITEM);
        } else if (input_pressed(INPUT_LEFT)) {
            audio_play_sfx(SFX_CURSOR);
            battle_cursor_move((Battle *)&vg->battle, -1);
        } else if (input_pressed(INPUT_RIGHT)) {
            audio_play_sfx(SFX_CURSOR);
            battle_cursor_move((Battle *)&vg->battle, 1);
        } else if (input_pressed(INPUT_UP)) {
            audio_play_sfx(SFX_CURSOR);
            battle_target_move((Battle *)&vg->battle, -1);
        } else if (input_pressed(INPUT_DOWN)) {
            audio_play_sfx(SFX_CURSOR);
            battle_target_move((Battle *)&vg->battle, 1);
        } else if (input_pressed(INPUT_A)) {
            battle_card_select((Battle *)&vg->battle);
        } else if (input_pressed(INPUT_B)) {
            battle_card_undo((Battle *)&vg->battle);
        } else if (input_pressed(INPUT_SELECT)) {
            battle_execute_combo((Battle *)&vg->battle);
        }
    }

    battle_update((Battle *)&vg->battle);
}

void battle_screen_render(Game *g)
{
    /* SDCC pointer-cache workaround: see battle_screen_update above.  The
     * render cache is likewise reached only through vg -- no cached
     * &g->render_cache pointer. */
    volatile Game *vg = g;
    uint8_t timer_bar;

    if (!g) return;
    timer_bar = ui_calc_timer_bar(vg->battle.timer_ticks);

    if (!vg->render_cache.valid ||
        vg->render_cache.prev_screen != SCREEN_BATTLE) {
        ui_lcd_off();
        ui_draw_battle_full((Battle *)&vg->battle);
        ui_lcd_on();
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_BATTLE, 0, 0, 0);
        vg->render_cache.valid = true;
        vg->render_cache.prev_screen = SCREEN_BATTLE;
    } else if (vg->battle.dirty) {
        ui_update_battle((Battle *)&vg->battle);
    } else if (vg->render_cache.prev_battle_timer_bar != timer_bar) {
        ui_draw_battle_timer((Battle *)&vg->battle);
    }
    vg->battle.dirty = 0;
    vg->render_cache.prev_battle_timer_bar = timer_bar;
}
