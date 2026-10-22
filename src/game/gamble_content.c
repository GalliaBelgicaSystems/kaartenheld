#pragma bank 8
#pragma disable_warning 110

#include "game/gamble.h"
#include "ui.h"
#include "core/game.h"
#include "core/banked.h"
#include "rng.h"
#include "game/game_ids.h"
#include "input.h"
#include "audio.h"
#include "gfx/gamble_art.h"
#include <gb/gb.h>

/* Chuck-a-luck banked bodies (ROM bank 8, overflow bank with room;
 * AGENTS.md 55.5).  The fixed bank has ~50 B headroom, so the fixed
 * side of this feature is only the screen.c dispatch case and the
 * update stub (~30 B): entry loads, paint, input, dice, OAM and gold
 * all live here behind the single frame body.
 *
 * Banked-body rules (52.11.1/52.11.3): never call fixed-bank functions
 * (no input_pressed/audio/currency/telemetry here), never hold di.
 * Input edges come straight from g_inp_pad/g_inp_prev (WRAM, always
 * mapped -- the established in-bank pattern, input.h).  RNG advances
 * via the inlined xorshift step on g_rng_state (same step as
 * rng_next_banked).  Art bytes come from the codegen bundle above
 * (tools/gamble_art_compile.py, regenerated every build from the live
 * battle sources -- never hand-copied, so the artist cannot silently
 * desync it).
 *
 * Audio: casino theme while the table is up, cursor blips on moves, the
 * block thump on a loss and the chest jingle on a win.  Requests are
 * staged in WRAM (g_gamble_sfx / g_gamble_jingle / g_bk_byte_b) and
 * consumed by the fixed bank, because a banked body cannot call the
 * audio API (52.11.1); no fixed-bank bytes are spent on the gamble
 * feature for this.
 *
 * v1 gap: gold moves by direct WRAM writes, so no CURRENCY_SPENT/ADDED
 * telemetry (the extended snapshot still shows every gold delta). */

/* Canonical gamble state (WRAM BSS: no _INITIALIZER cost, AGENTS.md
 * 52.24 -- the frame body resets every field on entry). */
GambleState g_gamble;

/* Audio staging (WRAM BSS).  Banked bodies cannot call the fixed-bank
 * audio API, so requests ride these bytes out to game_update():
 *  - g_gamble_sfx: one-shot SFX id + 1 (0 = none; BSS-safe because
 *    the custom CRT0 never copies _INITIALIZER), consumed there.
 *  - g_gamble_jingle: set on a winning roll; the tick body turns it
 *    into the shared chest-jingle request (byte_b == 1), which already
 *    has a fixed consumer. */
uint8_t g_gamble_sfx;
uint8_t g_gamble_jingle;
uint8_t g_gamble_enter;
uint8_t g_gamble_bye;

/* Prev-state snapshot (WRAM BSS): the dynamic path repaints only on
 * change.  Uninitialized by design: the entry paint runs
 * unconditionally and snapshots afterwards, so these are never read
 * before their first write. */
static uint8_t s_gprev_chosen;
static uint8_t s_gprev_bet;
static uint8_t s_gprev_field;
static uint8_t s_gprev_phase;
static uint8_t s_gprev_d0;
static uint8_t s_gprev_d1;
static uint8_t s_gprev_d2;
static int16_t s_gprev_gold;

static void gamble_mark_seen(void)
{
    s_gprev_chosen = g_gamble.chosen;
    s_gprev_bet = g_gamble.bet_idx;
    s_gprev_field = g_gamble.field;
    s_gprev_phase = g_gamble.phase;
    s_gprev_d0 = g_gamble.dice[0];
    s_gprev_d1 = g_gamble.dice[1];
    s_gprev_d2 = g_gamble.dice[2];
    s_gprev_gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
}

static uint8_t gamble_changed(void)
{
    if (s_gprev_chosen != g_gamble.chosen) return 1;
    if (s_gprev_bet != g_gamble.bet_idx) return 1;
    if (s_gprev_field != g_gamble.field) return 1;
    if (s_gprev_phase != g_gamble.phase) return 1;
    if (s_gprev_d0 != g_gamble.dice[0]) return 1;
    if (s_gprev_d1 != g_gamble.dice[1]) return 1;
    if (s_gprev_d2 != g_gamble.dice[2]) return 1;
    if (s_gprev_gold != g_game.state.currency.amount[CURRENCY_ID_GOLD - 1]) return 1;
    return 0;
}

/* Clamp the bet step to what the hero can afford (1/5/10 down to the
 * highest affordable; gold 0 keeps idx 0 and the roll gate refuses). */
static void gamble_clamp_bet(void)
{
    int16_t gold;
    uint8_t bet;

    gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
    bet = (g_gamble.bet_idx == 1) ? 5 : ((g_gamble.bet_idx == 2) ? 10 : 1);
    /* int16 compare: (uint8_t)gold would wrap for gold 256-265 and
     * clamp an affordable bet for no reason. */
    while ((int16_t)bet > gold && g_gamble.bet_idx > 0) {
        g_gamble.bet_idx--;
        bet = (g_gamble.bet_idx == 1) ? 5 : ((g_gamble.bet_idx == 2) ? 10 : 1);
    }
}

