# Card AP Cost — A Deterministic Formula (Proposal)

**Status:** accepted and implemented on this branch, 2026-11-03.  The
formula below is the ROM behavior (`CARD_AP` in `src/rpg/loot.h`, derived
catalog rows, loot gates, pool base 1 — the base-2 start from 3fe5c0d6 was
reverted the same day to restore the classic 1 → 5 ladder, so mythril
riders at 6 AP wait on a pool-6 source); the rollout plan in §8 is kept as
the record of how it landed.  `make card-costs` guards it.

Today a card's AP cost is either hand-picked (`src/game/cards_content.c`) or
one of three separate "loot costs 1" copies
(`src/rpg/loot_banked.c:101`, `src/battle/battle_init_content.c:67`,
`src/screens/item_content_banked.c:435`).  This document replaces all of
them with one function of the card's own immutable properties, so that any
future card — curated, looted, bought, or procedurally generated — has an AP
cost without anyone choosing a number.

---

## 0. The formula in one screen

```text
value = power  +  (material << 2)  +  MYTHRIL_PTS  +  (rider << 3)

AP    = (value + 7) >> 3           /* = ceil(value / 8); 1..6 by construction */
```

* `power` — the card's face value, meaning **damage dealt** (sword/bow/
  dagger), **damage blocked** (shield) or **HP restored** (ring).  1..9.
* `material` — 0..4: wood, bronze, iron, steel, mythril.
* `MYTHRIL_PTS` — 12 when `material` is mythril, 0 otherwise (§3.4).
* `rider` — 1 when the card carries an on-hit status (poison, burn, freeze),
  0 when plain.

In AP-step terms: **one AP step is worth 8 value points; a point of damage/
block/heal is 1, a material tier is 4, a mythril card pays a 12-point
premium on top, a rider is a whole step.**

Anchors:

```text
W SW   power 1, wood    -> 1 AP        (the brief's "very simple wood sword")
M BO   power 9, mythril -> 5 AP        (plain mythril is always 5, §3.4)
M F BO power 9, mythril + fire -> 6 AP (a mythril rider costs more than
                                       the same card plain)
```

No division, no multiplication, no table lookup.  The `1..6` range is a
property of the inputs rather than a clamp in the expression: the largest
legal value is `9 + 4·4 + 12 + 8 = 45`, which already yields 6, so the ROM
formula never needs a `min()`.  `CARD_AP_MAX` (6) is a *specification*
constant: `make card-costs` and the six AP pip tiles assert the table-built
range never exceeds it.  No runtime path clamps — the debug hand-injection
action stages an arbitrary byte cost deliberately (§6.2).

---

## 1. What AP is (and is not)

AP is the **energy pool**, `VARIABLE_ID_ENERGY_POOL` (`src/game/game_ids.h:101`),
refilled at battle start and at every transition into a decision phase —
attack *and* defend each get a fresh pool (`src/battle/battle.h:15-24`,
`docs/technical-manual.md` §6.2).  It starts at 1 and story grants raise it;
the grants in the demo (tutorial, field cache, Monster Hunt, mimic) top out
at 5.

So selection is a **budget**: `src/battle/battle_nav_banked.c:38` sums the
cost of every staged card and line 56 refuses one that would not fit.  AP
cost therefore answers exactly one question:

> How much of this phase's turn does this card consume?

It is **not** the card's gold value.  Price is a separate economy: sell
value is synthesized as `power + 2·tier` (+2 for a poison rider,
`docs/loot.md` §16/§34.6, stored in `CardDefinition.price`), while shop rows
carry authored prices (the Merchant sells the mythril bow for 30 g).  AP
buys *tempo*, price buys *economy*, and the two ladders disagree by design:
the bow costs 5 AP, and a loot roll of the same combo sells for 17 g.

And AP is **not** a rarity signal: rarity is the loot roll
(`docs/loot.md` §34.5).  Cost tracks what the card *does*, gate AP tracks
how likely it is to *drop*.

---

## 2. Why the current numbers cannot stay

| Rule | Where | Values |
|---|---|---|
| Authored per row | `src/game/cards_content.c` | one byte per row: 1, 1, 1, 1, 1, 0, 2, 1 (table order; §4.3 lists them per card) |
| Loot weapon table | `src/rpg/loot_banked.c:101` | all weapons 1 |
| Inline loot path | `src/battle/battle_init_content.c:67` | `(wpn <= WPN_RING) ? 1 : 0` |
| Screen path | `src/screens/item_content_banked.c:435` | same expression |
| Phantom/fallback card | `src/battle/deck_init.c:66`, `battle_turn_draw_banked.c:61` | 1 |

Six locations across six files, four distinct rules (authored per row; a
table of 1s; the same inline expression copied twice; the same fallback `1`
copied twice), one visible value.  Adding one card today means guessing a
byte in a table; adding one *material* means remembering to edit three
copies.  That is the whole problem this document solves: **the cost must be
a property of the card, not a decision by the author.**

---

## 3. Term by term

### 3.1 Power (weight 1) — what the card does per use

`power` is the only number the player already reads off the card (`SW5`,
`SH1`, `RG4`, `HE4`), and it is the same scale for every role
(`docs/loot.md` §34.1: "the same bands power attack, defense and healing"):

