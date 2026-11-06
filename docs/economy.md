# Economy Design Proposal

## 1. Goals

The economy should give gold a useful, ongoing purpose without becoming a second progression system that overwhelms the card game.

The goals are:

- Enemies are the main source of gold.
- The player can buy and sell cards, but only at shops.
- Card prices depend on AP cost and material.
- Prices fluctuate both upwards and downwards.
- Accumulating gold should not make the player effectively infinitely wealthy.

The proposed system deliberately avoids complicated economic simulation.

## 2. Economy loop

1. Defeat enemies and receive gold and card drops.
2. Keep useful cards.
3. Visit a shop.
4. Buy cards or sell unwanted cards.
5. Market prices adjust slightly.
6. Continue exploring and fighting.

Buying removes gold from the economy. Selling converts unwanted cards into spending money.

## 3. Enemy gold rewards

Each enemy has a base gold reward, optionally defined as a small range.

To prevent excessive wealth accumulation, enemy rewards decrease when the player's wallet exceeds an expected amount for the current area.

The expected wallet is a simple balancing constant defined per area.

Below the expected wallet, enemies pay their full reward. Above it, rewards gradually decrease, with a minimum of 25% of the normal reward.

This makes farming progressively less profitable without making enemies stop rewarding the player.

### 3.1 Integer loot factor (no float, no division)

The proposal draft used `wallet / expected_wallet` with float scaling. The ROM must not implement that literally: banked code cannot use `/`, `%`, `*` (generic multiply pulls `__mulint`/`__divuint` into the fixed bank, AGENTS.md §52.18) or `round()`.

Implement the dampener as threshold comparisons against small multiples of the expected wallet, all with 16-bit shifts/adds:

```text
if wallet < expected * 3 / 2:  factor = 100%
else if wallet < expected * 2:  factor = 75%
else if wallet < expected * 3:  factor = 50%
else:                           factor = 25%

gold_reward = max(1, (base_reward * factor + 50) / 100)
```

Only the final line needs a division, and it divides by the constant 100 on values that fit in a byte-times-byte product — implement it as the single 16x8 multiply in the unbanked shift-add helper (see §4.1), or as thresholded subtraction. Never call the SDCC division library from banked code.

Deterministic per seed: the factor is a pure function of `(wallet, expected_for_area)`; no RNG is involved.

### 3.2 Expected-wallet floor (saving must stay possible)

The wallet dampener punishes saving if the expected wallet for an area is below the price of the best card stocked there: a player saving up for it earns 25% income exactly when they need the money most.

Rule:

```text
expected_wallet(area) >= 1.5 * (most expensive stocked card in area)
```

Integer form (no float): `expected * 2 >= price_max * 3`.

Pin it with a host-side content check (not a ROM assert): the shop compiler or `make manifest` fails if any area's expected wallet is below the floor for the shops reachable from it. Add a scenario that saves toward the most expensive card in each stocked shop and asserts full (100%) rewards all the way up to `price_max` (see §10).

## 4. Card buy prices

Every card has a reference price derived from its AP cost and material.

```text
base_price = PRICE_TABLE[AP][material] + price_bias
buy_price  = (base_price * market_index + 16) >> 5
```

Notes:

- `PRICE_TABLE` is precomputed at compile time from the old `PRICE_PER_AP * (AP + 1) * material` ladder (PRICE_PER_AP = 10, Common 1.00 through Legendary 2.75). The ROM never computes the float ladder; it indexes the table.
- `market_index` is stored in 1/32 units: neutral = 32 (= 100%), range 24–48 (= 75%–150%). The `+16 >> 5` is rounding divide-by-32 with a shift, not a division instruction.
- `price_bias` is a per-card signed byte (see §4.1). It is part of `base_price`, so it scales with the market like everything else and cannot break the arbitrage invariant (§5.1).
- Curated shop rows may carry an authored `price` (as today: `CardDefinition.price` is the single source of truth for shop rows, loot synth fills it for derived ids). An authored price IS the `base_price` for that row; it must still satisfy the §5.1 assert against the market range.

Examples at a neutral market index (unchanged from the original ladder):

| Card | AP | Material | Price |
|---|---:|---|---:|
| Light card | 1 | Common | 20g |
| Heavy card | 3 | Common | 40g |
| Rare card | 2 | Rare | 48g |
| Expensive rare card | 5 | Rare | 96g |

Material should be represented by a small multiplier table.

If the game uses rarity instead of a separate material property, rarity can supply the multiplier. Avoid multiplying both unless there is a specific balancing reason.

### 4.1 AP is a weak proxy — add one bias byte per card

