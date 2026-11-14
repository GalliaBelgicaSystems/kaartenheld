#include "game.h"
#include "screen.h"
#include "telemetry.h"
#include "ui.h"
#include "audio.h"
#include "banked.h"

/* Boot autosave notice (SPLASH -> NOTICE -> TITLE): a white screen
 * carrying the autosave line.  The bank-12 body owns the paint plus
 * the per-frame text-ink fade (white in, hold, white out); this fixed
 * wrapper owns timing, skip and the advance (fixed-bank budget,
 * AGENTS.md 52.18).  Skippable with A/START like the splash. */

/* Timeline (frames at ~60 fps): fade in 0-15, hold to 107, fade out
 * 108-122, advance at 123 (~2 s, skippable). */
#define NOTICE_HOLD_UNTIL 108
#define NOTICE_END        123

void notice_content_render(void);
void notice_fade_banked(void);

void notice_screen_render(Game *g)
{
    RenderCache *rc;
    if (!g) return;
    rc = &g->render_cache;
    if (!rc->valid || rc->prev_screen != SCREEN_NOTICE) {
        slide_screen_render(g, SCREEN_NOTICE,
                            (uint16_t)&notice_content_render, 0, 12);
    }
}

void notice_screen_update(Game *g)
{
    if (!g) return;
    if (input_pressed(INPUT_A) || input_pressed(INPUT_START)) {
        audio_play_music(MUSIC_TITLE);
        screen_change(g, SCREEN_TITLE);
        return;
    }
    g_bk_call_bank = 12;
    g_bk_call_target = (uint16_t)&notice_fade_banked;
    banked_call_run();
    g->notice_frame++;
    if (g->notice_frame >= NOTICE_END) {
        audio_play_music(MUSIC_TITLE);
        screen_change(g, SCREEN_TITLE);
    }
}