| Role | `power` means | Band values |
|---|---|---|
| Sword / bow | damage dealt | 1, 3, 5, 7, 9 |
| Dagger / shield / ring | poison damage / block / heal | 1, 2, 4, 6, 8 |

Swords and bows sit one point above their band, daggers/shields/rings at the
band floor — but under `ceil(/8)` that point never crosses a step boundary,
so role never changes the cost on its own (§7.3).

Reasoning for weight 1: damage is the unit the player is already budgeting
in, and every other term can be expressed as "how many damage points is this
worth".

### 3.2 Material (weight 4) — the band, and everything it implies

`material` is the §34.1 tier, 0..4.  It is worth **half an AP step** (4
points) because a tier buys more than damage:

* it carries a rider at all (wood never can, §34.1);
* it raises ring charges (`LOOT_RING_USES`, `src/rpg/loot.h`);
* it unlocks under a later AP gate, so a high-tier card is itself a reward;
* it scales the sell price, so a high-tier card is a liquid asset.

Note that power already rises with material for band-built cards, so a
material tier is really worth 2 (via power) + 4 = **6 points ≈ ¾ of an AP
step**.  That is intentional: doubling a card's damage must not double its
cost, or late cards become unplayable (see §7.1).

### 3.3 Rider (weight 8) — a full step, flat

Any status effect on the card — debuff or buff — costs **one whole AP**,
regardless of tier.  Today that means the on-hit statuses (poison, burn,
ice); tomorrow it means buffs too (`docs/combo-system.md` §14/§19 plans
buffs in the same status system).  Flat, not scaled: burn ticks a flat
10%-of-hit (min 1) and ice always greys exactly 2 cards, while poison
confuses for a flat 2 rounds -- the effect's *absolute* value is
constant whether it rides a p2 dagger or a p9 bow, so its *relative*
price must fall as power rises.
A percentage-of-card price would charge the most for the effect on the
biggest card, which is backwards.

`rider` is readable from the definition itself
(`status_id != STATUS_NONE`), so no new field is needed: both the loot
synth and the catalog already decide whether a card carries one.  Because
the macro keys off `status_id` and not off a named status, a future buff
status prices automatically the day it lands — no formula change, just a
new row in §4.2 and a new case in `make card-costs`.  The one property
fusion needs is strictness (§7.5): the step always lands.  It does, for all
twelve rider-capable pairs — bronze 1→2, iron 2→3, steel 3→4, mythril 5→6 —
verified cell by cell and asserted by `make card-costs` (§9).

### 3.4 Mythril premium (weight 12) — the top tier never discounts

Any mythril card pays a flat **12-point premium** (one AP step plus one
material tier) on top of its power + material + rider terms.  The premium
exists for one reason: **a mythril card must cost 5 AP minimum, always**.
Without it a mythril shield or ring (p8) would cost 3 — the same as a steel
sword — and a mythril sword or bow (p9) would cost 4, sitting *inside* the
steel band.  The top tier of the ladder must never price level with the
tier below it.  With the premium every plain mythril card lands on exactly
5 (`9 + 16 + 12 = 37 -> 5`; `8 + 16 + 12 = 36 -> 5`), and every mythril
rider lands on exactly 6 (`45 -> 6`, `44 -> 6`), so a mythril card with a
status always costs more than the same card plain — riders are never free
at the top of the ladder either.

The premium is deliberately flat rather than scaled: like the rider, it is
a statement about the tier, not about the card's face value.  If a future
tuning pass moves the mythril power band, the 5 AP floor moves with it only
if the band stays 8..9 — a mythril power below 5 would need a larger
premium, so treat 12 as coupled to the §34.1 band, exactly like weights 4
and 8 (§8.4).

Implementation is one ternary on the material (`(material == MAT_MYTHRIL)
? 12 : 0`): a compare and two shifts, no table, no fixed-bank symbol
(§8.1).  `MAT_MYTHRIL` already exists in `src/rpg/loot.h`.

### 3.5 What is deliberately *not* in the formula

| Property | Why not |
|---|---|
| `uses_per_battle` | A limiter, not a benefit.  Rings are the only heal in the game (`docs/loot.md` §34.3), so the scarcity of a 1-use wood ring is already paid by playing a 1–2 AP card that heals 1.  Discounting limited-use cards would make wood rings free. |
| `max_copies` | Deck-construction rule, not a per-turn cost. |
| `price` | Different economy (§1).  Tying AP to gold would make a sell-price tweak change playability. |
| `type` (ATK/DEF/HEL/…) | Fully implied by role + power; `SPECIAL` cards are the one carve-out (§5.3). |
| Combo tier | Cost is known before the hand is classified; the combo multiplier pays for *combining*, not for *holding* a card. |

---

## 4. Reference tables

Generated from the formula and the §34.1 bands.  Power is shown in
parentheses so the reader can check any cell by hand:
`AP = ceil((power + 4·material [+ 12 if mythril] [+ 8 if rider]) / 8)`.

### 4.1 Plain cards — material × weapon

