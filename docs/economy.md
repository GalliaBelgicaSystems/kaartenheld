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

Suggested formula:

```text
wallet_ratio = player_gold / expected_wallet_for_area

loot_factor = clamp(
    1.0 - 0.25 * max(0, wallet_ratio - 1.0),
    0.25,
    1.0
)

gold_reward = max(1, round(base_reward * loot_factor))
```

The expected wallet is a simple balancing constant defined per area.

Below the expected wallet, enemies pay their full reward. Above it, rewards gradually decrease, with a minimum of 25% of the normal reward.

This makes farming progressively less profitable without making enemies stop rewarding the player.

## 4. Card buy prices

Every card has a reference price derived from its AP cost and material.

```text
base_price = PRICE_PER_AP * (AP_COST + 1) * material_multiplier

buy_price = round(base_price * market_index)
```

Suggested starting constants:

| Parameter | Value |
|---|---:|
| PRICE_PER_AP | 10 gold |
| Common material | 1.00 |
| Uncommon material | 1.25 |
| Rare material | 1.60 |
| Epic material | 2.10 |
| Legendary material | 2.75 |

The `+1` ensures that zero-AP cards still have a positive price.

Examples at a neutral market index:

| Card | AP | Material | Price |
|---|---:|---|---:|
| Light card | 1 | Common | 20g |
| Heavy card | 3 | Common | 40g |
| Rare card | 2 | Rare | 48g |
| Expensive rare card | 5 | Rare | 96g |

Material should be represented by a small multiplier table.

If the game uses rarity instead of a separate material property, rarity can supply the multiplier. Avoid multiplying both unless there is a specific balancing reason.

## 5. Selling cards

The player can sell owned cards at shops.

Suggested formula:

```text
sell_price = round(base_price * market_index * 0.50)
```

Cards therefore sell for 50% of their current shop purchase price.

Rules:

- Selling is only possible inside a shop.
- The card must actually belong to the player's collection.
- Quest and story cards cannot be sold.
- Cards in the active deck must first be removed from the deck.
- Removing the card and crediting gold must be atomic: no gold is awarded if removal fails.

The 50% resale value ensures that buying and immediately selling a card loses money.

Selling is therefore a way to recover some value from unwanted loot, not a way to generate infinite money.

## 6. Dynamic market prices

Use one global market index shared by all shops.

Represent it as an integer percentage:

| Market condition | Index |
|---|---:|
| Strong deflation | 75 |
| Neutral | 100 |
| Strong inflation | 150 |

Track two counters:

- `gold_in`: gold received from enemy loot and card sales.
- `gold_out`: gold spent on shop purchases.

At a market update:

```text
net = gold_in - gold_out

if net > MARKET_STEP_THRESHOLD:
    market_index += 5

else if net < -MARKET_STEP_THRESHOLD:
    market_index -= 5

else:
    move market_index 1 point toward 100

market_index = clamp(market_index, 75, 150)

gold_in = 0
gold_out = 0
```

Suggested threshold: 50 gold.

Interpretation:

- The player earns substantially more than they spend: prices increase.
- The player spends substantially more than they earn: prices decrease.
- The economy is relatively balanced: prices gradually return to normal.

This creates inflation and deflation without requiring a complex simulation.

Update the market at a clearly defined shop boundary, preferably when leaving a shop or before the next shop opens. Do not update prices every frame or every time the shop screen is redrawn.

## 7. Preventing runaway wealth

The proposal uses five safeguards:

1. Enemy rewards diminish when the player's wallet becomes unusually large.
2. Selling is restricted to shops.
3. Resale prices are only 50% of purchase prices.
4. Market fluctuations are bounded between 75% and 150%.
5. Shops provide an ongoing gold sink through card purchases.

Optional future gold sinks could include card identification, shop services, or modest service fees.

A hard wallet cap should not be necessary initially.

Importantly, prices should not permanently increase with story progression. Area progression can introduce stronger enemies, better rewards, and more expensive card stock, but the market index itself must remain capable of moving in both directions.

## 8. Example

A Rare card costs 3 AP:

```text
base_price = 10 * (3 + 1) * 1.60
           = 64 gold
```

At a market index of 115:

```text
buy_price  = round(64 * 1.15)
           = 74 gold

sell_price = round(64 * 1.15 * 0.50)
           = 37 gold
```

If the market subsequently falls to 95:

```text
buy_price  = round(64 * 0.95)
           = 61 gold

sell_price = round(64 * 0.95 * 0.50)
           = 30 gold
```

The same card can therefore become more or less expensive depending on market conditions.

## 9. Implementation scope

Keep the initial implementation small:

1. Add base gold rewards to enemy definitions.
2. Implement the wallet-based diminishing reward multiplier.
3. Implement one shared card-price calculation based on AP and material.
4. Add shop-only card selling.
5. Store a global market index and the two transaction counters.
6. Update the market at a consistent shop boundary.
7. Expose gold rewards, market index changes, purchases, and sales through debug telemetry and snapshots.

Avoid individual market simulations per shop, simulated NPC demand, supply-chain mechanics, and time-based price ticking.

These would add unnecessary complexity.

## 10. Balancing and testing

Create deterministic scenarios to verify:

- Normal enemy rewards below the expected wallet.
- Reduced rewards above the expected wallet.
- A minimum reward of 25% of normal.
- Zero-AP cards still have a positive price.
- Higher AP costs produce higher prices.
- Better materials produce higher prices.
- Market index 150 produces higher prices than 100.
- Market index 75 produces lower prices than 100.
- Net income increases the market index.
- Net spending decreases the market index.
- The index never exceeds its bounds.
- The index gradually returns towards 100.
- Selling removes exactly one owned card and awards the correct gold.
- Selling cards in the active deck is rejected.
- Selling quest cards is rejected.
- Buying and immediately selling a card loses gold.
- Repeated combat does not produce unlimited full-value gold rewards.

## 11. Initial balancing questions

After implementation, playtesting should determine:

- Is 10 gold per AP appropriate?
- Is 50% resale value sufficiently attractive?
- Does diminishing loot become noticeable too early?
- Is a 75–150 market range appropriate?
- Is a 50-gold market adjustment threshold appropriate?

Tune these constants before introducing additional mechanics.

**The guiding principle: the player should always have a reason to spend gold, but never be able to trivialize the economy simply by repeating battles.**