/* Roll + settle (banked WRAM writes; fixed-side currency_add would cost
 * fixed bytes the budget does not have -- see the header note).
 * Net: lose -> -bet; win with k matches -> +bet*k (stake back + k:1
 * profit).  No 8-bit * (lib would land in fixed _CODE, 52.18). */
static void gamble_do_roll(void)
{
    uint16_t s;
    uint8_t v, k, m;
    uint8_t bet;
    uint8_t profit;
    int16_t gold;

    bet = (g_gamble.bet_idx == 1) ? 5 : ((g_gamble.bet_idx == 2) ? 10 : 1);
    gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
    if (gold < (int16_t)bet || gold <= 0) return;

    for (k = 0; k < 3; k++) {
        /* Uniform 0..5: reject 252..255 (256 = 42*6 + 4), then fold by
         * repeated subtraction (no 8-bit * or %: those pull the SDCC
         * mult/div library into fixed _CODE even from a banked body). */
        do {
            s = g_rng_state;
            s ^= (uint16_t)(s << 7);
            s ^= (uint16_t)(s >> 9);
            s ^= (uint16_t)(s << 8);
            g_rng_state = s;
            v = (uint8_t)s;
        } while (v >= 252);
        while (v >= 6) v = (uint8_t)(v - 6);
        g_gamble.dice[k] = (uint8_t)(v + 1);
    }
    m = 0;
    for (k = 0; k < 3; k++) {
        if (g_gamble.dice[k] == g_gamble.chosen) m++;
    }
    g_gamble.matches = m;
    gold = (int16_t)(gold - bet);
    if (m != 0) {
        profit = 0;
        for (k = 0; k < m; k++) profit = (uint8_t)(profit + bet);
        /* Saturate instead of wrapping: the state field is int16_t and
         * currency_add() does not clamp either, so a hero near the
         * ceiling would gain a negative purse.  bet + profit <= 40. */
        if (gold > (int16_t)(32767 - (int16_t)(bet + profit))) {
            gold = 32767;
        } else {
            gold = (int16_t)(gold + bet + profit);
        }
        g_gamble.won = 1;
    } else {
        g_gamble.won = 0;
    }
    g_game.state.currency.amount[CURRENCY_ID_GOLD - 1] = gold;
    g_gamble.phase = GAMBLE_PHASE_RESULT;
    /* Result audio: a win rings the chest-loot jingle (the shared
     * one-shot, queued as g_gamble_jingle for the tick body); a loss
     * gets the same thump a blocked attack uses. */
    if (m != 0) {
        g_gamble_jingle = 1;
    } else {
        g_gamble_sfx = (uint8_t)(SFX_BLOCK + 1);
    }
}

/* ── Paint helpers (self-contained copies of the bank-3 battle VRAM
 * helpers: a banked body must never call fixed-bank ui functions).
 * LCD-off full paint and LCD-on dynamic refresh share these; every
 * store goes through a Mode-gated write (52.11.3). ────────────────── */

extern uint8_t ui_font_tile_base;
extern uint8_t g_is_cgb;
#ifdef DEBUG_BUILD
extern uint8_t g_tilemap_mirror[32 * 32];
extern volatile uint8_t g_tilemap_attr_mirror[32 * 32];
#endif

static void gamble_vram_sync_write(volatile uint8_t *dst, uint8_t tile)
{
    BANKED_VRAM_WRITE(dst, tile);
}

static void gamble_put_char(uint8_t x, uint8_t y, char ch)
{
    if (y < 18 && x < 20) {
        if (g_ui_screen_buf[y][x] != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            volatile uint8_t *dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
            VBK_REG = 0;
            gamble_vram_sync_write(dst, tile);
            g_ui_screen_buf[y][x] = ch;
        }
    }
}

/* Unconditional tile stamp (dice / card frames): art cells bypass the
 * semantic buffer's skip-guard like the battle stamper (the blank path
 * clears the same footprint, so unconditional writes are safe).  The
 * buffer keeps a representative char for harness text assertions. */
static void gamble_put_tile(uint8_t x, uint8_t y, char buf_char, uint8_t tile)
{
    if (y < 18 && x < 20) {
        volatile uint8_t *dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
        VBK_REG = 0;
        gamble_vram_sync_write(dst, tile);
        g_ui_screen_buf[y][x] = buf_char;
#ifdef DEBUG_BUILD
        g_tilemap_mirror[y * 32 + x] = tile;
#endif
    }
}