| material | sword | shield | bow | dagger | ring |
|---|---|---|---|---|---|
| **wood** (gate AP 1) | 1 AP (p1) | 1 AP (p1) | 1 AP (p1) | 1 AP (p1) | 1 AP (p1) |
| **bronze** (gate AP 2) | 1 AP (p3) | 1 AP (p2) | 1 AP (p3) | 1 AP (p2) | 1 AP (p2) |
| **iron** (gate AP 4) | 2 AP (p5) | 2 AP (p4) | 2 AP (p5) | 2 AP (p4) | 2 AP (p4) |
| **steel** (gate AP 5) | 3 AP (p7) | 3 AP (p6) | 3 AP (p7) | 3 AP (p6) | 3 AP (p6) |
| **mythril** (gate AP 5) | 5 AP (p9) | 5 AP (p8) | 5 AP (p9) | 5 AP (p8) | 5 AP (p8) |

"gate AP" is the AP below which that material cannot drop
(`LOOT_AP_BRONZE` / `LOOT_AP_IRON` / `LOOT_AP_STEEL`, `src/rpg/loot.h`;
mythril shares the `LOOT_AP_STEEL` = 5 gate, there is no separate mythril
constant, `docs/loot.md` §34.5).  The column is there to be checked: see
§6.1.  Plain mythril sits exactly on its gate (5 ≤ 5); mythril *riders*
need pool 6 and are gated separately (§6.1).

### 4.2 Rider cards — the only legal (effect, weapon) pairs

| material | F BO (burn) | I BO (freeze) | P DA (poison) |
|---|---|---|---|
| **wood** (gate AP 1) | n/a | n/a | n/a |
| **bronze** (gate AP 2) | 2 AP (p3) | 2 AP (p3) | 2 AP (p2) |
| **iron** (gate AP 4) | 3 AP (p5) | 3 AP (p5) | 3 AP (p4) |
| **steel** (gate AP 5) | 4 AP (p7) | 4 AP (p7) | 4 AP (p6) |
| **mythril** (gate AP 5, riders 6) | 6 AP (p9) | 6 AP (p9) | 6 AP (p8) |

Wood has no row by design (wood is always plain, §34.1) — and that is lucky:
a wood+rider card would be the only combination in the whole table that
costs more AP than the AP that unlocks its material (§6.1).  Mythril riders
are the second exception: they cost 6 against a material gate of 5, so the
generator must not roll them until the pool reaches 6
(`LOOT_AP_MYTHRIL_RIDER`, §6.1) — same shape as the wood rule, one extra
comparison.

### 4.3 Catalog cards — before and after

