# Short Straight — Implemented (gem-gated)

A new poker tier for three cards in sequence (e.g. 4,5,6). Brainstormed
2026-10-22; **implemented 2026-10-23, multiplier locked at 130**.
Deviations from the proposal below: the tier is **gem-gated** (it only
classifies while the STRAIGHT gem is equipped -- see §Gem gate), and
**paired duplicates score 0** (one best-eligible card per run value).

## Gem gate

The tier lives behind the GEM tab: it only classifies while
`VARIABLE_ID_GEM_EQUIPPED` has the `GEM_STRAIGHT` bit (the STRAIGHT gem,
won at the chuck-a-luck table by leaving +20 net up in one sitting,
docs/chuck-a-luck.md; equipped manually on the GEM tab).  Unequipped,
the ladder is exactly the old one -- bare pairs and high cards are
unchanged (scenario `gem_combo_locked`).

State: owned/equipped are generic-variable bitmasks (no GameState /
snapshot / descriptor churn); the registry is `src/game/gems_content.c`
(bank 8).  Observability: the skill itself through `COMBO_RESOLVED`
(tier 10), the grant through `DIALOGUE_STARTED KOBOLD_PRIZE` + the owned
bit, the tab through `screen_row`.

## Rules (proposed)

* **Trigger**: the longest run of distinct sequential values among the
  effective cards has length ≥ 3. Works at any staged count ≥ 3 (like
  pairs work at any count ≥ 2, not just 5-card hands).
* **Participation (base)**: one best-eligible card per run value counts,
  everything else 0 -- the "total of the cards", consistent with pairs
  (only paired cards count) and HIGH CARD (max only).  For a run of 4,
  only the **highest three values** count (e.g. staged 1,2,3,4 scores
  2+3+4 = 9; the extra card is dead weight that still costs energy, so
  optimal play stages exactly the scoring three -- the live COMBO-row
  preview already guides this).  A paired duplicate shares its value but
  is NOT part of the run: staged 3,3,4,5 scores 3+4+5 = 12 (the second 3
  scores 0).  Eligibility is per-card as usual (phase/ring rules apply:
  e.g. a shield at a run value contributes 0 on attack).
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

(Status: implemented 2026-10-23, gem-gated, multiplier locked at 130 --
see "Implementation (done)" at the end.)

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

## Implementation (done 2026-10-23)

* `HAND_SHORT_STRAIGHT = 10` appended at the END of the `HandTier` enum
  (no renumber: `COMBO_RESOLVED` d0 carries tier ids and scenarios pin
  them).
* Multiplier `130` in `g_combo_tier_mult[11]`; no other table changes.
* Detection + participation live in ONE shared banked ladder used by both
  `combo_resolve_banked` and the HUD preview: longest run from the shared
  `s_hist` leftovers; top three values participate, one best-eligible
  card each (a paired duplicate scores 0). The n==5 straight check and
  the kind checks keep running first, unchanged. The gem gate is an
  inline expression in the ladder (never a helper call -- the ladder sits
  at the bottom of the deepest battle chain and every extra call edge
  eats the `make stack-budget` baseline). Callers re-run the best ring
  trial before participating-base so `s_hist` holds the classifying
  values.
* Fixed-bank budget: detection lives in bank 3; the fixed-side cost of
  the whole feature is a widened tab condition + target swap (~6 B; the
  GEM nav/render bodies forward to the quest bodies in-bank so the fixed
  dispatch needs no new branches). `make memmap` + `make stack-budget`
  stay green (release `_HOME` 4 B, debug bank 3 ~7 B headroom, Oct 2026).
* Tests: `gem_combo_short` (3-run + 130-vs-120 pin: 4,5,6 gives 19),
  `gem_combo_4run` (top-three: 1,2,3,4 gives 11),
  `gem_combo_pair_run` (pair+run: 3,3,4,5 gives 15, duplicate 0),
  `gem_combo_locked` (no gem: HIGH CARD unchanged), plus GEM-tab and
  grant scenarios.

## Open questions (all decided)

1. Multiplier locked at 130 (`gem_combo_short` pins 4,5,6 to 19).
2. 4-runs count the top three, not all four.
3. Base is one best-eligible card per run value; 3-card minimum, any
   staged count of 3 or more.