static void gamble_draw_text_line(uint8_t x, uint8_t y, const char *text,
                                  uint8_t max_chars)
{
    uint8_t i;
    char ch;
    volatile uint8_t *dst;
    char *buf;
    uint8_t ended;

    if (y >= 18 || x >= 20) return;
    if ((uint8_t)(x + max_chars) > 20) max_chars = (uint8_t)(20 - x);

    VBK_REG = 0;
    ended = (text == 0);
    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    buf = &g_ui_screen_buf[y][x];

    for (i = 0; i < max_chars; i++) {
        if (!ended) {
            ch = text[i];
            if (ch == '\0') {
                ended = 1;
                ch = ' ';
            }
        } else {
            ch = ' ';
        }
        if (*buf != ch) {
            uint8_t tile = (uint8_t)(ui_font_tile_base + (uint8_t)(ch - ' '));
            gamble_vram_sync_write(dst, tile);
            *buf = ch;
        }
        dst++;
        buf++;
    }
}

static void gamble_color_span(uint8_t x, uint8_t y, uint8_t len, uint8_t palette)
{
    uint8_t i;
    uint8_t v;
    volatile uint8_t *dst;

    if (!g_is_cgb) return;
    if (y >= 18 || x >= 32) return;
    if ((uint8_t)(x + len) > 32) len = (uint8_t)(32 - x);

    dst = (volatile uint8_t *)(0x9800 + ((uint16_t)y << 5) + x);
    VBK_REG = 1;
    for (i = 0; i < len; i++) {
        v = (uint8_t)(palette & 0x07);
        gamble_vram_sync_write(&dst[i], v);
#ifdef DEBUG_BUILD
        g_tilemap_attr_mirror[(y & 31) * 32 + ((x + i) & 31)] = v;
#endif
    }
    VBK_REG = 0;
}

/* Compact decimal (no padding): returns the digit count.  No / or %
 * (div lib would land in fixed _CODE): tens by repeated subtraction. */
static uint8_t gamble_draw_num(uint8_t x, uint8_t y, int16_t val)
{
    char digit[6];
    uint8_t n = 0;
    uint8_t neg = 0;
    uint16_t u;
    uint8_t i;

    if (val < 0) {
        int16_t t;
        neg = 1;
        t = (int16_t)(0 - val);
        u = (uint16_t)t;
    } else {
        u = (uint16_t)val;
    }
    if (u == 0) {
        digit[0] = '0';
        n = 1;
    } else {
        uint8_t started = 0;
        uint16_t mag = 10000;
        while (mag != 0) {
            uint8_t d = 0;
            while (u >= mag) {
                u = (uint16_t)(u - mag);
                d++;
            }
            if (d != 0 || started || mag == 1) {
                started = 1;
                if (n < 5) digit[n++] = (char)('0' + d);
            }
            /* Next magnitude down (no division): 10000 -> 1000 -> ... */
            if (mag == 10000) mag = 1000;
            else if (mag == 1000) mag = 100;
            else if (mag == 100) mag = 10;
            else if (mag == 10) mag = 1;
            else mag = 0;
        }
        if (n == 0) {
            digit[0] = '0';
            n = 1;
        }
    }
    if (neg) {
        gamble_put_char(x, y, '-');
        x++;
    }
    for (i = 0; i < n; i++) {
        gamble_put_char((uint8_t)(x + i), y, digit[i]);
    }
    return (uint8_t)(n + neg);
}

static void gamble_draw_banner(void)
{
    const char *text = "CHUCK-A-LUCK";
    uint8_t len = 0;
    uint8_t x;

    while (len < 20 && text[len] != '\0') len++;
    x = (uint8_t)((20 - len) >> 1);
    gamble_draw_text_line(0, 0, 0, 20);
    gamble_draw_text_line(x, 0, text, (uint8_t)(20 - x));
    gamble_color_span(0, 0, 20, UI_COLOR_NONE);
}

/* Row 5: RESULT announcement directly below the kobold art (more
 * visible than the old row-0 banner spot), blank otherwise. */
static void gamble_draw_outcome(void)
{
    char winbuf[12];
    uint8_t len = 0;
    uint8_t x;
    uint8_t i;
    uint8_t bet;
    uint8_t profit;
    uint8_t tens;
    uint8_t ones;
    const char *text;

    gamble_draw_text_line(0, 5, 0, 20);
    if (g_gamble.phase != GAMBLE_PHASE_RESULT) {
        gamble_color_span(0, 5, 20, UI_COLOR_NONE);
        return;
    }
    if (g_gamble.won) {
        /* Inline bet map (banked bodies must not call fixed-bank
         * functions): idx 0->1, 1->5, 2->10.  Tens by subtraction
         * (no / or %: div lib would land in fixed _CODE, 52.18).
         * Profit tops at 30 (10 x 3 matches). */
        bet = (g_gamble.bet_idx == 1) ? 5 : ((g_gamble.bet_idx == 2) ? 10 : 1);
        profit = 0;
        for (i = 0; i < g_gamble.matches; i++) profit = (uint8_t)(profit + bet);
        tens = 0;
        ones = profit;
        while (ones >= 10) {
            ones = (uint8_t)(ones - 10);
            tens++;
        }
        winbuf[0] = 'Y';
        winbuf[1] = 'O';
        winbuf[2] = 'U';
        winbuf[3] = ' ';
        winbuf[4] = 'W';
        winbuf[5] = 'I';
        winbuf[6] = 'N';
        winbuf[7] = ' ';
        winbuf[8] = '+';
        if (tens != 0) {
            winbuf[9] = (char)('0' + tens);
            winbuf[10] = (char)('0' + ones);
            winbuf[11] = '\0';
        } else {
            winbuf[9] = (char)('0' + ones);
            winbuf[10] = '\0';
        }
        text = winbuf;
    } else {
        text = "NO LUCK";
    }
    while (len < 20 && text[len] != '\0') len++;
    x = (uint8_t)((20 - len) >> 1);
    gamble_draw_text_line(x, 5, text, (uint8_t)(20 - x));
    gamble_color_span(0, 5, 20, UI_COLOR_NONE);
}