| Card | Row | power / material / rider | Formula | Today |
|---|---|---|---|---|
| Wood Sword | `W SW` | 1 / wood / — | **1** | 1 |
| Wooden Shield | `W SH` | 1 / wood / — | **1** | 1 |
| Bronze Sword | `B SW` | 3 / bronze / — | **1** | — (new plain row; the slot was a 2-AP fire rider) |
| Poison Dagger | `P DA` | 2 / bronze / poison | **2** | 1 |
| Iron Sword | `I SW` | 5 / iron / — | **2** | 1 |
| Iron Ring | `I RG` | 4 / iron / — | **2** | 1 |
| Mythril Bow | `M BO` | 9 / mythril / — | **5** | 2 |
| Steel Shield | `S SH` | 6 / steel / — | **3** | — (new, docs/enemies_ranking.md §5.2: the merchant-stocked answer to the Lord's flat-6 swing) |
| Shop Keys | `SHOPKEYS` | SPECIAL, not playable | **0** | 0 |
| Bone | `BONE` | SPECIAL, not playable | **0** | 0 |
| Old Key | `OLD KEY` | SPECIAL, not playable | **0** | 0 |

Every curated row happens to be band-built, which is why the formula
reproduces all of them without a single exception to the band rule.

---

## 5. Where the material comes from

Three sources, no new state:

1. **Loot cards** — `loot_id_material(id)` (`src/rpg/loot.h`), pure masks.
2. **Catalog cards** — written as `MAT_*` literals in the row
   (`CARD_AP(5, MAT_IRON, 0)`), so the number in
   `src/game/cards_content.c` is computed, not typed.
3. **Future cards** — the same literal.  A card whose power is off-band
   keeps working because material is named, not inferred.

`CardDefinition.cost` stays a byte and keeps its meaning; only its *value*
becomes derived.  This matters: adding a `material` field would grow
`CardDefinition`, shift `g_card_scratch` in WRAM by a byte, and force a
re-run of `make stack-budget` plus the sentinels (AGENTS.md §52.24).  A
snapshot-neutral design avoids that entirely.

### 5.1 Can material be inferred from power?

For band-built cards, yes: `power = 2·material + (sword|bow ? 1 : 0)`, so
`material = power >> 1`.  That is tempting (zero data, zero call-site
changes) and is how the equivalence can be *checked*, but it must not become
the implementation: it silently couples the cost to the band table, so the
first off-band card (say a curated p6 sword) would be priced as steel.
Explicit `MAT_*` is one byte in ROM and self-documenting.

### 5.2 Complexity on a Game Boy

`(power + (mat << 2) + (rider << 3) + (mat == MAT_MYTHRIL ? 12 : 0) + 7) >> 3`
is shifts, adds and one compare only — no `/`, no `%` (AGENTS.md §52.18:
those pull the SDCC div/mod library into the fixed bank), and 8-bit
multiply is avoided too.  In the catalog the whole expression folds to a
constant at compile time; at runtime (loot synth, the inline battle path,
the card list) it is a handful of bytes and it removes a per-weapon column
(`s_weapons[].cost`) that had to be kept in step with the ladder.  Power
and material still come from that same table -- the win is one fewer source
of truth for the cost, not a saved lookup.

### 5.3 SPECIAL cards

`CARD_TYPE_SPECIAL` (quest items such as the Shop Keys) are not battle cards,
are not deckable, and keep cost 0 as an explicit carve-out: the formula
requires `power >= 1`.  Cost 0 should stay reserved for "not playable", never
for "free card" — the budget check and the combo economy both assume a
staged card consumes at least 1 AP.

---

## 6. Guarantees

The formula is not just monotone; it has four properties worth testing.

### 6.1 Every card is affordable at the AP that unlocks it

```text
cost(material, weapon, effect) <= gate_ap(material, effect)   for every legal card
```

Verified cell by cell against §4.1/§4.2: wood ≤ 1, bronze ≤ 2, iron ≤ 3 ≤ 4,
steel ≤ 4 ≤ 5, plain mythril = 5 ≤ 5.  Two combinations need more AP than
their material gate, and both are gated separately rather than allowed to
drop unplayable:

* wood+riders (would cost 2 at gate 1), which the generator and the
  synthesiser both forbid (§34.1, `rider_legal()`);
* mythril+riders (cost 6 at material gate 5): the generator rolls them
  freely now that drops are rank-decided (docs/enemies_ranking.md §4.3) --
  the old `LOOT_AP_MYTHRIL_RIDER` hold-back is gone, and the equip gate
  (`deck_equip_card`, §4.8) covers it instead: a 6-AP card cannot be
  decked until the pool reaches 6, so the locked card sits in the
  collection as a visible "come back with AP 6" marker.  The synth's
  `has_rider` strip stays the last line of defense for hand-encoded ids.

Coherence now comes from the equip gate, not the generator: a card that
can drop is a card you can *own*, and the gate names the price of wearing
it (`NEED AP n`) instead of hiding it.  Any future tuning knob must
preserve that (§8.4).

### 6.2 The ceiling is 6, and only mythril riders live there

Max legal input: `9 + 4·4 + 12 + 8 = 45 -> 6`, and the largest
intermediate, `45 + 7 = 52`, still fits a byte.  Plain mythril sits
exactly on the pool's top rung (5); mythril riders cost 6, which the grant
ladder no longer reaches (base 1 + tutorial, chest, Monster Hunt, mimic
tops out at 5 — the base-2 start was reverted to restore the classic
1 → 5 progression).  No 6-cost card can be played today, and until the
pool reaches 6 the generator simply never rolls one (§6.1), so no 6-cost
card can drop that the pool cannot play.  A fifth +1 source (or a pool-6
source, §7.1) is required before mythril riders can enter play.

That is why the expression carries no `min()`: the ceiling is a consequence
of the inputs, not a clamp.  `CARD_AP_MAX` is the compile-time assertion of
that ceiling — `make card-costs` recomputes every combo against it, and the
AP pip art covers exactly 1..`CARD_AP_MAX`.  Nothing clamps at runtime; it
has no `.c` call sites by design.

### 6.3 Monotone in every axis

More power, higher material, or an added rider never lowers the cost
(all four terms are non-negative and the map `x -> ceil(x/8)` is monotone —
the mythril premium is a step up from steel, never a discount).
That is the property that lets designers *reason* about cards without
consulting a table: "strictly better card, never cheaper".

Read backwards, it is the property fusion needs (§7.5): "cheaper card,
weaker card".  The contrapositive holds along every comparable line — more
power, higher material, or an added effect never lowers the cost, so a card
that costs less is never the stronger card.  Equal costs are allowed (a
steel sword and a steel shield both cost 3); strict inversions are not, and
there are none in §4.1/§4.2.  Stronger still, the rider step is strict, not
just non-decreasing: adding an effect to any rider-capable card always costs
exactly one AP more than the same card plain (§3.3).  `make card-costs`
asserts both — pairwise no-inversion over all 37 combos and strictness for
all twelve rider pairs — so a future card or band edit that inverts the
ladder fails the gate before it reaches a scenario.

### 6.4 Deterministic and save-neutral

The cost is a pure function of immutable definition bytes: no RNG, no game
state, no frame timing, no build-order dependence.  The same card costs the
same in the release ROM, the debug ROM, the harness, and after a save/load
roundtrip.  Nothing new enters `GameState`; `BattleCard.cost`
(`src/battle/card.h`) stays the runtime copy it already is.

---

## 7. Design consequences

### 7.1 A 5-AP card is a whole turn, a 6-AP card is the endgame

With a mid-demo pool, a plain mythril card can never be combined with
anything, and a mythril rider (6 AP) cannot be played at all — it is a card
for a pool-6 source that does not exist yet (the completed grant ladder,
base 1 + all four +1s, tops out at 5).  Meanwhile five 1-AP
cards can form a straight (210%) or five of a kind (400%,
`docs/combo-system.md`), so raw damage-per-AP favours combos.  The
expensive card is therefore an *option*, not an upgrade: it buys certainty
(a single card always "combos"), a rider, and the highest single value on
the table.

The pool grows with the ladder by construction (`VARIABLE_ID_ENERGY_POOL`
is uncapped and the four story grants take it 1 → 5), so plain mythril
stays playable at full completion without any separate pool-growth
feature: a 5-cost mythril card owns the whole phase once the pool allows.
Mythril riders (6) wait on a pool-6 source (§7.1, §8 Q4).  Recommended:
keep average cost per staged card near 2, so a 3–4 card combo stays
affordable at every stage of the
game.

### 7.2 The floor is 1, the classic start

The starter deck (`screens/hero.json`) is four wood swords, four wood
shields and **two plain bronze swords**.  Under this formula every
starter card costs 1, so at the hero's pool of 1 (`src/game/content.c`)
the whole deck is live on turn one — the manual promises "You get
**1 Energy** each round" (`docs/manual.md:167`).  (Before the rider
move the two bronze slots were fire swords at 2 AP, which came online
with the first +1 grant (the field-cache dagger chest, pool 2): start
1, dagger 2, tutorial 3, Monster Hunt 4, mimic 5.  The plain bronze
sword collapses that beat — the first grant now buys combo width
instead of unlocking the swords.)

