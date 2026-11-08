#pragma bank 11

#include "economy.h"
#include "banked.h"
#include "game.h"
#include "game_ids.h"
#include "telemetry.h"
#include "telemetry_ring.h"
#include "scene_ids_generated.h"

/* ── Banked economy bodies (ROM bank 11) ─────────────────────────────
 * Hosts ALL economy arithmetic out of the fixed bank (which sits ~100 B
 * under 0x8000, AGENTS.md §52.18).  Self-contained per §52.11.1: WRAM
 * globals + same-bank table only, no fixed-bank calls, no di/ei, no
 * * / % (a bare 8-bit * pulls __mulsuchar into fixed _CODE, §52.18).
 * Telemetry is written to the ring directly: a banked body cannot
 * re-enter the trampoline to reach the bank-2 emit body, so the body
 * uses telemetry_ring.h (same seq/frame/head/count discipline as
 * telemetry_emit_banked; keep in sync). */

/* Six bytes, no padding: the save layout and snapshot offsets assume
 * it.  Fail the build if the toolchain ever pads the struct. */
typedef char economy_state_is_6[sizeof(EconomyState) == 6 ? 1 : -1];

/* Ring telemetry runs through telemetry_ring.h (debug/telemetry_ring.h):
 * one shared inline writer, same seq/frame/head/count discipline. */

/* Expected wallet per area (docs/economy.md §3.2): every entry clears the
 * floor (>= 1.5x the most expensive stocked card; the merchant's best is
 * the 30g bow, floor 45).  tools/check_economy.py mirrors these values
 * and fails loudly on floor violations -- keep in sync. */
static uint16_t economy_expected_for_scene(uint8_t scene)
{
    if (scene == SCENE_FOREST || scene == SCENE_GRASSY_FOREST ||
        scene == SCENE_VILLAGE_AREA || scene == SCENE_SOUTH_FIELD)
        return 300;
    if (scene == SCENE_MOUNTAIN_PASS || scene == SCENE_SOUTH_RIDGE ||
        scene == SCENE_DESOLATE_FIELD)
        return 500;
    if (scene == SCENE_CASTLE || scene == SCENE_CASTLE_ENTRY ||
        scene == SCENE_CASTLE_HALL || scene == SCENE_THRONE_ROOM ||
        scene == SCENE_UNDERGROUND_TUNNEL ||
        scene == SCENE_UNDERGROUND_TUNNEL_SOUTH ||
        scene == SCENE_DEMO_END_WAKE_SCENE)
        return 800;
    return 200;
}

/* Market-scaled quote S.  In: byte_a = price, byte_b = (uint8_t)bias,
 * byte_c = use_market.  Out: byte_b = S.  Same shape as the bank-2
 * display copy (screen_content.c s_sc_market_quote): display and charge can
 * never disagree, by construction.  use_market = 0 forces neutral 32
 * (debug/test paths needing exact figures; every live till price ---
 * buy quotes, loot appraisals, rarity cuts -- scales with the market,
 * §5).  Multiply is repeated addition
 * (UI-time paths only, never per-frame) so no mult lib is pulled into
 * fixed _CODE (§52.18).  Price 0 quotes 0 however biased (not for
 * sale); tools/check_economy.py keeps price-0 rows bias-free so the
 * guard never masks content. */
void economy_quote_banked(void)
{
    uint8_t price = g_bk_byte_a;
    int8_t bias = (int8_t)g_bk_byte_b;
    uint8_t use_market = g_bk_byte_c;
    uint8_t idx;
    uint16_t p;
    uint16_t q;
    uint8_t i;
    uint8_t b;
    if (price == 0) {
        g_bk_byte_b = 0;
        return;
    }
    if (bias >= 0) {
        b = (uint8_t)(price + bias);
        if (b < price) b = 255;
    } else {
        uint8_t neg = (uint8_t)(0 - bias);
        b = (neg >= price) ? 1 : (uint8_t)(price - neg);
    }
    if (use_market) {
        idx = g_game.state.economy.market_index;
        if (idx < ECONOMY_INDEX_MIN || idx > ECONOMY_INDEX_MAX)
            idx = ECONOMY_INDEX_NEUTRAL;
    } else {
        idx = ECONOMY_INDEX_NEUTRAL;
    }
    p = 0;
    for (i = idx; i != 0; i--) p = (uint16_t)(p + b);
    /* Saturate, never truncate (see s_sc_market_quote): base 255 at high
     * inflation exceeds 255 and must cap, not wrap. */
    q = (uint16_t)(((uint16_t)(p + 16)) >> 5);
    g_bk_byte_b = (q > 255) ? 255 : (uint8_t)q;
}

/* Victory gold path (docs/economy.md §3/§6).  In: byte_a = base reward,
 * byte_b = victory flag, byte_c = currency.  Out: byte_b = granted.
 * Ticks battles only on a real grant (victory with a nonzero base and a
 * valid currency): rewardless fights -- Carl's zero-gold spar, debug
 * no-op calls -- grant nothing AND tick nothing, so a spar plus shop
 * spend cannot move the market on gold_out alone (§6 guards).  Silent
 * no-grant (no tick) when victory is 0, base is 0, or the currency is
 * invalid. */