/* Row 2: kobold identity (replaces the battle enemy-HP row). */
static void gamble_draw_kobold_row(void)
{
    gamble_draw_text_line(0, 2, 0, 20);
    gamble_draw_text_line(8, 2, "Nis", 3);
    gamble_color_span(0, 2, 20, UI_COLOR_NONE);
}

/* Info rows (7-9): CHOOSE BET / CHOOSE NUMBER / GOLD OWNED, with the
 * '>' caret on the active row during CHOOSE only (RESULT freezes the
 * values with no caret).  Every row is fully cleared first (stale-tail
 * hazard, 52.25 family).  The rules + YES/NO proposal now lives in the
 * kobold's overworld dialogue, not on this screen. */
static void gamble_draw_gold_rows(void)
{
    int16_t gold;
    uint8_t bet;

    gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
    bet = (g_gamble.bet_idx == 1) ? 5 : ((g_gamble.bet_idx == 2) ? 10 : 1);

    gamble_draw_text_line(0, 7, 0, 20);
    gamble_draw_text_line(0, 8, 0, 20);
    gamble_draw_text_line(0, 9, 0, 20);
    gamble_put_char(0, 7, (g_gamble.phase == GAMBLE_PHASE_CHOOSE && g_gamble.field == 0) ? '>' : ' ');
    gamble_put_char(0, 8, (g_gamble.phase == GAMBLE_PHASE_CHOOSE && g_gamble.field == 1) ? '>' : ' ');
    gamble_put_char(0, 9, ' ');
    gamble_draw_text_line(1, 7, "CHOOSE BET:", 11);
    gamble_draw_num((uint8_t)(1 + 11), 7, (int16_t)bet);
    gamble_draw_text_line(1, 8, "CHOOSE NUMBER:", 14);
    gamble_put_char((uint8_t)(1 + 14), 8, (char)('0' + g_gamble.chosen));
    gamble_draw_text_line(0, 9, "GOLD OWNED:", 11);
    gamble_draw_num(11, 9, gold);
    gamble_color_span(0, 7, 20, UI_COLOR_FIELD);
    gamble_color_span(0, 8, 20, UI_COLOR_FIELD);
    gamble_color_span(0, 9, 20, UI_COLOR_FIELD);
}

/* Row 10: match detail during RESULT, blank during CHOOSE. */
static void gamble_draw_mid_row(void)
{
    char buf[17];
    uint8_t i;
    uint8_t bet;
    uint8_t profit;
    uint8_t tens;
    uint8_t ones;

    gamble_draw_text_line(0, 10, 0, 20);
    if (g_gamble.phase == GAMBLE_PHASE_RESULT) {
        bet = (g_gamble.bet_idx == 1) ? 5 : ((g_gamble.bet_idx == 2) ? 10 : 1);
        if (g_gamble.won) {
            profit = 0;
            for (i = 0; i < g_gamble.matches; i++) profit = (uint8_t)(profit + bet);
            tens = 0;
            ones = profit;
            while (ones >= 10) {
                ones = (uint8_t)(ones - 10);
                tens++;
            }
            buf[0] = (char)('0' + g_gamble.matches);
            buf[1] = ' ';
            buf[2] = 'M';
            buf[3] = 'A';
            buf[4] = 'T';
            buf[5] = 'C';
            buf[6] = 'H';
            buf[7] = ' ';
            buf[8] = 'W';
            buf[9] = 'I';
            buf[10] = 'N';
            buf[11] = ' ';
            buf[12] = '+';
            if (tens != 0) {
                buf[13] = (char)('0' + tens);
                buf[14] = (char)('0' + ones);
                buf[15] = '\0';
            } else {
                buf[13] = (char)('0' + ones);
                buf[14] = '\0';
            }
        } else {
            buf[0] = '0';
            buf[1] = ' ';
            buf[2] = 'M';
            buf[3] = 'A';
            buf[4] = 'T';
            buf[5] = 'C';
            buf[6] = 'H';
            buf[7] = ' ';
            buf[8] = 'L';
            buf[9] = 'O';
            buf[10] = 'S';
            buf[11] = 'E';
            buf[12] = ' ';
            buf[13] = '-';
            if (bet >= 10) {
                buf[14] = '1';
                buf[15] = '0';
                buf[16] = '\0';
            } else {
                buf[14] = (char)('0' + bet);
                buf[15] = '\0';
            }
        }
        gamble_draw_text_line(0, 10, buf, 17);
        gamble_color_span(0, 10, 17, UI_COLOR_FIELD);
    } else {
        gamble_color_span(0, 10, 20, UI_COLOR_NONE);
    }
}

