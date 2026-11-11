# Poker Hands — Available Tiers, Damage, and Bonus Calculation

Source of truth for scoring is the bank-3 ladder
(`src/battle/combo_content.c` → `combo_classify_loot` +
`combo_tier_from_hist`, multipliers in `g_combo_tier_mult`) and the
effect scaler (`src/rpg/effects_content.c` → `effect_resolve_into`).
Design background lives in `docs/combo-system.md` (§0.1 hand table)
and `docs/short-straight.md`.

## 1. Hand tiers

Poker-hands-only scoring: only the cards forming the hand count toward
the effect magnitude — every extra card deals/blocks 0.

| Tier | Enum | Requirement | Multiplier |
|------|------|-------------|------------|
| HIGH CARD | `HAND_NONE` (0) | fallback: fewer than 2 effective cards, or no pair/run | 100% |
| PAIR | `HAND_PAIR` (1) | ≥ 2 effective cards share one value | 120% |
| TWO PAIR | `HAND_TWO_PAIR` (2) | two distinct paired values | 150% |
| THREE KIND | `HAND_THREE_KIND` (3) | three cards share one value | 180% |
| STRAIGHT | `HAND_STRAIGHT` (4) | all 5 effective cards, 5 distinct sequential values | 210% |
| FLUSH | `HAND_FLUSH` (5) | all 5 effective cards, same symbol, not sequential, > 2 distinct values | 240% |
| FULL HOUSE | `HAND_FULL_HOUSE` (6) | trips + pair | 260% |
| FOUR KIND | `HAND_FOUR_KIND` (7) | four cards share one value | 280% |
| STRAIGHT FLUSH | `HAND_STRAIGHT_FLUSH` (8) | all 5, sequential + same symbol | 350% |
| FIVE KIND | `HAND_FIVE_KIND` (9) | five cards share one value (needs a ring/wildcard) | 400% |
| SHORT STRAIGHT | `HAND_SHORT_STRAIGHT` (10) | run of 3+ distinct sequential values at any staged count ≥ 3, **only while the STRAIGHT gem is equipped** | 130% |

Notes:

* Values classify order-independently (histogram over values 1–10).
* Pairs/kinds work at any staged count ≥ 2. STRAIGHT / FLUSH /
  STRAIGHT_FLUSH / FIVE KIND require all five effective cards.
* SHORT STRAIGHT slots below THREE KIND, above TWO PAIR in the ladder,
  but resolution is best-subset (see §3).
* Enemy side: enemies play single cards, never combos, so every tier
  above HIGH CARD is player-only.
* Rings (jokers): count as wildcards for the tier via the best-tier
  trial loop (values 1–10, keep best; rank-aware so SHORT never
  outranks real straights). An attack-phase ring converts the whole
  hand to healing (healed at the ring's substituted value);
  defend-phase rings add 0 block but enable the tier + over-block heal.
  Suitedness is unaffected by ring substitution (types untouched).

## 2. Short straight (gem-gated)

Full rules: `docs/short-straight.md`.

* **Gate:** only classifies while `VARIABLE_ID_GEM_EQUIPPED` has the
  `GEM_STRAIGHT` bit (STRAIGHT gem, won at the chuck-a-luck table,
  equipped on the GEM tab). Unequipped, the ladder is the old one
  (scenario `gem_combo_locked`).
* **Trigger:** longest run of distinct sequential values among the
  effective cards has length ≥ 3, at any staged count ≥ 3.
* **Participation:** one best-eligible card per run value counts,
  everything else 0. For a 4-run only the **highest three values**
  count (e.g. staged 1,2,3,4 scores 2+3+4 = 9). A paired duplicate
  shares its value but is NOT part of the run (staged 3,3,4,5 scores
  3+4+5 = 12, the second 3 scores 0). Phase/ring eligibility applies
  (attack: shields contribute 0; defend: only shields count, so the
  tier is attack-only in practice with current shield bands
  1,2,4,6,8).
* **Suited:** same +25 rule applies automatically. Practically N/A:
  single-weapon values skip by two (swords/bows 1,3,5,7,9;
  shields/rings 1,2,4,6,8), so no three same-suit cards can be
  sequential — except daggers (1,2,3,5,7): DA1+DA2+DA3 is a suited run.
* **Energy cost:** staging 3 cards costs 3 energy vs a pair's 2, and
  starter decks (only 1s and 3s) can never make one — it comes online
  with loot (e.g. SW1 + DA2 + SW3).

Best-subset resolution: classification picks the ladder tier, but
resolution plays the highest-scoring subset. A pair subset or lone high
card can beat the ladder's SHORT (3,3,4,4,5 resolves TWO_PAIR 21, not
SHORT 15; 1,2,3 + 9 resolves HIGH CARD 9, not SHORT 7), and a SHORT run
can beat the ladder's PAIR (3,3,4,5 resolves SHORT 15, not PAIR 7).
The live COMBO-row preview shows the winning play, so preview and
result agree. Ringless only (healing amounts are incomparable).