AP cost plus material underprices two cases: a cheap-but-strong card costs the same as a weak one at the same AP, and 0-AP card-advantage cards are underpriced at ~10g.

Each card row carries one extra signed byte, `price_bias` (gold, added to the table value before market scaling, result clamped to a minimum of 1). Cost: one byte per card definition. `0` means "table price is right". Authors use it to lift underpriced utility cards or to separate two cards that share an AP/material cell but not a power level.

The bias must be inside `base_price` (scaled by the market), never added after scaling — a post-scale add is a different buy/sell ratio per card and defeats the §5.1 invariant.

Host-side `make card-costs`-style check (or the shop compiler) should warn on two cards sharing an AP/material cell with identical prices but materially different win-rate/power, as a prompt to set the byte — the byte itself is authorial, not derived.

### 4.2 SM83 arithmetic rules for prices

- The one 16x8 multiply (`base_price * market_index`, max ~96*48 = 4608, fits in 13 bits) lives in ONE unbanked shift-add helper. Banked callers stage inputs through the `g_bk_*` globals and call it; they never emit `*` or `/` themselves (a bare `*` pulls `__mulsuchar` into fixed `_CODE`, AGENTS.md §52.18).
- No `round()`, no `* 1.15`, no `/ expected_wallet` anywhere in ROM code. Threshold compares (§3.1) and the `>> 5` shift above are the only allowed forms.
- `base_price` fits in a byte (max table value plus bias stays under ~120g); the product fits in 16 bits; `buy_price`/`sell_price` fit in a byte.

## 5. Selling cards

Selling lives in the shop's SELL tab (LEFT/RIGHT flips BUY/SELL,
docs/loot.md §34.6), served by the same banked row-quote body as
buying (`shop_row_quote_banked`): the row shows what the till pays,
by construction. Two sell bases, one quote function `S` (§4):

- Loot ids sell at their full synthesized formula value (loot doc
  §16/§34.6) — loot has no buy row, so the formula IS the appraisal.
- Curated cards sell on a rarity-scaled cut of their catalog price:
  `price × (5−tier)/6` (wood 5/6 … mythril 1/6), minimum 1 for any
  priced card. Commons sell near buy price, mythril for a fraction.

```text
sell_price = S(sell_base, market_index)   (every till price scales;
                                           at neutral this is the bare cut)
```

Shops stock curated cards only (`shops_compile.py` still rejects
loot-range ids), so loot can never be bought — and a bought curated
card can never be resold above its buy quote (see §5.1).

Rules:

- Selling is only possible on a shop's SELL tab.
- The card must actually belong to the player's collection.
- Quest/SPECIAL cards and price-0 basics cannot be sold (selling a
  free card would print money: buy 0, sell 1, rebuy 0, repeat).
- Fully decked cards refuse: selling must never strand the deck
  without ownership backing (decked copies are excluded first).
- Removing the card and crediting gold must be atomic: no gold is awarded if removal fails.
- Sale proceeds never touch `gold_in` (§6.1): a sale is conversion of
  an existing asset, not income.

### 5.1 No-arbitrage by construction

For every curated card the sell base never exceeds the buy base
(`cut = price × (5−tier)/6 ≤ price`, floored at 1), and the quote `S`
is monotonic non-decreasing in both arguments — so at EVERY market
index `S(cut, idx) ≤ S(price, idx)`: buy-then-sell always loses (or
washes at 1g). Loot has no buy row at all (shops stock curated
only), so no buy-low/sell-high loop exists in either direction. The
gates that protect this are:

1. `shops_compile.py` fails loudly on any loot-range id in shop stock.
2. `tools/check_economy.py` verifies the quote formula over the whole
   byte domain (neutral identity, saturation, monotonicity) AND the
   cross rule: for every price 1..255, every bias, every material
   tier, every index 24..48, `S(cut(price, tier), idx) ≤ S(base, idx)`.
3. Sell-all-then-rebuy scenarios run at neutral, where every figure is
   exact: `merchant_sell_loot` (1g formula value) and the shop buy
   scenarios pin both sides of the till.

## 6. Dynamic market prices

Use one global market index shared by all shops.

Represent it as an integer in 1/32 units (neutral 32 = 100%):

| Market condition | Percent | Stored index |
|---|---:|---:|
| Strong deflation | 75 | 24 |
| Neutral | 100 | 32 |
| Strong inflation | 150 | 48 |

Track:

- `gold_in`: gold received from enemy loot. ONLY enemy loot — see §6.1.
- `gold_out`: gold spent on shop purchases.
- `battles_since_update`: battles completed since the last market update (uint8, saturating at 255).

At a market update:

