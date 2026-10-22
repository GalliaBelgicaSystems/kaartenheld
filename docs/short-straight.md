# Short Straight — Design Proposal (not yet implemented)

A new poker tier for three cards in sequence (e.g. 1,2,3). Brainstormed
2026-10-22; probability analysis below. **Status: proposal only, no ROM
changes.** Open decision: multiplier 130 (recommended) vs 120 fallback.

## Rules (proposed)

* **Trigger**: the longest run of distinct sequential values among the
  effective cards has length ≥ 3. Works at any staged count ≥ 3 (like
  pairs work at any count ≥ 2, not just 5-card hands).
* **Participation (base)**: the run cards count, everything else 0 --
  the "total of the cards", consistent with pairs (only paired cards
  count) and HIGH CARD (max only). For a run of 4, only the **highest
  three** count (e.g. staged 1,2,3,4 scores 2+3+4 = 9; the extra card
  is dead weight that still costs energy, so optimal play stages exactly
  the scoring three -- the live COMBO-row preview already guides this).
* **Precedence**: insert below THREE_KIND, above TWO_PAIR. A 5-card
  sequential hand stays STRAIGHT (210, checked first as today). Kinds
  keep precedence (a pair + run stages as the pair unless the player
  drops a card), so no existing classification changes: the tier only
  upgrades hands that are currently HIGH_CARD or pairs.
* **Suited bonus**: N/A in practice. Single-weapon values skip by two
  (swords/bows 1,3,5,7,9; daggers/shields/rings 1,2,4,6,8), so no
  three same-suit cards can ever be sequential. The existing +25% rule
  would apply automatically if a future band ever allows it -- no
  special-casing needed.
* **Rings (joker)**: free synergy. The existing best-tier trial loop
  (v = 1..10, keep best) completes runs automatically, e.g. SW4 + SW6
  + ring→5. No new code path.
* **Defend phase**: the classifier is phase-agnostic, but current shield
  bands (1,2,4,6,8) contain no 3-run, so the tier is attack-only in
  practice until bands change.
* **Energy gate**: staging 3 cards costs 3 energy vs a pair's 2. With
  pools of 2-3 early, the straight is often unaffordable -- a natural
  brake the raw probabilities below do not model (they assume free
  subset choice and are therefore an upper bound on strength).
* **Starter decks can never make one** (only 1s and 3s). The tier comes
  online with loot -- e.g. the field-cache poison dagger (bronze DA2)
  completes SW1 + DA2 + SW3. Nice progression beat; keep it.

## Balance analysis

Method: exact enumeration of all 5-card holdings (values i.i.d.
uniform 1..v, unsuited), best-subset-wins play (player stages the
highest-amount subset; extras deal 0). Reported: P(best tier is the new
one), EV uplift vs today, steal rate (holdings whose best tier changes
from pair/two-pair to short straight -- always upward, since the max
over a superset of options cannot drop).

| Deck model | Freq (M=120/130) | EV uplift @120 | EV uplift @130 |
|---|---|---|---|
| uniform 1..9 | 24% / 26% | +16% | +19% |
| uniform 1..6 | 30% / 31% | +13% | +15% |
| skewed mid-game* | 38% / 38% | +21% | +24% |

\* 20-card {1×6, 2×4, 3×4, 4×3, 5×2, 6×1}, Monte Carlo 40k.

Rarity context (uniform 1..9): PAIR 33%, HIGH_CARD 44% → 28%,
TWO_PAIR 13%, THREE_KIND 8%. A short straight (~26%) is about as common
as a pair and twice as common as two-pair: by the ladder's own
rarity ordering it belongs at or just above PAIR (120), well below
TWO_PAIR (150).

Why 130 over 120 or 100:
- 100/110 are invisible on small runs: 1+2+3=6 × 110% truncates to 6,
  the same no-bonus feel as today's pair-of-1s (base 2 × 145% → 2).
- 120 and 130 both give 7 for 1,2,3; they separate on mid runs
  (4,5,6: 18 vs 19; 7,8,9: 29 vs 31).
- The extra card + energy cost and the permanently-unsuited status
  justify a small premium over PAIR's 120.
- 140+ is too strong (+21-29% EV here; it starts outscoring dedicated
  mid pairs: 6,7,8 at 140% = 29 vs pair of 9s at 21).

Concrete damage at 130% (truncated): 1,2,3→7 · 2,3,4→11 · 3,4,5→15 ·
4,5,6→19 · 5,6,7→23 · 6,7,8→27 · 7,8,9→31. For reference: pair of 5s
(base 10) is 12 unsuited / 14 suited; three-kind of 5s is 27.

One known quirk (same class as flush-beats-full-house today): a 5-card
stage of two-pair + run (e.g. 2,3,3,4,4) classifies SHORT_STRAIGHT (11)
instead of TWO_PAIR (15). The preview shows the tier live and staging
4 cards keeps the 15, so optimal play routes around it -- but the
default 5-card read can surprise. Accept (precedent exists); alternative
is slotting the tier below TWO_PAIR, which strands pair+run hands on
PAIR instead (worse feel, since the run is the salient feature).

Enemy side: enemies play single cards, never combos, so the tier is
player-only -- same as every other tier.

## Worked examples (multiplier 130)

* Starter + cache: SW1 + DA2 + SW3 → base 6 → DMG 7.
* Mid game: SW4 + DA6? no -- 4,5,6 needs SW4 + IRON_SWORD? no: 4,5,6 =
  e.g. bronze dagger DA4? (dagger bands 1,2,4,6,8: DA4 exists) + SW5 +
  SW6? no SW6 (swords odd). E.g. DA4 + RG5? rings heal... attack example:
  SH4? shields don't attack. Say SW5 + DA4 + BRONZE? Keep it simple:
  3,4,5 = SW3 + DA4 + SW5 → base 12 → DMG 15.
* High end: 7,8,9 = steel sword + mythril dagger + mythril bow → base 24 → DMG 31.

## Implementation notes (for later)

* Append `HAND_SHORT_STRAIGHT = 10` at the END of the `HandTier` enum
  (do not renumber: `COMBO_RESOLVED` d0 carries tier ids and scenarios
  pin them). Update the "mirrors ranking" comment -- the ladder if-chain
  already evaluates rank position explicitly, so order in the enum is
  cosmetic.
* Add the multiplier to `g_combo_tier_mult[11]`; no other table changes.
* Detection + participation must live in ONE shared banked helper used
  by both `combo_resolve_banked` and the HUD preview (same contract as
  `combo_classify_loot` / `combo_participating_base`): longest run from
  the shared `s_hist` leftovers, top three values participate. The n==5
  straight check and the kind checks keep running first, unchanged.
* Tests to add: tier classification (3-run basic, 4-run top-three,
  pair+run precedence, 5-run still STRAIGHT, no-run HIGH CARD unchanged),
  preview == resolve agreement (DMG number), a `--check`-style deck seed
  where the cache dagger unlocks the first straight.
* Fixed-bank budget: detection lives in bank 3 with the rest; the
  `make memmap` + `make stack-budget` gates must stay green (debug fixed
  bank has ~34 B headroom as of Oct 2026).

## Open questions

1. Multiplier 130 vs the 120 fallback (recommendation: 130).
2. (Decided) 4-runs count the top three, not all four.
3. (Decided) Base = sum of the run cards; 3-card minimum, any staged
   count ≥ 3.
