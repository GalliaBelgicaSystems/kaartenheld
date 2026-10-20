#include "game.h"
#include "screen.h"
#include "story.h"
#include "dialogue.h"
#include "interaction.h"
#include "telemetry.h"
#include "scenarios.h"
#include "content.h"
#include "scene.h"
#include "rpg/progression.h"
#include "rpg/party.h"
#include "banked.h"

/* Reset the render cache so the next render takes the full-redraw path.
 * Inline (not banked): pure WRAM writes, and the fixed bank has room
 * again. */
void game_render_reset(Game *g)
{
    uint8_t *p;
    uint16_t n;
    if (!g) return;
    p = (uint8_t *)&g->render_cache;
    n = sizeof(RenderCache);
    while (n--) *p++ = 0xFF;
    g->render_cache.valid = false;
    g->render_cache.prev_dialogue_active = false;
    g->render_cache.prev_screen = g->prev_screen;
    g->render_cache.prev_map_id = g->world.map_id;
    g->battle.dirty = BATTLE_DIRTY_ALL;
}

/* Reset the world to a fresh new-game state: canonical state, the runtime
 * world engine copy, dialogue, and the frame counter.  Does NOT choose a
 * screen or music -- the caller (boot->TITLE or in-game restart) decides
 * where the player lands (AGENTS.md 54.1). */
static void game_prepare_new_game(Game *g)
{
    g->frame = 0;
    g->game_over_choice = 0;
    g->shop_id = 0;
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
    /* Chest-jingle edge + restore (MUSIC_CHEST, one-shot) plus the
     * overworld HUD marker refresh (quest "?" / pursuer "!"): one
     * bank-8 round-trip per frame through the game-layer bodies
     * (game_frame_tick_banked runs the jingle then the marks refresh).
     * Same position as the old standalone jingle dispatch (after
     * screen_update): the edge sees this frame's state, which the
     * 240-frame harness backstop race depends on.  Ticks here rather
     * than in the overworld screen so menus, dialogue and shops don't
     * freeze the restore timer while the driver loops the one-shot
     * song.  Engine-generic restore (scene track lookup); action codes
     * come back through g_bk_byte_b (jingle), the pursuit alert through
     * g_bk_byte_c (marks: sting + hold) and the held-track replay through
     * g_bk_byte_d (marks: scene track restarts ~1 s after the sting so
     * the alert is heard first, then the music as pursuers approach). */
    {
        const SceneDefinition *def;
        extern void game_frame_tick_banked(void);
        g_bk_call_bank = 8;
        g_bk_call_target = (uint16_t)&game_frame_tick_banked;
        banked_call_run();
        /* byte_b == 2 (jingle restore) and byte_d == 1 (held-track
         * replay) are the same action: restart the scene track. */
        if (g_bk_byte_b == 1) {
            audio_play_music(MUSIC_CHEST);
        } else if (g_bk_byte_b == 2 || g_bk_byte_d == 1) {
            def = scene_definition_for_map(g->world.map_id);
            audio_play_music(def ? def->music : MUSIC_OVERWORLD);
        } else if (g_bk_byte_b == 3 || g_bk_byte_c == 1) {
            /* Title play-once stop, or the pursuit-alert hold: stop
             * in silence (the sting plays below; the byte_d replay
             * restarts the track from the top). */
            audio_play_music(MUSIC_NONE);
        }
        if (g_bk_byte_c == 1) {
            audio_play_sfx(SFX_ALERT);
        }
    }
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
    /* LCD-on transitions (no wipe white-flash): overworld -> dialogue
     * entry paints the box over the live map; dialogue -> overworld exit
     * restores just the box rows; an overworld map crossing runs the
     * black-wipe + invisible-load + row-reveal transition.  Every other
     * full redraw (battle, menus, boot) blanks via LCD-off.
     *
     * Exit classification reads rc->prev_screen (the last RENDERED
     * screen), never g->prev_screen (the last CHANGED-FROM screen, which
     * stays stale across a whole overworld session): after closing any
     * dialogue g->prev_screen remains DIALOGUE, which once misclassified
     * the next gate crossing as a dialogue overlay (sprites never
     * hidden) or -- after a menu -- forced ui_lcd_off (white flash).
     * (Entry classification still reads g->prev_screen: entry always
     * follows a synchronous screen_change, which refreshes it -- see
     * the exemption disjunct below.)
     * A dialogue exit that somehow lands on a different map still takes
     * the LCD-on path: overworld_screen_render() routes any map mismatch
     * to the full transition repaint (which reloads the tileset), so no
     * white flash and no stale tiles either way. */
    if (full_redraw &&
        !((g->prev_screen == SCREEN_OVERWORLD && g->screen == SCREEN_DIALOGUE) ||
          (rc->prev_screen == SCREEN_DIALOGUE && g->screen == SCREEN_OVERWORLD))) {
        /* A full redraw hides the sprite in real OAM first: the redraw
         * spans several display sweeps, and the sprite must not float
         * over it at a stale position.  The frame-boundary commit
         * (ui_sprite_commit in main.c) reveals it at the new position. */
        ui_sprite_begin_transition();
        if (rc->prev_screen != SCREEN_OVERWORLD || g->screen != SCREEN_OVERWORLD) {
            /* A full redraw is much larger than the part of VBlank remaining
             * after vsync() returns.  Keeping the LCD on here makes the PPU
             * drop the tail of the tilemap writes in modes 2/3, which
             * produces garbled menus, incomplete maps, and eventually a
             * white screen.  Disable the LCD for the complete transaction
             * instead.  This is also required for the boot redraw, which
             * runs before the first main-loop vsync().  Re-enable it
             * before returning so the next iteration can synchronize
             * normally. */
            ui_lcd_off();
            screen_render(g);
            ui_lcd_on();
            return;
        }
    }
    screen_render(g);
}