```text
if gold_in == 0 and gold_out == 0:  skip update (shop-hopping guard)
if battles_since_update == 0:       skip update (no combat happened)

net = clamp_to_int16(gold_in - gold_out)   /* u16 - u16 into int16 */

if net > threshold(area):
    market_index += 1                      /* +3.125% */
else if net < -threshold(area):
    market_index -= 1                      /* -3.125% */
else:
    move market_index 1 toward 32          /* -1 / +1 / stay */

market_index = clamp(market_index, 24, 48)

gold_in = 0
gold_out = 0
battles_since_update = 0
```

`gold_in`/`gold_out` use saturating adds (cap at 0xFFFF, never wrap); `net` is int16. Step size is 1 index point (3.125%) rather than 5 percentage points, so a single update cannot jump the price ladder — the old ±5 step conflated the percent representation with the stored unit.

Update the market at a clearly defined shop boundary, preferably when leaving a shop or before the next shop opens. Do not update prices every frame or every time the shop screen is redrawn.

### 6.1 What counts as income (deliberate decision)

Selling loot does NOT increment `gold_in`. `gold_in` counts enemy-loot gold only; `gold_out` counts shop purchases only. Card-sale proceeds are conversion of an existing asset into gold, not new income — counting them as income punishes selling with inflation, which is counterintuitive (sell loot → prices rise). If sale volume is ever needed for tuning, track it in a separate counter that the update reads but does not add to `net`; the default update ignores it.

### 6.2 Threshold: per-area, not 50 gold (change before playtesting)

The draft's fixed 50-gold threshold is the one number to change before playtesting. One card costs 20–96g and one enemy pays several gold, so almost every update with any combat in it exceeds ±50 and the index pins at 150 within a few shop visits; the "move toward 100" branch only fires on zero-counter visits (which §6's guard now skips anyway).

Replace the constant with a per-area threshold scaled to a fraction of the area's expected wallet:

```text
threshold(area) = max(100, expected_wallet(area) >> 2)   /* quarter wallet, floor 100g */
```

Integer only: a shift and a compare. The floor keeps early areas (small wallets) from updating on pocket change; the quarter-wallet scale keeps late areas (large wallets, expensive stock) from inflating on a single fight. Tune the `>> 2` and the `100` after the first playtest, not before — the shape (per-area, proportional) is what matters now.

### 6.3 Persistence and wire format

Market state is persistent economy state and belongs in the save:

- Append `market_index` (uint8, 1/32 units), `gold_in` (u16), `gold_out` (u16), `battles_since_update` (uint8) to `GameState` — a NEW trailing economy struct, never inserted between existing fields.
- Bump `SAVE_VERSION` (→ 5 with the 50-row collection); old saves are rejected per the existing policy (docs/save-format.md).
- Extend the extended snapshot (`g_state_snap_buf`) and the state-load descriptor (`g_scen_state_buf`) by APPENDING the same fields at the end with a version bump of their own. Never insert bytes in the middle — the host (`tools/emulator.py`) and ROM constants must stay in sync and every existing scenario depends on the current offsets (AGENTS.md §53.4).
- Emit a `MARKET_UPDATED` telemetry event (old index, new index, net sign) on every non-skipped update so the harness can assert market movement without reading SRAM.

## 7. Preventing runaway wealth

The proposal uses six safeguards:

1. Enemy rewards diminish when the player's wallet becomes unusually large (§3.1 threshold ladder, minimum 25%).
2. Selling is restricted to shops.
3. Resale never exceeds purchase: the rarity cut sits under the buy base and the quote is monotonic, so `S(cut) ≤ S(buy)` at every index (§5.1, checked by `make economy-check`).
4. Market fluctuations are bounded between 75% and 150% (stored 24–48).
5. Shops provide an ongoing gold sink through card purchases.
6. Gold saturates at 9999 (fits the 4-digit HUD; `currency_add` clamps rather than wrapping — a u16 wrap would turn a rich player poor and a poor display into a lie). `gold_in`/`gold_out` saturate independently for the same reason.

A hard wallet cap below 9999 should not be necessary initially.

Importantly, prices should not permanently increase with story progression. Area progression can introduce stronger enemies, better rewards, and more expensive card stock, but the market index itself must remain capable of moving in both directions.

## 8. Example

A Rare card costs 3 AP (table base 64g, bias 0):

At market index 115% (stored 37; `37/32 = 1.15625`), for a 2-material (iron) card:

```text
buy_price  = (64 * 37 + 16) >> 5
           = 2384 >> 5
           = 74 gold

sell_price = S(64 * 3/6 = 32, 37)
           = (32 * 37 + 16) >> 5
           = 1200 >> 5
           = 37 gold   (cut 32 ≤ base 64, so 37 ≤ 74 at every index)
```