### 7.3 Block and heal match damage, point for point

Block (p2/4/6/8) and heal (p2/4/6/8) sit one band below damage (p1/3/5/7/9) —
but under `ceil(/8)` that one point never crosses a step boundary, so a
shield costs exactly what the same-material sword costs at every tier
(bronze 1, iron 2, steel 3, mythril 5), rider or plain.  The band gap is
real in *effect* (a mythril ring heals 8 where a mythril sword deals 9) and
in *price*, but it buys no tempo discount: blocking only pays when the
enemy attacks and a ring's heal is limited to `material + 1` charges, yet
both pay full damage-rate AP.  If either ever feels too dear for its
conditional payoff, the fix is the band, not the formula — but note the
ceiling absorbs single-point moves silently, so a band tweak that should
move an AP cell needs checking against §4.1, not assuming.

### 7.4 Display work follows automatically

The battle hand shows AP as OAM pips: `AP_TILE_1/2` (OAM tiles 96/97,
`screens/ap_icons.json` → `src/game/ap_tiles_generated.h`) and a two-branch
chain in `src/battle/battle_oam_banked.c:236`.  Costs of 3–5 will appear the
moment the formula lands, so the pip chain has to grow; cost 6 arrives later
with the first pool-6 source (§7.1) and needs a sixth pip cell the shared
sheet does not have yet (`one`–`five ap` only).  The art already
exists on the shared sheet (`one/two/three/four/five ap` cells, also listed
in `tools/level_editor/tilesets/actors.json`), but only cells 1 and 2 are
wired; 3–5 need tile slots from the sprite budget and a lookup instead of
the if-chain.  Cost 6 has no pip: no sixth cell exists on the shared
sheet and no free OAM id sits below the BG-art aliasing, so a staged 6-cost
card (full-completion pool + a mythril rider drop + the draw) shows no pip
— the DECK-tab BG digit still shows its cost.  The sixth pip is art + sprite-
budget work for when post-completion battles make it matter.

### 7.5 Cheap cards are fuel: sell or fuse

A weak card is not a dead card.  It has four futures: played for tempo (a
1-AP wood sword still swings every turn, and five 1-AP cards form a straight
or five of a kind, §7.1), sold for gold (§1: a loot roll sells for `power +
2·tier`), fused into a powerful card (future mechanic: combine cheap weak
cards into one strong card), and — once buffs land — carried for its effect
rather than its face value (§3.3).  The formula's job is to make the fusion
future safe without knowing its recipes:

1. **Ordering.**  Fusion trades up in stats, so it must trade up in cost.
   §6.3 guarantees it: any recipe whose output gains power, material, or an
   effect over its inputs gains AP over them too.  The fusion system can
   therefore price its recipes in *cards* (how many inputs per output)
   without ever re-checking tempo — the output's cost is derived from its
   own stats, so fusing into something underpriced for its power is
   unrepresentable, the same way a mispriced catalog row is (§10,
   last row).  A useful rung on that ladder: a rider at tier *m* costs the
   same as plain at tier *m+1* (bronze rider 2 = iron plain 2, iron rider
   3 = steel plain 3), so tier-by-tier fusion trades equal tempo until
   mythril, where the premium charges one extra AP (steel rider 4 vs plain
   mythril 5) — the top tier is never free.
2. **No knowledge.**  The formula never sees recipes and recipes never set
   costs.  The fusion exchange rate (how many weak cards one strong card
   costs) is the fusion system's dial, tuned by playtest, not by AP
   weights — §8.4 stays closed.
3. **Effects always cost.**  A fused poison dagger costs strictly more than
   a fused plain dagger (§3.3 strictness), so "add an effect" is never a
   free upgrade on either side of the fusion.
4. **Sell and fuse agree on order.**  Sell value (`power + 2·tier`) and AP
   cost are both monotone non-decreasing in power and material, so the two
   sinks rank cards the same way even though they price them differently
   (§1).  No card is simultaneously "trash to sell, treasure to fuse" in a
   way that breaks either sink: the fuse-or-sell decision is a real
   economic choice, not an arbitrage.

