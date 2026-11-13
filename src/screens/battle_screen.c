#include "game.h"
#include "screen.h"
#include "battle.h"
#include "scene.h"
#include "telemetry.h"
#include "audio.h"
#include "banked.h"
#include "content.h"
#include "ui.h"
#include "rpg/cards.h"
#include "rpg/loot.h"
#include "rpg/status.h"

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

    /* Carl's sparring tutorial (game layer): opens each coaching dialogue
     * box once at the right phase.  True means a dialogue opened this
     * frame -- yield, the dialogue screen owns input now. */
    if (game_carl_tutorial_update(g)) {
        return;
    }

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
                    /* Deferred victory dialogue (e.g. Carl's reward
                     * speech, started by the ACTOR_DEFEATED event inside
                     * world_on_battle_end): show it before leaving. */
                    screen_change(g, vg->dialogue.active ? SCREEN_DIALOGUE :
                        game_screen_after_victory(g));
                    /* Final-boss victory exits to the ending screen (THE
                     * END credits): switch from the area track to the
                     * end-credits theme (demo complete.uge, loops). */
                    if (g->screen == SCREEN_ENDING) {
                        audio_play_music(MUSIC_ENDING);
                    }
                } else {
                    world_on_battle_fled(g);
                    screen_change(g, SCREEN_OVERWORLD);
                }
            } else {
                /* Slime-lord desperation outro (unwinnable final battle):
                 * game-owned flow in content.c (own function keeps this
                 * frame lean, AGENTS.md 52.18); the flag stays set for the
                 * dialogue close (which routes to THANKS) and battle_start
                 * clears it for the next fight. */
                if (g_slime_despair != 0) {
                    game_slime_despair_outro();
                    return;
                }
                world_on_battle_end(g, false);
                vg->game_over_choice = 0;
                /* Stop everything: the per-frame tick is gated off on
                 * terminal screens, so without this the game-over theme
                 * from the defeat would drone on under GAME_OVER. */
                audio_play_music(MUSIC_NONE);
                screen_change(g, SCREEN_GAME_OVER);
            }
        }
        return;
    }

    if (vg->battle.phase == BATTLE_PHASE_PLAYER_SELECT ||
        vg->battle.phase == BATTLE_PHASE_PLAYER_DEFEND) {
        /* Confused hero: a random hand gets played -- no input this turn
         * (except START, which stays reachable while the staged hand
         * waits out the visit).  The banked body stages the random hand
         * when the hero is poisoned: a fresh stage holds its beat (the
         * player reads the Confused! message), an already-pending stage
         * counts the hold down and executes at zero.  The WRAM slot
         * check up front keeps decision frames with no live status out
         * of the banked call entirely (any hero status opens the gate;
         * the body itself stages only for poison). */
        uint8_t confuse_stage = 0;
        if (input_pressed(INPUT_START)) {
            item_screen_reset(g);
            screen_change(g, SCREEN_ITEM);
        } else {
            if (s_battle_status[0].count != 0) {
                confuse_stage = battle_confuse_auto((Battle *)&vg->battle);
            }
            if (confuse_stage == 2) {
                g_battle_confuse_hold = 30;
            } else if (confuse_stage == 1) {
                if (g_battle_confuse_hold != 0) {
                    g_battle_confuse_hold--;
                } else {
                    battle_execute_combo((Battle *)&vg->battle);
                }
            } else if (input_pressed(INPUT_LEFT) || input_pressed(INPUT_RIGHT)) {
                audio_play_sfx(SFX_CURSOR);
                battle_cursor_move((Battle *)&vg->battle, input_pressed(INPUT_LEFT) ? -1 : 1);
            } else if (input_pressed(INPUT_UP) || input_pressed(INPUT_DOWN)) {
                audio_play_sfx(SFX_CURSOR);
                battle_target_move((Battle *)&vg->battle, input_pressed(INPUT_UP) ? -1 : 1);
            } else if (input_pressed(INPUT_A) &&
                       vg->battle.phase == BATTLE_PHASE_PLAYER_DEFEND &&
                       vg->battle.msg_id >= BATTLE_MSG_ICED_FOE &&
                       vg->battle.msg_id <= BATTLE_MSG_CONFUSED_OUTCOME) {
                /* Acknowledgement beats (msg ids 5-7 are contiguous).
                 * Iced (5): nothing staged, nothing incoming -- resolve
                 * the no-op through the shared combo path.  Confused:
                 * the FIRST A reveals who takes the redirected swing
                 * (6 -> 7), the SECOND resolves it.  The hero stages
                 * nothing either way; every A press is consumed here,
                 * so no card selection is reachable during the beat. */
                if (vg->battle.msg_id == BATTLE_MSG_CONFUSED_FOE) {
                    vg->battle.msg_id = BATTLE_MSG_CONFUSED_OUTCOME;
                    vg->battle.dirty = BATTLE_DIRTY_ALL;
                } else {
                    battle_execute_combo((Battle *)&vg->battle);
                }
            } else if (input_pressed(INPUT_A)) {
                battle_card_select((Battle *)&vg->battle);
            } else if (input_pressed(INPUT_B)) {
                battle_card_undo((Battle *)&vg->battle);
            } else if (input_pressed(INPUT_SELECT)) {
                battle_execute_combo((Battle *)&vg->battle);
            }
        }
    } else if (vg->battle.phase == BATTLE_PHASE_SLIME_DESPAIR) {
        /* The telegraph waits for A: the fixed 99-damage reprisal lands
         * here (not at trigger time), then the normal defeat exit routes
         * the outro.  All other buttons are ignored while he focuses.
         * The spell message + damage are applied by the bank-8 body
         * (literals and the damage clamp would eat fixed-bank budget).
         * The explosion cue plays over the boss track, which keeps
         * playing underneath.  No explicit return: the defeat result
         * makes battle_update return at once, otherwise it keeps
         * ticking the flash cadence below. */
        if (input_pressed(INPUT_A)) {
            g_bk_call_bank = 8;
            g_bk_call_target = (uint16_t)&game_despair_reprisal_banked;
            banked_call_run();
            telemetry_emit(EVENT_DAMAGE_RECEIVED, 99, 0, 0, 0);
            /* Desperation move: the Lord of Slimes casts POWERFUL SPELL
             * (assets/sfx/explosion.uge, SFX_EXPLOSION) over silence --
             * the boss track was already stopped when the telegraph
             * staged, so the cue lands clean.  No game-over music starts
             * on this path (battle_set_result skips it for the despair
             * hold): the DEFEATED! result screen plays out in silence
             * under the explosion tail. */
            audio_play_sfx(SFX_EXPLOSION);
            battle_set_result((Battle *)&vg->battle, BATTLE_RESULT_DEFEAT);
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
    } else {
        if (vg->battle.dirty) {
            ui_update_battle((Battle *)&vg->battle);
        }
        /* Independent if (not else-if): a dirty frame coinciding with a
         * bar-tick change must still redraw the bar, or the bar freezes
         * for a full second while prev_battle_timer_bar moves on. */
        if (vg->render_cache.prev_battle_timer_bar != timer_bar) {
            ui_draw_battle_timer((Battle *)&vg->battle);
        }
    }
    vg->battle.dirty = 0;
    vg->render_cache.prev_battle_timer_bar = timer_bar;
}
