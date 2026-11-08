#ifndef GAME_ECONOMY_H
#define GAME_ECONOMY_H

#include <stdint.h>
#include "rpg/state.h"

/* Shared gold economy (docs/economy.md).  Fixed-bank budget discipline
 * (AGENTS.md §52.18: debug _CODE sits ~100 B under 0x8000): ALL economy
 * arithmetic lives in the bank-11 bodies (src/game/economy_banked.c).
 * Fixed-bank code pays only thin staging wrappers; bank-2 display bodies
 * carry their own local shift-add copies (a banked body must never call
 * a fixed-bank function, §52.11.1).  No float, no *, /, % anywhere on
 * these paths (§52.18).  Banked bodies that need telemetry write the
 * ring directly (they cannot re-enter the trampoline to reach the bank-2
 * emit body); the writer mirrors telemetry_emit_banked exactly. */

/* Market index in 1/32 units: neutral 32 = 100%, range 24-48 = 75%-150%. */
#define ECONOMY_INDEX_NEUTRAL 32
#define ECONOMY_INDEX_MIN     24
#define ECONOMY_INDEX_MAX     48

/* Market-scaled quote S (docs/economy.md §4-§5): base = price + bias
 * (8-bit saturating; price 0 quotes 0 = not for sale, however biased --
 * tools/check_economy.py keeps price-0 rows bias-free); S = (base *
 * index + 16) >> 5 with a repeated-add multiply (no lib, §52.18).  ONE
 * meaning for every till price: BUY rows quote S(price + bias, live
 * index), SELL rows quote S(sell_base, live index) where the sell base
 * is the loot formula value (loot has no buy row) or the rarity cut
 * of the BIASED base (curated: cut from price + bias, never the raw
 * price).  The cut never exceeds the biased buy base and S is
 * monotonic, so a same-visit buy-then-sell never prints gold (§5.1,
 * gated by tools/check_economy.py; cross-visit market drift is market
 * risk, not arbitrage).  Shops stock curated-only
 * (shops_compile.py rejects loot-range ids), so loot can never be
 * bought.  use_market nonzero selects the live clamped index (every
 * live till caller passes nonzero); zero pins neutral 32 (debug/test
 * paths needing exact figures).  At neutral S == base, so every
 * pre-economy expectation holds exactly. */
uint8_t economy_quote(uint8_t price, int8_t bias, uint8_t use_market);

/* Victory gold path (bank-11 body, inlined at the single world.c site):
 * dampens the base reward against wallet vs expected (100%/75%/50%/25%
 * ladder), credits it saturating at 9999, notes gold_in (loot only,
 * §6.1), ticks battles on a real grant only (rewardless spars tick
 * nothing), emits CURRENCY_ADDED (debug).  Silent no-grant (no tick)
 * when victory is 0, base is 0, or the currency is invalid.
 * Reads/writes g_game.state directly.
 * Shop-boundary market update (bank-11 body, inlined at the single shop
 * exit site): §6 guards (zero counters / zero battles skip silently);
 * emits MARKET_UPDATED (old, new, dir: 1 = up, 2 = down, 3 =
 * toward-100/stay) on every non-skipped run. */

/* Saturating shop-spend note (gold_out).  gold_in is noted only inside
 * the grant body -- sales and event gold never touch it (§6.1). */
void economy_note_spent(GameState *state, uint8_t amount);

/* Bank-11 bodies (src/game/economy_banked.c), dispatched above.  Staging:
 * quote: in byte_a = price, byte_b = (uint8_t)bias, byte_c = use_market,
 * out byte_b = S.  grant: in byte_a = base, byte_b = victory,
 * byte_c = currency, out byte_b = granted.  update: no staging; it reads
 * g_game directly. */
void economy_quote_banked(void);
void economy_grant_banked(void);
void economy_update_banked(void);

#endif /* GAME_ECONOMY_H */