What the formula does *not* do is conserve AP across a fusion: three 1-AP
cards fusing into one 2-AP card is quantity becoming quality, and that is
the point.  AP is per-turn tempo, cards are stock; fusion converts stock
into tempo efficiency, and the output pays the correct tempo price for what
it does.  The DECK tab already prints the cost as a BG digit
(`docs/deck-management.md`: "AP cost digit at cols 3-4"), so it needs
nothing.

---

## 8. Implementation plan

### 8.1 Add the formula

`src/rpg/loot.h`, next to `LOOT_POWER` — same arc, same constants, and
`MAT_*` already lives there (game layer including an engine header is the
allowed direction, AGENTS.md §55.1; a separate `card_ap.h` would have to
import `loot.h` for the material enum anyway, so it would only add a file):

```c
/* docs/card-ap-cost-formula.md: one AP step = 8 value points. */
#define CARD_AP_MATERIAL_PTS 4
#define CARD_AP_RIDER_PTS    8
#define CARD_AP_MYTHRIL_PTS  12  /* §3.4 premium: mythril floor of 5 AP */
#define CARD_AP_MAX          6   /* spec ceiling, asserted by card-costs */

#define CARD_AP_VALUE(power, material, rider)                    \
    ((uint8_t)((power) + ((material) << 2) + ((rider) << 3) +    \
               (((material) == MAT_MYTHRIL) ? CARD_AP_MYTHRIL_PTS : 0)))

#define CARD_AP(power, material, rider)                          \
    ((uint8_t)((CARD_AP_VALUE(power, material, rider) + 7) >> 3))
```

Everything fits a byte — the largest value is 45 and the largest
intermediate 52 — so the `uint8_t` types are exact and a 16-bit widen would
buy nothing.  (SDCC still does the adds in `int`, which C89 promotion makes
unavoidable, but no value leaves `0..52` and the truncated result is
provably correct.  The `== MAT_MYTHRIL` ternary compiles to a compare and
two shifts — no table, no division.)  `CARD_AP_MAX` never appears in C: it
is a specification constant asserted by `make card-costs` (every table-built
combo <= 6) and mirrored by the six pip tiles, not a runtime clamp.  The
debug hand-injection action (`DBG_ACT_SET_HAND_CARD_META`) stages whatever
cost byte a scenario asks for, including out-of-range ones, deliberately.

All three runtime call sites are already banked files
(`src/battle/battle_init_content.c` and `src/screens/item_content_banked.c`
are bank 2, `src/rpg/loot_banked.c` is bank 8), so they expand the macro
inline from their own bank-local table values.  That is deliberate: a
`card_ap()` helper in fixed-bank `src/rpg/loot.c` would spend `_CODE` bytes
on a shifts-and-compare expression, and `_CODE` is the budget that corrupts
bank 2 when it overflows (AGENTS.md §52.18/§55.5).  No new fixed-bank
symbol, no new banked body, no trampoline.

### 8.2 Collapse the duplicated rules

| File | Change |
|---|---|
| `src/rpg/loot.h` | add `LOOT_AP_MYTHRIL_RIDER 6` next to the `LOOT_AP_*` gates |
| `src/game/loot_drop_banked.c` | suppress the rider roll on mythril while the pool is below the new gate (§6.1) |
| `src/rpg/loot_banked.c` | delete `s_weapons[].cost` (the column) and compute `CARD_AP(power, material, status_id != STATUS_NONE)` — the mythril premium rides along inside the macro via `material`, no extra argument |
| `src/battle/battle_init_content.c:67` | same, using the rider it already staged |
| `src/screens/item_content_banked.c:435` | same |
| `src/game/cards_content.c` | every row's cost byte becomes `CARD_AP(power, MAT_x, rider)` |
| `deck_init.c` / `battle_turn_draw_banked.c` | leave the phantom-card `1` alone: it is a safety net, not a card |

The rider term is free at all three runtime sites: they already compute
`status_id` for the burn/freeze/poison assignment, so
`(status_id != STATUS_NONE)` is the same fact, not a second decision.

### 8.3 Order of work

1. `loot.h` macros (including `LOOT_AP_MYTHRIL_RIDER`) only; no behavior
   change (verify: the three runtime sites still emit 1s and the catalog
   still matches §4.3 by hand), plus the `make card-costs` gate from §9 so
   §6.1 is already guarded before any number moves.
2. Switch the three runtime sites plus the `loot_drop_banked.c` rider
   suppression; run `loot_drop_victory`, `loot_ap_iron_cap`,
   `loot_ap_steel`, `cards_menu_loot_shift`, `loot_ring_uses_mythril`,
   `merchant_sell_loot`.
3. Switch the catalog rows; re-run `card_cost_energy`,
   `card_battle_heal`, `card_shield_defend`, `defend_dagger_preview_highcard`.
4. Pips 3–5 (`screens/ap_icons.json` + the OAM chain); `make memmap` and a
   sprite-budget check, then `make verify-oam`.  (Ported from the `new-pips`
   branch's wiring: same art cells and OAM ids, while `M BO` keeps the
   derived 5 from the formula instead of the hand-set 5.)
