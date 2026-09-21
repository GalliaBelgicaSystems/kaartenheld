#pragma bank 4
#pragma disable_warning 110

#include "ui.h"
#include "banked.h"
#include "screen.h"
#include "game.h"
#include "gfx/rpg_tile_lookup.h"
#include <gb/gb.h>
#include <gb/cgb.h>

/* Bank-4 self-contained title/splash/intro render bodies (AGENTS.md
 * 52.11.1): the fixed _CODE/_HOME area is at its size limit (see make
 * memmap), so the entire title logo + menu draw lives here, writing VRAM
 * directly with bank-local helpers (a banked body must never call
 * fixed-bank functions -- see ui_battle_content.c for the same pattern).
 *
 * Staged inputs (set by the fixed wrapper in title_screen.c):
 *   g_bk_byte_a = title_menu_showing (0 = PRESS START, 1 = menu)
 *   g_bk_byte_b = title_menu_index  (0 = NEW GAME, 1 = CONTINUE, 2 = SOUND)
 */

extern char g_ui_screen_buf[18][21];
extern uint8_t ui_font_tile_base;
extern uint8_t g_sound_enabled;

/* Generated title screen data (bank 4, read directly by the renderer) */
extern const char g_title_logo[][21];
extern uint8_t g_title_logo_x, g_title_logo_y, g_title_logo_count;
extern uint8_t const g_title_logo_image_enabled, g_title_logo_image_x,
    g_title_logo_image_y, g_title_logo_image_width, g_title_logo_image_height,
    g_title_logo_image_palette;
extern const char g_title_subtitle_text[21];
extern uint8_t const g_title_subtitle_x, g_title_subtitle_y;
extern const char g_title_graphic[5][21];
extern uint8_t const g_title_graphic_x, g_title_graphic_y, g_title_graphic_count, g_title_graphic_enabled;
extern const char g_title_prompt_text[21];
extern uint8_t g_title_prompt_x, g_title_prompt_y;
extern const char g_title_credits_text[21];
extern uint8_t const g_title_credits_x, g_title_credits_y, g_title_credits_enabled;
extern const char g_title_menu_options[4][21];
extern uint8_t g_title_menu_x, g_title_menu_caret_x, g_title_menu_first_row, g_title_menu_row_step, g_title_menu_count;

/* CGB calibration index (ui.c): palette programming is skipped on DMG. */
extern uint8_t g_is_cgb;

static void title_vram_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    if (LCDC_REG & 0x80) {
        while (STAT_REG & 0x02);
        *dst = tile;
    } else {
        *dst = tile;
    }
}

static void title_put_char(uint8_t x, uint8_t y, char ch)
{
    if (y < 18 && x < 20) {
        if (g_ui_screen_buf[y][x] != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            volatile uint8_t *dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
            VBK_REG = 0;
            title_vram_sync_write(dst, tile);
            g_ui_screen_buf[y][x] = ch;
        }
    }
}

static void title_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t max_chars)
{
    uint8_t i;
    char ch;
    volatile uint8_t *dst;
    char *buf;
    uint8_t ended;

    if (y >= 18) return;
    if ((uint8_t)(x + max_chars) > 20) max_chars = (uint8_t)(20 - x);

    VBK_REG = 0;
    ended = 0;
    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    buf = &g_ui_screen_buf[y][x];

    for (i = 0; i < max_chars; i++) {
        ch = text[i];
        if (ch == '\0') ended = 1;
        if (ended) ch = ' ';
        if (*buf != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            title_vram_sync_write(dst, tile);
            *buf = ch;
        }
        dst++;
        buf++;
    }
}

static void title_draw_logo(void)
{
    uint8_t i;
    for (i = 0; i < g_title_logo_count; i++) {
        title_draw_text(0, (uint8_t)(i + 1), g_title_logo[i], 19);
    }
}

/* CGB BG palette ramp for the bitmap logo (title-red.png): white, light
 * pink, red, dark red -- must match the `title` ramp in
 * assets/palette.txt (the sheet encodes against it).  Slot
 * g_title_logo_image_palette (1) is rewritten here each title redraw;
 * other screens reload CRAM on entry. */
static void title_program_logo_palette(void)
{
    static const palette_color_t ramp[4] = {
        RGB8(255, 255, 255), RGB8(228, 180, 180),
        RGB8(202, 106, 106), RGB8(139, 27, 27)
    };
    const uint8_t *bytes = (const uint8_t *)ramp;
    uint8_t i;

    if (!g_is_cgb) return;
    BCPS_REG = (uint8_t)(0x80 | (g_title_logo_image_palette << 3));
    for (i = 0; i < 8; i++) {
        BCPD_REG = bytes[i];
    }
}

/* Bitmap logo: BG tile ids RPG_TILE_BASE_WORLD.. (+width*height) stamped
 * at (x,y) with CGB palette `palette`.  The tile bytes were streamed into
 * the same VRAM block by ui_title_logo_load_banked() (bank 5 debug / 2 release) before this
 * body runs.  Runs with the LCD off (full redraw), so the raw writes need
 * no PPU wait; the overworld reloads the block on entry. */