/* Dice boxes (rows 11-14): 3 card-frame boxes at cols 2, 8, 14, each
 * 3 wide x 4 tall (top = cards_row - 3, matching the battle layout).
 * Center cell (row 12) shows the dice tile (VRAM) + digit char
 * (buffer, harness-readable); '?' before the first roll. */
static void gamble_draw_dice(void)
{
    const uint8_t cols[3] = { 2, 8, 14 };
    const uint8_t top = 11;
    uint8_t b;
    uint8_t frame = UI_TILE_CARD_FRAME_BASE;
    volatile uint8_t *dst;
    char *buf;

    for (b = 0; b < 3; b++) {
        uint8_t x = cols[b];
        uint8_t d = g_gamble.dice[b];
        VBK_REG = 0;
        /* Top border. */
        dst = (volatile uint8_t *)(0x9800 + ((uint16_t)top << 5) + x);
        gamble_vram_sync_write(&dst[0], frame);
        gamble_vram_sync_write(&dst[1], (uint8_t)(frame + 1));
        gamble_vram_sync_write(&dst[2], (uint8_t)(frame + 2));
        /* Row 12: rails + center (dice tile or font '?'). */
        dst = (volatile uint8_t *)(0x9800 + ((uint16_t)(top + 1) << 5) + x);
        gamble_vram_sync_write(&dst[0], (uint8_t)(frame + 3));
        if (d >= 1 && d <= 6) {
            gamble_vram_sync_write(&dst[1], (uint8_t)(UI_TILE_DICE_BASE + d));
        } else {
            gamble_vram_sync_write(&dst[1], (uint8_t)(ui_font_tile_base + (uint8_t)('?' - ' ')));
        }
        gamble_vram_sync_write(&dst[2], (uint8_t)(frame + 5));
        /* Row 13: plain interior. */
        dst = (volatile uint8_t *)(0x9800 + ((uint16_t)(top + 2) << 5) + x);
        gamble_vram_sync_write(&dst[0], (uint8_t)(frame + 3));
        gamble_vram_sync_write(&dst[1], (uint8_t)(frame + 4));
        gamble_vram_sync_write(&dst[2], (uint8_t)(frame + 5));
        /* Bottom border. */
        dst = (volatile uint8_t *)(0x9800 + ((uint16_t)(top + 3) << 5) + x);
        gamble_vram_sync_write(&dst[0], (uint8_t)(frame + 6));
        gamble_vram_sync_write(&dst[1], (uint8_t)(frame + 7));
        gamble_vram_sync_write(&dst[2], (uint8_t)(frame + 8));
#ifdef DEBUG_BUILD
        g_tilemap_mirror[top * 32 + x] = frame;
        g_tilemap_mirror[top * 32 + x + 1] = (uint8_t)(frame + 1);
        g_tilemap_mirror[top * 32 + x + 2] = (uint8_t)(frame + 2);
        g_tilemap_mirror[(top + 1) * 32 + x] = (uint8_t)(frame + 3);
        if (d >= 1 && d <= 6) {
            g_tilemap_mirror[(top + 1) * 32 + x + 1] = (uint8_t)(UI_TILE_DICE_BASE + d);
        } else {
            g_tilemap_mirror[(top + 1) * 32 + x + 1] =
                (uint8_t)(ui_font_tile_base + (uint8_t)('?' - ' '));
        }
        g_tilemap_mirror[(top + 1) * 32 + x + 2] = (uint8_t)(frame + 5);
        g_tilemap_mirror[(top + 2) * 32 + x] = (uint8_t)(frame + 3);
        g_tilemap_mirror[(top + 2) * 32 + x + 1] = (uint8_t)(frame + 4);
        g_tilemap_mirror[(top + 2) * 32 + x + 2] = (uint8_t)(frame + 5);
        g_tilemap_mirror[(top + 3) * 32 + x] = (uint8_t)(frame + 6);
        g_tilemap_mirror[(top + 3) * 32 + x + 1] = (uint8_t)(frame + 7);
        g_tilemap_mirror[(top + 3) * 32 + x + 2] = (uint8_t)(frame + 8);
#endif
        /* Semantic buffer: frame corners as written by VRAM (harness
         * reads text, not art); the dice cell keeps the digit char. */
        buf = &g_ui_screen_buf[top][x];
        buf[0] = ' ';
        buf[1] = ' ';
        buf[2] = ' ';
        buf = &g_ui_screen_buf[top + 1][x];
        buf[0] = ' ';
        buf[1] = (d >= 1 && d <= 6) ? (char)('0' + d) : '?';
        buf[2] = ' ';
        buf = &g_ui_screen_buf[top + 2][x];
        buf[0] = ' ';
        buf[1] = ' ';
        buf[2] = ' ';
        buf = &g_ui_screen_buf[top + 3][x];
        buf[0] = ' ';
        buf[1] = ' ';
        buf[2] = ' ';
        /* Dice digit cells ride the wood slot (matches the dice tile
         * encoding); the box stays paper. */
        gamble_color_span(x, top, 3, UI_COLOR_NONE);
        gamble_color_span(x, (uint8_t)(top + 1), 1, UI_COLOR_NONE);
        gamble_color_span((uint8_t)(x + 1), (uint8_t)(top + 1), 1, UI_COLOR_WOOD);
        gamble_color_span((uint8_t)(x + 2), (uint8_t)(top + 1), 1, UI_COLOR_NONE);
        gamble_color_span(x, (uint8_t)(top + 2), 3, UI_COLOR_NONE);
        gamble_color_span(x, (uint8_t)(top + 3), 3, UI_COLOR_NONE);
    }
}