5. The §7.2 pool start (base 2) — implemented with the ROM change, not
   after it: the formula prices the starter fire swords at 2, so the change
   is not accepted with the pool at 1.  No separate pool-growth follow-up:
   the four +1 grants already take the pool 2 → 6, which plays even mythril
   riders at full completion.  (Reverted 2026-11-03: base back to 1,
   ladder 1 → 5, fire swords dead until the first grant — see §7.2.
   Mythril riders at 6 are unplayable until a pool-6 source exists.)

Run `make test-sentinels` after steps 1–3 regardless: the edits shrink
banked code, and per AGENTS.md §52.19 even a shrink/move can flip a latent
layout-sensitive miscompile.  `make memmap` is still the gate if the
catalog rows grow (they shrink: the literal columns were wider than the
macro calls).

### 8.4 Tuning knobs (ranked)

The weights are **not** balance dials.  Material 4, rider 8 and the
mythril 12 are fixed by the ladder in §4, and the top of that ladder is
pinned by the pool: any shift-legal increase overshoots it (material weight
8 puts a plain mythril sword at `9 + 32 + 12 = 53`, i.e.
`(53 + 7) >> 3 = 7`, and drags plain steel to 4 with it).  A smaller
premium breaks the floor it exists for (8 instead of 12 leaves a mythril
shield at `8 + 16 + 8 = 32 -> 4`), a larger one strands plain mythril above
the demo pool (16 puts the sword at `9 + 16 + 16 = 41 -> 6` with no rider
involved).  Rider 16 makes every bronze rider cost 3 and breaks §6.1;
rider 0 makes statuses free.  Treat all three weights as part of the card
language, like the bands themselves.

The real levers, in order:

1. **The pool** (§7.1/§7.2) — the only way to change what a given cost
   *buys*.  No formula change.
2. **The band table** (`LOOT_POWER`, `docs/loot.md` §34.1) — moves power,
   and so cost and the loot gate together, preserving §6.1 by construction.
3. **Effect-specific riders** — a per-effect weight table would replace the
   flat `rider` bit without touching the expression (open question 1).

Not a lever, by design: the **fusion exchange rate** (how many weak cards
fuse into one strong card, §7.5).  That rate lives entirely in the future
fusion system; it must never feed back into AP weights.  If a fusion
recipe ever feels wrong, the recipe is wrong — the ordering guarantee (§6.3)
means the AP side cannot be the cause.

---

## 9. Verification

| Check | What it proves |
|---|---|
| New scenarios `card_ap_formula_wood` / `_rider` / `_mythril` | Stage the anchors through a **real deck**, not hand injection: `SET_HAND_CARD` bypasses `CardDefinition` lookup entirely (and never sets `cost` — only `SET_HAND_CARD_META` pins it, which would assert a hand-written value, not the formula).  The descriptor deck section deals five identical catalog cards (`WOOD_SWORD` → pip 96, `POISON_DAGGER` → pip 97, `BOW_9` → pip 122) and asserts the OAM pip tile on all five hand slots (`oam_tile`).  The cost travels catalog def → battle deck → render with no injection anywhere in the path.  `M F BO` (6) cannot be staged this way until a pool-6 battle exists — assert it via `make card-costs`, not via a battle. |
| New scenario `card_ap_loot_gate` | Seed 76 at pool 5 rolls a plain STEEL SWORD (derived id 192, formula cost 3) on the rank row (R3 kobold -- the pool is staged, not read): victory grants it, the reveal names it (`S SW` / `STEEL SWORD`), and the DECK tab row shows AP digit 3, which the pool affords.  Rank-decided cost on a real drop, not on a hand-picked card. |
| New scenario `card_ap_loot_gate_mythril_rider` | Seed 210 at pool 5 rolls mythril + a rider-eligible fire roll on the R5 mimic: the drop is id 242 (`M F BO`, cost 6) and the reveal names the rider -- no hold-back exists anymore.  The card cannot be decked until the pool reaches 6 (`deck_equip_locked_loot` pins the `NEED AP 6` refusal); the locked card is the motivation to raise AP. |
| `make card-costs` (required, host-side) | Recompute the formula over the 37 legal (material, weapon, effect) combos, fail on any `cost > gate_ap` where `gate_ap` is 6 for mythril riders and the material gate otherwise (§6.1), assert pairwise no-inversion (for every pair where B dominates A on all three inputs with at least one strict, `cost(B) >= cost(A)` — the §6.3/§7.5 fusion ordering) and strict riders (every rider pair costs exactly one AP more than the same card plain), and diff the result against §4.1/§4.2 of this document so these tables cannot rot.  Catches loot-table edits that no scenario happens to roll. |
| Existing suite | Every scenario that asserts a hand, a detail page or a shop row: the AP display changes for 5 of the 8 catalog cards (Fire Sword, Poison Dagger, Iron Sword, Iron Ring, Mythril Bow; Wood Sword, Wooden Shield and the Shop Keys are unchanged). Expect to update `card_cost_energy` and the DECK-tab assertions. |
| `make memmap`, `make test-sentinels` | Bank budget and SDCC-layout safety, even though the plan is layout-neutral. |

