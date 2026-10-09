#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "ui.h"
#include "audio.h"
#include "banked.h"

/* Classic title screen.  The heavy logo/menu rendering lives in a bank-2
 * body (banked.h constraint, AGENTS.md 52.11.1): the fixed _CODE/_HOME
 * area is at its size limit (see make memmap), so this file only runs a
 * thin input/transition wrapper around title_content_render().
 *
 * Flow: PRESS START -> NEW GAME / CONTINUE / SOUND.  SOUND toggles
 * g_sound_enabled (ON/OFF).  NEW GAME -> intro.  CONTINUE -> SAVE_LOAD
 * (load mode). */

void title_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;
    if (!rc->valid || rc->prev_screen != SCREEN_TITLE) {
        ui_clear_screen();
        /* Stream the bitmap logo into the world BG block before the bank-4
         * content body stamps its tilemap.  The loader's bank differs by
         * build: bank 5 in the harness build (layout-pinned), bank 2 in
         * release (title_logo_content.c).  Runs inside the LCD-off full
         * redraw, so the raw VRAM writes are safe. */
#ifdef DEBUG_BUILD
        g_bk_call_bank = 5;
#else
        g_bk_call_bank = 2;
#endif
        g_bk_call_target = (uint16_t)&ui_title_logo_load_banked;
        banked_call_run();
        g_bk_call_bank = 4;
        g_bk_call_target = (uint16_t)&title_content_render;
        g_bk_byte_a = g->title_menu_showing;
        g_bk_byte_b = g->title_menu_index;
        banked_call_run();
        telemetry_emit(EVENT_RENDER_SCREEN, (uint8_t)SCREEN_TITLE, 0, 0, 0);
        rc->valid = true;
        rc->prev_screen = SCREEN_TITLE;
    }
}

void title_content_render(void);
void title_menu_step_banked(void);

/* Targeted caret move (AGENTS.md 36).  The bank-4 body computes the next
 * index from the staged direction, writes it back through g_bk_ptr_a, and
 * repaints only the two '>' cells, so a pure UP/DOWN never touches the
 * render cache and the full title redraw is avoided. */
static void title_menu_step(Game *g, uint8_t direction)
{
    g_bk_call_bank = 4;
    g_bk_call_target = (uint16_t)&title_menu_step_banked;
    g_bk_ptr_a = (void *)g;
    g_bk_byte_b = direction;
    banked_call_run();
}

void title_screen_update(Game *g)
{
    if (!g) return;

    if (!g->title_menu_showing) {
        /* PRESS START splash: any start/a advances to the menu.  The
         * prompt blinks on the frame counter (every ~32 frames). */
        if (input_pressed(INPUT_START) || input_pressed(INPUT_A)) {
            g->title_menu_showing = 1;
            g->title_menu_index = 0;
            audio_play_sfx(SFX_CURSOR);
            g->render_cache.valid = false;
        }
        return;
    }

    if (input_pressed(INPUT_UP)) {
        title_menu_step(g, 0); /* UP */
        audio_play_sfx(SFX_CURSOR);
    }
    if (input_pressed(INPUT_DOWN)) {
        title_menu_step(g, 1); /* DOWN */
        audio_play_sfx(SFX_CURSOR);
    }

    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        switch (g->title_menu_index) {
        case 0: /* NEW GAME */
            audio_play_sfx(SFX_CONFIRM);
            telemetry_emit(EVENT_NEW_GAME_STARTED, 0, 0, 0, 0);
            g->intro_slide = 0;
            screen_change(g, SCREEN_INTRO);
            break;
        case 1: /* CONTINUE */
            audio_play_sfx(SFX_CONFIRM);
            telemetry_emit(EVENT_GAME_CONTINUED, 0, 0, 0, 0);
            g->save_slot_mode = 0; /* LOAD */
            g->save_slot_index = 0;
            screen_change(g, SCREEN_SAVE_LOAD);
            break;
        case 2: /* SOUND */
            g_sound_enabled = ~g_sound_enabled & 1;
            audio_play_sfx(SFX_CURSOR);
            telemetry_emit(EVENT_SOUND_TOGGLED, g_sound_enabled, 0, 0, 0);
            g->render_cache.valid = false;
            break;
        default: /* TUTORIAL */
            g->tutorial_slide = 0;
            screen_change(g, SCREEN_TUTORIAL);
            break;
        }
    }
}