/* Row 16 controls.  The row is fully cleared first: RESULT text is
 * longer than CHOOSE text, so a shorter redraw would leave a stale
 * tail ("A:ROLL B:LEAVE" over "A:AGAIN B:LEAVE" read "LEAVEE"). */
static void gamble_draw_controls(void)
{
    gamble_draw_text_line(0, 16, 0, 20);
    if (g_gamble.phase == GAMBLE_PHASE_RESULT) {
        gamble_draw_text_line(0, 16, "A:AGAIN B:LEAVE", 15);
    } else {
        gamble_draw_text_line(0, 16, "A:ROLL B:LEAVE", 14);
    }
    gamble_color_span(0, 16, 20, UI_COLOR_NONE);
}

/* Full paint (entry): clear everything, then draw all rows. */
static void gamble_paint_full(void)
{
    uint8_t r, c;
    VBK_REG = 0;
    for (r = 0; r < 18; r++) {
        volatile uint8_t *dst = (volatile uint8_t *)(0x9800 + ((uint16_t)r << 5));
        char *buf = &g_ui_screen_buf[r][0];
        for (c = 0; c < 20; c++) {
            gamble_vram_sync_write(dst, ui_font_tile_base);
            *buf = ' ';
            dst++;
            buf++;
        }
        gamble_color_span(0, r, 20, UI_COLOR_NONE);
    }
    gamble_draw_banner();
    gamble_draw_kobold_row();
    gamble_draw_outcome();
    gamble_draw_gold_rows();
    gamble_draw_mid_row();
    gamble_draw_dice();
    gamble_draw_controls();
}

/* Dynamic refresh: banner + gold rows + mid row + dice + controls.
 * Kobold art is static (OAM, placed at entry and re-placed every
 * frame by gamble_oam_place). */
static void gamble_paint_dynamic(void)
{
    gamble_draw_banner();
    gamble_draw_outcome();
    gamble_draw_gold_rows();
    gamble_draw_mid_row();
    gamble_draw_dice();
    gamble_draw_controls();
}

/* ── Entry machinery (banked, LCD-off) ─────────────────────────────── */

/* Program the base BG CRAM slots 0-7 + the kobold OBJ scratch slot
 * from the codegen bundle (same bytes the battle pipeline programs).
 * Direct register writes: entry runs LCD-off (see gamble_entry). */
static void gamble_cram_load(void)
{
    uint8_t i;
    if (!g_is_cgb) return;
    BCPS_REG = 0x80;
    for (i = 0; i < 64; i++) {
        gamble_vram_sync_write(&BCPD_REG, g_gamble_base_slots[i]);
    }
    OCPS_REG = (uint8_t)(0x80 | (7 << 3));
    for (i = 0; i < 8; i++) {
        gamble_vram_sync_write(&OCPD_REG, g_gamble_kobold_ramp[i]);
    }
}

/* Stream the kobold art bytes to the battle-art block base (128).
 * Direct writes: entry runs LCD-off. */
static void gamble_art_stream(void)
{
    uint8_t i;
    volatile uint8_t *dst = (volatile uint8_t *)(0x8000u + ((uint16_t)128 << 4));
    for (i = 0; i < 96; i++) {
        dst[i] = g_gamble_kobold_tiles[i];
    }
}

/* Kobold OAM placement (shadow OAM entries 1-6): mirrors the bank-5
 * battle OAM stride for a 3-wide art slot centered on the boss screen
 * position (x=8, see screens/battle/boss.json) at the boss sprite row
 * (row 3).  Direct shadow writes; the main-loop commit reveals them. */