The negative test for §6.1 is cheap and worth writing, but it has to target a
row where it actually fires.  With rider weight 16, steel lands on exactly 5
(`7 + 12 + 16 = 35 -> 5`, inside the gate), so it proves nothing on its own;
**bronze** is the honest target: `3 + 4 + 16 = 23 -> 3 AP` against a gate of
2.  (Mythril would also fire under that edit — `9 + 16 + 12 + 16 = 53 -> 7`
against a gate of 6 — but it exercises the premium on top of the weight
change, so it is a worse minimal case.)  Confirm `make card-costs` fails on
the bronze edit, then restore.  The symmetric negative test for the new
gate is free: delete the `loot_drop_banked.c` rider suppression and confirm
`card_ap_loot_gate_mythril_rider` drops id 240 instead of 224.  A check that
cannot fail is not a check (AGENTS.md §52.16).

---

## 10. Alternatives considered and rejected

| Alternative | Why not |
|---|---|
| **Hand-maintained 100-cell cost table** (one number per material × effect × weapon) | Exactly the status quo with more cells: the same six locations to keep in sync, no guarantee of monotonicity, and a new material means 20 new numbers plus 8 new gate expectations. |
| **Power only** (`AP = ceil(power / 2)`) | The rider is free at every tier: a bronze fire bow (p3) costs the same 2 AP as a plain bronze sword, and at the top a mythril fire bow (p9) costs the same 5 as a plain one.  Pricing the side effect at nothing is the whole thing this formula exists to avoid. |
| **Price-proportional** (`AP = ceil(price / 10)`, rounding up) | Couples tempo to the gold economy: a sell-price tweak would silently change what is playable, and the Merchant's mythril bow (30 g, authored) would cost 3 AP by coincidence, not by design — while a loot roll of the same combo (17 g) would cost 2.  The division rule is itself arbitrary: with C integer division those same two prices give 3 and 1 instead, so the mapping has to be specified *and* justified, which the value ladder does not. |
| **Percent-of-card rider** (`power × 1.2` for a rider) | Backwards: it charges the most for the rider on the card where the rider matters least (a flat 3-damage status). |
| **Exponential tiers** (`2^material`) | Blows past any foreseeable pool: wood 1, bronze 2, iron 4, then steel 8 and mythril 16, so the top half of the ladder is unplayable and every rider there is free by default. |
| **Cost stored per card and verified by a test** | Keeps two sources of truth and fails *late* (at test time). Computing at the definition makes a wrong cost unrepresentable. |

---

## 11. Open questions

1. **Ice at weight 16?**  (Asked when ice still skipped whole enemy
   attacks, worth more than burn damage and the only non-flat status.)
   Ice is a grey-out now, flat like the rest (2 cards for 2 rounds), so
   the question is closed: keep rider weight 8.
2. **Does the bow deserve a reach premium?**  A bow already takes the
   high-power band, so it matches a same-material sword.  A +1 role step is
   dead weight under `ceil(/8)` — it changes no cell in §4.1 (wood bow goes
   from value 1 to 2, still 1 AP; every other row has at least 1 point of
   headroom the same way) — so a reach premium that matters must be at
   least a half step, and that breaks §6.1 at AP 1.  If reach ever becomes
   mechanical, the role term must be material-gated.
3. **Should `uses_per_battle` discount?**  A 1-use wood ring at 2 AP is poor
   value by any measure, but a discount would let a 1-use ring undercut the
   heal premium that keeps rings scarce.
4. **Should the pool reach 6?**  Not yet, and nothing else changes until
   it does: mythril riders already cost 6, but the ladder tops out at 5
   (base 1 + all four +1s), so no 6-cost card can be decked today -- it can
   be *dropped* (rank rows have no AP ceiling), just not worn.  If a fifth
   +1 source ever lands, the open question after it is 7–8 — the only
   lever that makes the top of the ladder feel like an upgrade rather
   than a trade.
5. **Editor surfacing.**  Costs become derived, so the Hero/DECK views need
   no input — but a read-only AP badge on the card detail page would make
   the formula visible to players ("why does this cost 5?" → "9 damage +
   mythril").
6. **Fusion exchange rate.**  How many weak cards fuse into one strong card
   is the future fusion system's decision, not this document's — §7.5 only
   constrains it to trade up in cost whenever it trades up in stats, which
   the formula guarantees by construction.  The rate itself waits for the
   fusion design and playtest data.

---

## Appendix A — the band-built equivalence

For every card the content tables can build:

```text
power    = 1                    if material == wood
         = 2·material + 1       sword, bow
         = 2·material           dagger, shield, ring

material = power >> 1           (both cases)
```

which is why §4.1/§4.2 can be read straight off the power ladder:

```text
p1 -> wood      plain 1 AP   rider 2 AP
p2 -> bronze    plain 1 AP   rider 2 AP
p3 -> bronze    plain 1 AP   rider 2 AP
p4 -> iron      plain 2 AP   rider 3 AP
p5 -> iron      plain 2 AP   rider 3 AP
p6 -> steel     plain 3 AP   rider 4 AP
p7 -> steel     plain 3 AP   rider 4 AP
p8 -> mythril   plain 5 AP   rider 6 AP
p9 -> mythril   plain 5 AP   rider 6 AP
```

(The p1 rider cell is arithmetic only: wood cannot carry an effect (§4.2),
so no legal wood card ever reaches 2 AP.)

Any card that breaks this ladder (a curated off-band card, a future weapon
with a different band) is still priced correctly, because the formula reads
material as an explicit fact (§5.1) rather than re-deriving it.