void economy_grant_banked(void)
{
    uint8_t base = g_bk_byte_a;
    uint8_t victory = g_bk_byte_b;
    uint8_t currency = g_bk_byte_c;
    int16_t wraw;
    uint16_t wallet;
    uint16_t expected;
    uint16_t e15;
    uint16_t e2;
    uint16_t e3;
    uint16_t t;
    uint8_t granted;
    int16_t cur;
    int16_t nv;
    uint16_t v;
    if (!victory || base == 0 || currency < 1 ||
        currency > MAX_CURRENCIES) {
        g_bk_byte_b = 0;
        return;
    }
    if (g_game.state.economy.battles < 255)
        g_game.state.economy.battles++;
    /* Wallet dampener (docs/economy.md §3.1): 1.5x / 2x / 3x ladder. */
    wraw = g_game.state.currency.amount[currency - 1];
    wallet = (wraw < 0) ? 0 : (uint16_t)wraw;
    expected = economy_expected_for_scene(g_game.state.scene.scene_id);
    e15 = (uint16_t)(expected + (expected >> 1));
    e2 = (uint16_t)(expected << 1);
    e3 = (uint16_t)(e2 + expected);
    if (wallet < e15) {
        granted = base;
    } else if (wallet < e2) {
        t = (uint16_t)((uint16_t)base + (uint16_t)((uint16_t)base << 1));
        granted = (uint8_t)((t + 2) >> 2);
    } else if (wallet < e3) {
        granted = (uint8_t)(((uint16_t)base + 1) >> 1);
    } else {
        granted = (uint8_t)(((uint16_t)base + 2) >> 2);
    }
    if (granted == 0) granted = 1;
    /* Saturating credit at 9999 (mirrors the currency_add clamp; the body
     * must not call the fixed-bank fn, §52.11.1).  cur <= 9999 and
     * granted <= 255, so no int16 overflow. */
    cur = g_game.state.currency.amount[currency - 1];
    if (cur < 0) cur = 0;
    nv = (int16_t)(cur + (int16_t)granted);
    if (nv > 9999) nv = 9999;
    g_game.state.currency.amount[currency - 1] = nv;
    /* Loot-only gold_in note (sales and event gold never touch it, §6.1). */
    v = (uint16_t)(g_game.state.economy.gold_in + granted);
    if (v < g_game.state.economy.gold_in) v = 0xFFFF;
    g_game.state.economy.gold_in = v;
#ifdef DEBUG_BUILD
    telemetry_ring_emit(EVENT_CURRENCY_ADDED, currency, granted, 0, 0);
#endif
    g_bk_byte_b = granted;
}

/* Shop-boundary market update (docs/economy.md §6).  No staging; reads
 * g_game directly.  Skips reset nothing.  Emits MARKET_UPDATED
 * (old, new, dir) on every non-skipped run. */
void economy_update_banked(void)
{
    uint8_t idx;
    uint8_t old;
    uint8_t dir = 0;
    uint16_t expected;
    uint16_t thresh;
    int16_t th;
    int16_t net;
    uint16_t gi;
    uint16_t go;
    uint16_t d;
    /* Shop-hopping guard: no counters, no update. */
    if (g_game.state.economy.gold_in == 0 &&
        g_game.state.economy.gold_out == 0)
        return;
    /* No combat since the last update: no update. */
    if (g_game.state.economy.battles == 0)
        return;
    old = g_game.state.economy.market_index;
    idx = old;
    if (idx < ECONOMY_INDEX_MIN || idx > ECONOMY_INDEX_MAX)
        idx = ECONOMY_INDEX_NEUTRAL;
    expected = economy_expected_for_scene(g_game.state.scene.scene_id);
    thresh = (uint16_t)(expected >> 2);
    if (thresh < 100) thresh = 100;
    th = (int16_t)thresh;
    gi = g_game.state.economy.gold_in;
    go = g_game.state.economy.gold_out;
    if (gi >= go) {
        d = (uint16_t)(gi - go);
        net = (d > 9000) ? 9000 : (int16_t)d;
    } else {
        d = (uint16_t)(go - gi);
        net = (d > 9000) ? -9000 : (int16_t)(0 - (int16_t)d);
    }
    if (net > th) {
        if (idx < ECONOMY_INDEX_MAX) idx++;
        dir = 1;
    } else if (net < (int16_t)(0 - th)) {
        if (idx > ECONOMY_INDEX_MIN) idx--;
        dir = 2;
    } else {
        if (idx < ECONOMY_INDEX_NEUTRAL) idx++;
        else if (idx > ECONOMY_INDEX_NEUTRAL) idx--;
        dir = 3;
    }
    g_game.state.economy.market_index = idx;
    g_game.state.economy.gold_in = 0;
    g_game.state.economy.gold_out = 0;
    g_game.state.economy.battles = 0;
#ifdef DEBUG_BUILD
    telemetry_ring_emit(EVENT_MARKET_UPDATED, old, idx, dir, 0);
#endif
}
