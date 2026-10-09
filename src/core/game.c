#include "game.h"
#include "screen.h"
#include "story.h"
#include "dialogue.h"
#include "interaction.h"
#include "telemetry.h"
#include "scenarios.h"
#include "content.h"
#include "rpg/progression.h"
#include "rpg/party.h"
#include "banked.h"

void game_render_reset(Game *g)
{
    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&game_render_reset_banked;
    g_bk_ptr_a = (void *)g;
    banked_call_run();
}

/* Reset the world to a fresh new-game state: canonical state, the runtime
 * world engine copy, dialogue, and the frame counter.  Does NOT choose a
 * screen or music -- the caller (boot->TITLE or in-game restart) decides
 * where the player lands (AGENTS.md 54.1). */
static void game_prepare_new_game(Game *g)
{
    g->frame = 0;
    g->game_over_choice = 0;
    g->shop_id = 1;
    game_new_game(&g->state);
    world_init(&g->world, &g->state);
    dialogue_init(&g->dialogue);
    game_render_reset(g);
}

/* Start a fresh playthrough at the overworld (ENDING restart, GAME_OVER
 * YES, and the last intro slide's drop into the world). */
void game_restart(Game *g)
{
    if (!g) return;
    g->screen = SCREEN_OVERWORLD;
    g->prev_screen = SCREEN_OVERWORLD;
    game_prepare_new_game(g);
}

void game_init(Game *g)
{
    if (!g) return;
    /* Install the WRAM banked-copy trampoline before any banked content
     * (event/dialogue tables) can be read.  Runs here rather than in CRT0
     * init because the harness jumps straight to main(). */
    banked_copy_init();
    banked_call_init();
    telemetry_init();
    telemetry_set_frame_ptr(&g->frame);
    /* Boot into the studio splash, which auto-advances (or is skipped)
     * onto the classic title screen.  A fresh new-game state sits behind
     * both screens. */
    g->screen = SCREEN_SPLASH;
    g->prev_screen = SCREEN_SPLASH;
    g->title_menu_showing = 0;
    g->title_menu_index = 0;
    g->intro_slide = 0;
    game_prepare_new_game(g);
    /* On real hardware the `uint8_t g_sound_enabled = 1` default is applied
     * by CRT0's .data copy; the debug harness skips CRT0 and jumps straight
     * to main(), so re-assert the sound-on default here (matches the
     * hardware boot state).  Without this, audio_play_music() no-ops under
     * the harness and music_track assertions report NONE. */
    g_sound_enabled = 1;
    audio_play_music(MUSIC_TITLE);
    game_render(g);

#ifdef DEBUG_BUILD
    debug_snapshot();
#endif
}

void game_update(Game *g)
{
    if (!g) return;

#ifdef DEBUG_BUILD
    scenario_check_and_load();
#endif
    g->frame++;

    screen_update(g);
    scene_sync_from_world(g);
    /* The world player entity mirrors the canonical party HP so the
     * overworld HUD and the core snapshot reflect healing immediately. */
    g->world.player.hp = g->state.party.members[0].hp;
    g->world.player.max_hp = g->state.party.members[0].max_hp;

#ifdef DEBUG_BUILD
    debug_snapshot();
#endif
}

void game_render(Game *g)
{
    RenderCache *rc;
    uint8_t full_redraw;

    if (!g) return;
    rc = &g->render_cache;

    /* Any frame that performs a full redraw (screen change, map change,
     * boot/restart) hides the sprite in real OAM first: the redraw takes
     * several display sweeps (blank then top-to-bottom redraw), and the
     * sprite must not float over the wipe at a stale position.  The
     * frame-boundary commit (ui_sprite_commit in main.c, after vsync)
     * reveals it at the new screen's position once the redraw is done.
     *
     * The three triggers are distinct:
     *  - rc->valid == false: screen change / boot / restart (reset);
     *  - prev_screen != g->screen: a transition whose reset pre-dates the
     *    render (both halves of a frame with a screen change);
     *  - world.map_id != prev_map_id: a gate crossing.  This goes through
     *    world_change_map() without a screen change and without resetting
     *    the render cache, so overworld_screen_render() wipes the display
     *    based only on the map_id mismatch.  The condition mirrors that
     *    branch.
     *
     * On steady non-overworld frames (battle/dialogue/menu) none of the
     * three fire: prev_map_id was initialized to the current map at the
     * last reset and the map does not change while those screens are up,
     * so the sprite is NOT re-hidden every frame.  That per-frame re-hide
     * (from the old 255 prev_map_id sentinel) made the sprite invisible
     * for the whole fight/discussion on real hardware. */
    full_redraw = (!rc->valid || rc->prev_screen != g->screen ||
                   g->world.map_id != rc->prev_map_id);
    if (full_redraw) {
        ui_sprite_begin_transition();
        /* A full redraw is much larger than the part of VBlank remaining
         * after vsync() returns.  Keeping the LCD on here makes the PPU drop
         * the tail of the tilemap writes in modes 2/3, which produces
         * garbled menus, incomplete maps, and eventually a white screen.
         * Disable the LCD for the complete transaction instead.  This is
         * also required for the boot redraw, which runs before the first
         * main-loop vsync().  Re-enable it before returning so the next
         * iteration can synchronize normally. */
        ui_lcd_off();
        screen_render(g);
        ui_lcd_on();
        return;
    }
    screen_render(g);
}