static void title_draw_logo_image(void)
{
    uint8_t x = g_title_logo_image_x;
    uint8_t y = g_title_logo_image_y;
    uint8_t w = g_title_logo_image_width;
    uint8_t h = g_title_logo_image_height;
    uint8_t tile = (uint8_t)RPG_TILE_BASE_WORLD;
    uint8_t row, col;
    volatile uint8_t *dst;

    if ((uint8_t)(y + h) > 18) h = (uint8_t)(18 - y);
    if ((uint8_t)(x + w) > 20) w = (uint8_t)(20 - x);

    /* Tile indices (row-major, no 8-bit multiply). */
    for (row = 0; row < h; row++) {
        VBK_REG = 0;
        dst = (volatile uint8_t *)(0x9800 + ((uint16_t)(y + row) << 5) + x);
        for (col = 0; col < w; col++) {
            title_vram_sync_write(&dst[col], tile);
            tile++;
        }
    }

    /* CGB BG-palette attribute per tile (DMG ignores VRAM bank 1). */
    VBK_REG = 1;
    for (row = 0; row < h; row++) {
        dst = (volatile uint8_t *)(0x9800 + ((uint16_t)(y + row) << 5) + x);
        for (col = 0; col < w; col++) {
            dst[col] = g_title_logo_image_palette;
        }
    }
    VBK_REG = 0;

    /* The screen is a bitmap, so expose the logo's text identity in the
     * semantic screen buffer for harness assertions (AGENTS.md 53.7). */
    if (g_title_logo_count > 0) {
        for (col = 0; col < 20; col++) {
            g_ui_screen_buf[y][col] = g_title_logo[0][col];
        }
    }
}

static void title_draw_graphic(void)
{
    uint8_t i;
    if (!g_title_graphic_enabled) return;
    for (i = 0; i < g_title_graphic_count; i++) {
        title_draw_text(g_title_graphic_x, (uint8_t)(g_title_graphic_y + i), g_title_graphic[i], 20);
    }
}

static void title_draw_menu(uint8_t index)
{
    title_draw_text(3, 10, index == 0 ? "> NEW GAME" : "  NEW GAME", 11);
    title_draw_text(3, 12, index == 1 ? "> CONTINUE" : "  CONTINUE", 11);
    if (g_sound_enabled) {
        title_draw_text(3, 14, index == 2 ? "> SOUND: ON " : "  SOUND: ON ", 13);
    } else {
        title_draw_text(3, 14, index == 2 ? "> SOUND: OFF" : "  SOUND: OFF", 13);
    }
    title_draw_text(3, 16, index == 3 ? "> TUTORIAL" : "  TUTORIAL", 10);
}

void title_content_render(void)
{
    uint8_t showing = g_bk_byte_a;
    uint8_t index = g_bk_byte_b;

    if (g_title_logo_image_enabled) {
        title_program_logo_palette();
        title_draw_logo_image();
    } else {
        title_draw_logo();
    }

    if (g_title_subtitle_text[0] != ' ') {
        title_draw_text(g_title_subtitle_x, g_title_subtitle_y,
                        g_title_subtitle_text, 20);
    }

    if (!showing) {
        title_draw_graphic();
        title_draw_text(g_title_prompt_x, g_title_prompt_y, g_title_prompt_text, 20);
        if (g_title_credits_enabled) {
            title_draw_text(g_title_credits_x, g_title_credits_y, g_title_credits_text, 20);
        }
    } else {
        title_draw_menu(index);
        if (g_title_credits_enabled && g_title_credits_y > 16) {
            title_draw_text(g_title_credits_x, g_title_credits_y, g_title_credits_text, 20);
        }
    }
}

/* Pure caret move for the title menu (AGENTS.md 36): computes the next
 * index from the direction (staged in g_bk_byte_b, 1 = DOWN) and the
 * current index, writes it back through the staged Game* (WRAM always
 * mapped), and repaints only the two '>' cells -- mirroring
 * g_ui_screen_buf so the render cache stays valid and no full redraw
 * flickers on navigation.  Rows are 10, 12, 14, 16 for indices 0..3. */
void title_menu_step_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t index;
    uint8_t new_index;
    uint8_t old_row;
    uint8_t new_row;
    uint8_t max_idx = 3;

    if (!g) return;
    index = g->title_menu_index;
    if (g_bk_byte_b) {
        if (index == max_idx) new_index = 0;
        else new_index = (uint8_t)(index + 1);
    } else {
        if (index == 0) new_index = max_idx;
        else new_index = (uint8_t)(index - 1);
    }
    old_row = (uint8_t)(10 + (index << 1));
    new_row = (uint8_t)(10 + (new_index << 1));
    if (old_row != new_row) {
        title_put_char(3, old_row, ' ');
        title_put_char(3, new_row, '>');
    }
    g->title_menu_index = new_index;
}

/* ── Intro: three scripted ASCII slides ───────────────────────────── */

static const char s_intro_0[4][20] = {
    "A troubled land",
    "calls out for a",
    "hero of a new",
    "kind.",
};
static const char s_intro_1[4][20] = {
    "Your strength is",
    "not in steel,",
    "but in the cards",
    "you carry.",
};
static const char s_intro_2[4][20] = {
    "Only the Lord of",
    "Slimes stands",
    "between all that",
    "lives and the end.",
};

void intro_content_render(void)
{
    uint8_t slide = g_bk_byte_a;
    const char (*lines)[20];
    uint8_t count = 4;
    uint8_t i;

    if (slide == 0) lines = s_intro_0;
    else if (slide == 1) lines = s_intro_1;
    else lines = s_intro_2;

    for (i = 0; i < count; i++) {
        title_draw_text(3, (uint8_t)(6 + i), lines[i], 18);
    }
    title_draw_text(5, 16, "[A] NEXT", 10);
}