static void gamble_oam_place(void)
{
    uint8_t n = 0;
    uint8_t cx, cy, t;
    volatile uint8_t *e;

    t = 128;
    for (cy = 0; cy < 2; cy++) {
        for (cx = 0; cx < 3; cx++) {
            e = (volatile uint8_t *)(0xC000u + ((uint16_t)(1 + n) << 2));
            e[0] = (uint8_t)(((uint8_t)(3 + cy) << 3) + 16);
            e[1] = (uint8_t)(((uint8_t)(9 + cx) << 3) + 8);
            e[2] = t;
            e[3] = 7;
            t++;
            n++;
        }
    }
}

/* Dice tiles to UI_TILE_DICE_BASE (246-255).  The bytes live in this
 * bank (dice_tiles.h via dice_content.c is bank 8 too) -- plain call,
 * no trampoline. */
void dice_tiles_load_banked(void);

static void gamble_entry(void)
{
    /* LCD off for the whole transaction (same discipline as the battle
     * full redraw): direct writes land deterministically. */
    LCDC_REG &= (uint8_t)~0x80u;
    gamble_cram_load();
    gamble_art_stream();
    dice_tiles_load_banked();
    gamble_paint_full();
    gamble_oam_place();
    g_game.render_cache.valid = true;
    g_game.render_cache.prev_screen = SCREEN_GAMBLE;
    g_game.render_cache.prev_map_id = g_game.world.map_id;
    gamble_mark_seen();
    LCDC_REG |= 0x80u;
}

/* ── Update (banked) ───────────────────────────────────────────────── */

/* Leaving the table: stage Nis's farewell dialogue (g_bk_dlg_id) and let
 * the fixed post-tick hook in game_update() start it and switch
 * SCREEN_GAMBLE -> SCREEN_DIALOGUE.  Banked code can call neither
 * dialogue_start_def nor screen_change; the dialogue screen's first
 * render does the LCD-off world repaint (prev_screen != OVERWORLD) and
 * reloads the tileset.  Closing the box returns to the overworld through
 * the normal dialogue path. */
static void gamble_exit(void)
{
    g_bk_dlg_id = (uint8_t)DIALOGUE_ID_KOBOLD_FAREWELL;
    g_gamble_bye = 1;      /* audio tick: hand the scene track back */
    g_gamble.entered = 0;
}

static void gamble_update(void)
{
    uint8_t pressed;
    int16_t gold;
    uint8_t bet;

    pressed = (uint8_t)(g_inp_pad & (uint8_t)(~g_inp_prev));

    if (g_gamble.phase == GAMBLE_PHASE_RESULT) {
        if (pressed & (uint8_t)((1 << INPUT_A) | (1 << INPUT_START))) {
            /* Fresh hidden dice for the next round: the boxes show '?'
             * again instead of keeping the previous values. */
            g_gamble.phase = GAMBLE_PHASE_CHOOSE;
            g_gamble.dice[0] = 0;
            g_gamble.dice[1] = 0;
            g_gamble.dice[2] = 0;
            gamble_clamp_bet();
        } else if (pressed & (uint8_t)(1 << INPUT_B)) {
            gamble_exit();
        }
        return;
    }

    /* CHOOSE: field 0 = CHOOSE BET row, field 1 = CHOOSE NUMBER row. */
    if (pressed & (uint8_t)((1 << INPUT_UP) | (1 << INPUT_DOWN))) {
        g_gamble.field = (uint8_t)(g_gamble.field ^ 1);
        g_gamble_sfx = (uint8_t)(SFX_CURSOR + 1);
    } else if (pressed & (uint8_t)(1 << INPUT_LEFT)) {
        g_gamble_sfx = (uint8_t)(SFX_CURSOR + 1);
        if (g_gamble.field == 0) {
            g_gamble.bet_idx = (g_gamble.bet_idx == 0) ? (GAMBLE_BET_COUNT - 1) : (uint8_t)(g_gamble.bet_idx - 1);
            gamble_clamp_bet();
        } else {
            g_gamble.chosen = (g_gamble.chosen <= 1) ? 6 : (uint8_t)(g_gamble.chosen - 1);
        }
    } else if (pressed & (uint8_t)(1 << INPUT_RIGHT)) {
        g_gamble_sfx = (uint8_t)(SFX_CURSOR + 1);
        if (g_gamble.field == 0) {
            /* No % (it pulls the div library into fixed _CODE even
             * from a banked body, 52.18): idx cycles 0->1->2->0. */
            g_gamble.bet_idx = (g_gamble.bet_idx >= 2) ? 0 : (uint8_t)(g_gamble.bet_idx + 1);
            gamble_clamp_bet();
        } else {
            g_gamble.chosen = (g_gamble.chosen >= 6) ? 1 : (uint8_t)(g_gamble.chosen + 1);
        }
    } else if (pressed & (uint8_t)((1 << INPUT_A) | (1 << INPUT_START))) {
        bet = (g_gamble.bet_idx == 1) ? 5 : ((g_gamble.bet_idx == 2) ? 10 : 1);
        gold = g_game.state.currency.amount[CURRENCY_ID_GOLD - 1];
        if (gold >= (int16_t)bet && gold > 0) {
            g_gamble_sfx = (uint8_t)(SFX_SELECT + 1);
            gamble_do_roll();
        }
    } else if (pressed & (uint8_t)(1 << INPUT_B)) {
        gamble_exit();
    }
}