If the market subsequently falls to 95% (stored 30):

```text
buy_price  = (64 * 30 + 16) >> 5
           = 1936 >> 5
           = 60 gold   (integer rounding vs the draft's 61; the shift form is authoritative)

sell_price = S(32, 30)
           = (32 * 30 + 16) >> 5
           = 976 >> 5
           = 30 gold   (still ≤ 60: no deflation pump)
```

The same card can therefore become more or less expensive depending on market conditions. The worked decimals (`64 * 1.15 = 73.6 → 74`) are commentary only — the ROM computes the shift form.

## 9. Implementation scope

Keep the initial implementation small:

1. Add base gold rewards to enemy definitions.
2. Implement the wallet-based diminishing reward threshold ladder (§3.1, integer only).
3. Implement one shared card-price calculation based on the precomputed `[AP][material]` table plus the `price_bias` byte (§4–§4.2, unbanked shift-add helper only).
4. Add shop-only card selling (atomic remove-then-credit, decked/quest guards).
5. Store a global market index in 1/32 units plus the three counters (§6).
6. Update the market at a consistent shop boundary with the zero-counter / zero-battle guards (§6).
7. Expose gold rewards, market index changes, purchases, and sales through debug telemetry and snapshots (`MARKET_UPDATED`, existing `CURRENCY_*`/`CARD_SOLD` events, appended snapshot/descriptor fields).
8. Persist the economy struct in the save with a version bump (§6.3).

Avoid individual market simulations per shop, simulated NPC demand, supply-chain mechanics, and time-based price ticking.

These would add unnecessary complexity.

SM83 constraints (all normative, not advisory):

- No float, no `round()`, no `* 1.15`, no `/ expected_wallet` in ROM code.
- No `*`/`/`/`%` anywhere on these paths — the quote multiply is
  repeated addition (`for (i = idx; i != 0; i--) p += b`), everything
  else is shifts, adds, and threshold compares.
- `S(base, idx) = (base * idx + 16) >> 5`, saturating (never wrapping):
  every till price (buy quotes, loot appraisals, rarity cuts) uses the
  live clamped index through the same function.
- Gold saturates at 9999; counters saturate; `net` is int16.
- New persistent fields go at the end of `GameState` + save version bump; new wire bytes are appended, never inserted.

## 10. Balancing and testing

Create deterministic scenarios to verify:

- Normal enemy rewards below the expected wallet.
- Reduced rewards above the expected wallet (75% / 50% / 25% bands at 1.5x / 2x / 3x).
- A minimum reward of 25% of normal.
- Zero-AP cards still have a positive price.
- Higher AP costs produce higher prices.
- Better materials produce higher prices.
- The `price_bias` byte raises a card's buy quote without changing its AP.
- Market index 48 quotes higher than 32; index 24 quotes lower than 32.
- All till prices scale with the index through one function `S` (buy, loot appraisal, rarity cut).
- Net income increases the market index.
- Net spending decreases the market index.
- The index never exceeds its bounds (24–48).
- The index gradually returns towards 32.
- Zero-counter shop visits do not move the index (shop-hopping guard).
- Zero-battle shop visits do not move the index.
- Expected wallet of every stocked area covers 1.5x its most expensive card (content check + save-toward-best-card scenario at full rewards).
- Card-sale gold does not inflate the index (sell-only visit leaves the index unchanged or moves it toward 32).
- Selling removes exactly one owned card and awards the correct gold.
- Selling cards in the active deck is rejected.
- Selling quest cards is rejected.
- Buy-then-sell always loses or washes: `S(cut) ≤ S(buy)` at every index (§5.1, `make economy-check`).
- Market determinism replay: from a saved snapshot, run the same shop-visit + battle sequence twice with the same seed and assert identical indices and gold (catches counter/RNG leakage between updates).
- Repeated combat does not produce unlimited full-value gold rewards.

## 11. Initial balancing questions

After implementation, playtesting should determine:

- Is 10 gold per AP appropriate?
- Is the loot sell value (formula value, market-scaled like everything else) sufficiently attractive?
- Does diminishing loot become noticeable too early?
- Is a 75–150 market range appropriate?
- Is the per-area `max(100, expected >> 2)` threshold (replacing the draft's flat 50 gold) too twitchy or too sticky?
- Does the `price_bias` byte get used, or does the AP/material table carry prices on its own?

Tune these constants after the first playtest before introducing additional mechanics. The threshold shape (per-area, proportional) ships now; its constants are tuned by playtest.

**The guiding principle: the player should always have a reason to spend gold, but never be able to trivialize the economy simply by repeating battles.**