Concrete damage at 130% (truncated, unsuited):

```text
1,2,3 → base 6  → 7
2,3,4 → base 9  → 11
3,4,5 → base 12 → 15
4,5,6 → base 15 → 19
5,6,7 → base 18 → 23
6,7,8 → base 21 → 27
7,8,9 → base 24 → 31
```

Reference: pair of 5s (base 10) is 12 unsuited / 14 suited;
three-kind of 5s is 27.

## 3. How much damage (or block/heal)

Two steps, kept in separate modules by design
(`docs/combo-system.md` §5):

1. **Combo evaluator** → quality: `tier`, `multiplier`, `base_power`.
2. **Effect resolver** → magnitude: `amount = base_power × multiplier / 100`
   (integer arithmetic, truncated; shift-add/subtract loop in bank 3,
   no `*`/`/` operators).

### Base power (participation)

Only poker-hand cards count:

* HIGH CARD: single highest eligible card.
* PAIR / kinds: only the paired cards (a ringed kind uses the
  substituted value directly).
* Five-card hands (STRAIGHT/FLUSH/FULL HOUSE and up): all five.
* SHORT STRAIGHT: one best-eligible card per run value (see §2).
* Attack phase: shields contribute 0 to the base (fodder — still shape
  the hand); non-shields at face value; rings count 0 for damage
  (or at substituted value when the ring converts the hand to heal).
* Defend phase: only shields count; SW/BO/FI/HE are fully inert
  (0 block, 0 combo).

### Multiplier (tier + suited bonus)

```text
multiplier = g_combo_tier_mult[tier] + (suited ? 25 : 0)
```

Suited = all effective cards share one symbol **and** a tier was made.
Examples: PAIR suited = 145%, SHORT STRAIGHT suited = 155%,
FIVE KIND suited = 425%.

### Amount

```text
amount = base_power * multiplier / 100   (truncated toward zero)
```

The same scaling applies to DAMAGE, BLOCK, and HEAL — every effect
currently scales base_power identically (per-effect scaling is a
planned Phase C extension, not yet implemented).

Worked examples (attack, unsuited unless noted):

```text
SW2 + SW2               base 4   PAIR 120%        → 4
SW1 + SH2 + SW3         base 2   (shield 0)       → tier decides
SW2 + BO3               base 3   HIGH CARD 100%   → 3
SW1 + RING              base 2   PAIR 120%        → HEAL 2 (ring converts)
SW1 + DA2 + SW3         base 6   SHORT 130%       → 7
DA1 + DA2 + DA3 suited  base 6   SHORT 130% + 25  → 9
```

Shields alone in attack (base 0) resolve to 0 — the card is still
consumed.

## 4. Bonus damage calculation

"Bonus" is the multiplier on top of base power. Two sources, both
folded into the single `multiplier` field (`ComboResult.multiplier`):

1. **Tier percent** (`g_combo_tier_mult` table above). This is the main
   bonus: e.g. THREE KIND turns base 15 into 27 (×180%).
2. **Suited bonus: +25** when every effective card shares one symbol
   and a tier was made. E.g. pair of 5s: base 10 → 12 unsuited
   (×120%), 14 suited (×145%).

There is no other damage bonus. Status-rider chances scale with the
same multiplier (see §5); nothing else (buffs, debuffs, crits) feeds
into the amount today.

TELEMETRY: `COMBO_RESOLVED` carries tier/multiplier/base;
`EFFECT_RESOLVED` carries the scaled amount, so a test pins
`4,5,6 → SHORT 130% → 19` end to end (`gem_combo_short`).

## 5. Status-chance scaling (bonus to riders)

On-hit status riders (data on the card: `status_id/status_chance`,
e.g. POISON DAGGER 50%) scale with the hand's full multiplier, capped
at ~100%:

```text
scaled_chance = min(base_chance * multiplier / 100, 255)   (1/255 units)
```

Computed in `effect_scale_status_chance`, staged into `g_bk_byte_c`
for the fixed-bank caller. Example: 50% base × SHORT 130% → 65%;
the same card × STRAIGHT FLUSH suited (375%) would cap toward 100%.
Rider resolution itself (roll vs deterministic RNG, STACK/refresh,
telemetry `STATUS_APPLIED/RESISTED/EXPIRED`) lives in the status
system, never in the combo evaluator.