/* Enter the table (banked): replicates screen_change(g, SCREEN_GAMBLE)
 * with WRAM/register writes -- a non-overworld target hides the sprite
 * set and parks the camera, then the screen fields flip.  The entry
 * paint on the next frame fills OAM with Nis, so no refresh_OAM() here
 * (the main-loop commit does it once per frame). */
void gamble_enter_screen(void)
{
    uint8_t i;
    for (i = 0; i < 40; i++) {
        ((volatile uint8_t *)0xC000u)[(uint16_t)i << 2] = 0;
    }
    SCX_REG = 0;
    SCY_REG = 0;
    g_game.prev_screen = g_game.screen;
    g_game.screen = SCREEN_GAMBLE;
    g_game.render_cache.valid = false;
    g_gamble_enter = 0;
}

/* ── Audio tick (banked, called from game_frame_tick_banked) ─────────
 * Runs AFTER gamble_frame_banked and the shared jingle body, so it owns
 * the final g_bk_byte_b value for this frame:
 *   1 -> chest jingle (win), 4 -> casino theme, 2 -> scene-track
 * restore (left the table).  Codes 1 and 2 already have fixed consumers;
 * 4 is the one casino branch. */
void gamble_audio_tick(void)
{
    /* Table blips ride the shared one-shot SFX slot (byte_c == 2 with
     * the id in byte_d), which the fixed side already consumes.  Only
     * when nothing else claimed it this frame (the pursuit alert never
     * fires at the table, but stay defensive). */
    if (g_gamble_sfx != 0 && g_bk_byte_c == 0 && g_bk_byte_d == 0) {
        g_bk_byte_c = 2;
        g_bk_byte_d = (uint8_t)(g_gamble_sfx - 1);
        g_gamble_sfx = 0;
    }

    if (g_gamble_bye) {
        /* Leaving: restore the scene track now; the table is gone. */
        if (g_audio_current_track == MUSIC_CASINO) {
            g_bk_byte_b = 2;
        }
        return;
    }
    if (g_game.screen == SCREEN_GAMBLE) {
        if (g_gamble_jingle) {
            g_gamble_jingle = 0;
            g_bk_byte_b = 1;
            return;
        }
        if (g_bk_byte_b == 2) {
            /* The jingle's restore clock fired, but we are still at the
             * table: go straight to the casino theme instead of the
             * scene track (no stray one-frame town track). */
            g_bk_byte_b = 4;
            return;
        }
        if (g_audio_current_track != MUSIC_CASINO &&
            g_audio_current_track != MUSIC_CHEST) {
            g_bk_byte_b = 4;
        }
        return;
    }
    /* Left the table (B / farewell exit): hand the scene track back
     * through the existing restore code, no new fixed branch. */
    if (g_audio_current_track == MUSIC_CASINO) {
        g_bk_byte_b = 2;
    }
}

/* ── Single frame body (the only banked entry point) ───────────────── */

void gamble_frame_banked(void)
{
    uint8_t old_phase;

    if (!g_gamble.entered) {
        g_gamble.chosen = 1;
        g_gamble.bet_idx = 0;
        g_gamble.dice[0] = 0;
        g_gamble.dice[1] = 0;
        g_gamble.dice[2] = 0;
        g_gamble.phase = GAMBLE_PHASE_CHOOSE;
        g_gamble.field = 0;
        g_gamble.matches = 0;
        g_gamble.won = 0;
        g_gamble_sfx = 0;         /* no queued blip on entry */
        g_gamble_bye = 0;
        g_gamble_jingle = 0;
        g_gamble.entered = 1;
        gamble_entry();
        return;
    }
    old_phase = s_gprev_phase;
    gamble_update();
    if (!g_gamble.entered) {
        /* Exited to the overworld (banked screen_change replica): its
         * render path takes over from here (full redraw + OAM hide). */
        return;
    }
    if (g_gamble.phase != old_phase) {
        /* Phase switch changes the row content (outcome banner, carets,
         * dice, controls).  Every dynamic painter fully clears the rows
         * it draws (52.25 family), so the LCD-on repaint stays small --
         * a full paint here would be ~720 mode-gated writes and could
         * overrun VBlank. */
        gamble_paint_dynamic();
        gamble_mark_seen();
    } else if (gamble_changed()) {
        gamble_paint_dynamic();
        gamble_mark_seen();
    }
    gamble_oam_place();
}
