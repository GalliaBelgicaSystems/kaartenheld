# Card AP Cost — A Deterministic Formula (Proposal)

**Status:** proposal, 2026-11-02. Not implemented. Nothing in `src/` changes
until this document is accepted.

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
value = power  +  (material << 2)  +  (rider << 3)

AP    = (value + 7) >> 3           /* = ceil(value / 8) */
AP    = min(AP, 5)                 /* demo pool ceiling */
```

* `power` — the card's face value, meaning **damage dealt** (sword/bow/
  dagger), **damage blocked** (shield) or **HP restored** (ring).  1..9.
* `material` — 0..4: wood, bronze, iron, steel, mythril.
* `rider` — 1 when the card carries an on-hit status (poison, burn, freeze),
  0 when plain.

In AP-step terms: **one AP step is worth 8 value points; a point of damage/
block/heal is 1, a material tier is 4, a rider is a whole step.**

Anchors:

```text
W SW   power 1, wood    -> 1 AP        (the brief's "very simple wood sword")
M F SW power 9, mythril + fire -> 5 AP (the brief's "mythril fire sword")
```

No division, no multiplication, no table: two shifts and two adds.  The
natural maximum is 5 for any card the content tables can produce
(`9 + 16 + 8 = 33 -> 5`), so the clamp only guards injected/out-of-band ids.

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

It is **not** the card's gold value.  Price is a separate, already
deterministic formula (`docs/loot.md` §16/§34.6: `power + 2·tier` (+2 for a
poison rider), stored in `CardDefinition.price`).  AP buys *tempo*, price
buys *economy*; the two ladders are allowed to disagree (a mythril bow is
worth 30 gold but costs 4 AP, not 30).

And AP is **not** a rarity signal: rarity is the loot roll
(`docs/loot.md` §34.5).  Cost tracks what the card *does*, gate AP tracks
how likely it is to *drop*.

---

## 2. Why the current numbers cannot stay

| Rule | Where | Values |
|---|---|---|
| Authored per row | `src/game/cards_content.c` | 1, 1, 1, 1, 1, 0, 2, 1 |
| Loot weapon table | `src/rpg/loot_banked.c:101` | all weapons 1 |
| Inline loot path | `src/battle/battle_init_content.c:67` | `(wpn <= WPN_RING) ? 1 : 0` |
| Screen path | `src/screens/item_content_banked.c:435` | same expression |
| Phantom/fallback card | `src/battle/deck_init.c:66`, `battle_turn_draw_banked.c:61` | 1 |

Five places, four rules, one visible value.  Adding one card today means
guessing a byte in a table; adding one *material* means remembering to edit
three copies.  That is the whole problem this document solves: **the cost
must be a property of the card, not a decision by the author.**

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
band floor — that gap is what makes a mythril shield cheaper than a mythril
sword (§6.3).

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

Any on-hit status (poison, burn, freeze) costs **one whole AP**, regardless
of tier.  Flat, not scaled, because the statuses are flat: `STATUS_POISON`
and `STATUS_BURN` tick 1 damage for 3 rounds and `STATUS_FREEZE` denies one
enemy attack (`src/rpg/status_content.c`) — the rider's *absolute* value is
constant whether it rides a p2 dagger or a p9 sword, so its *relative* price
must fall as power rises.  A percentage-of-card price would charge the most
for the rider on the biggest card, which is backwards.

`rider` is readable from the definition itself
(`status_id != STATUS_NONE`), so no new field is needed: both the loot
synth and the catalog already decide whether a card carries one.

### 3.4 What is deliberately *not* in the formula

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
`AP = ceil((power + 4·material [+ 8 if rider]) / 8)`.

### 4.1 Plain cards — material × weapon

| material | sword | shield | bow | dagger | ring |
|---|---|---|---|---|---|
| **wood** (gate AP 1) | 1 AP (p1) | 1 AP (p1) | 1 AP (p1) | 1 AP (p1) | 1 AP (p1) |
| **bronze** (gate AP 2) | 1 AP (p3) | 1 AP (p2) | 1 AP (p3) | 1 AP (p2) | 1 AP (p2) |
| **iron** (gate AP 4) | 2 AP (p5) | 2 AP (p4) | 2 AP (p5) | 2 AP (p4) | 2 AP (p4) |
| **steel** (gate AP 5) | 3 AP (p7) | 3 AP (p6) | 3 AP (p7) | 3 AP (p6) | 3 AP (p6) |
| **mythril** (gate AP 5) | 4 AP (p9) | 3 AP (p8) | 4 AP (p9) | 3 AP (p8) | 3 AP (p8) |

"gate AP" is the AP below which that material cannot drop
(`LOOT_AP_BRONZE` / `LOOT_AP_IRON` / `LOOT_AP_STEEL`, `src/rpg/loot.h`,
`docs/loot.md` §34.5).  The column is there to be checked: see §6.1.

### 4.2 Rider cards — the only legal (effect, weapon) pairs

| material | F SW (burn) | I SW (freeze) | P DA (poison) |
|---|---|---|---|
| **wood** (gate AP 1) | n/a | n/a | n/a |
| **bronze** (gate AP 2) | 2 AP (p3) | 2 AP (p3) | 2 AP (p2) |
| **iron** (gate AP 4) | 3 AP (p5) | 3 AP (p5) | 3 AP (p4) |
| **steel** (gate AP 5) | 4 AP (p7) | 4 AP (p7) | 4 AP (p6) |
| **mythril** (gate AP 5) | 5 AP (p9) | 5 AP (p9) | 4 AP (p8) |

Wood has no row by design (wood is always plain, §34.1) — and that is lucky:
a wood+rider card would be the only combination in the whole table that
costs more AP than the AP that unlocks its material (§6.1).

### 4.3 Catalog cards — before and after

| Card | Row | power / material / rider | Formula | Today |
|---|---|---|---|---|
| Wood Sword | `W SW` | 1 / wood / — | **1** | 1 |
| Wooden Shield | `W SH` | 1 / wood / — | **1** | 1 |
| Fire Sword | `F SW` | 3 / bronze / burn | **2** | 1 |
| Poison Dagger | `P DA` | 2 / bronze / poison | **2** | 1 |
| Iron Sword | `I SW` | 5 / iron / — | **2** | 1 |
| Iron Ring | `I RG` | 4 / iron / — | **2** | 1 |
| Mythril Bow | `M BO` | 9 / mythril / — | **4** | 2 |
| Amulet | `AMULET` | SPECIAL, not playable | **0** | 0 |

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

`(power + (mat << 2) + (rider << 3) + 7) >> 3` is shifts and adds only — no
`/`, no `%` (AGENTS.md §52.18: those pull the SDCC div/mod library into the
fixed bank), and 8-bit multiply is avoided too.  In the catalog the whole
expression folds to a constant at compile time; at runtime (loot synth, the
inline battle path, the card list) it is a handful of bytes, cheaper than
the `s_weapons[]` table lookup it replaces.

### 5.3 SPECIAL cards

`CARD_TYPE_SPECIAL` (quest items such as the Amulet) are not battle cards,
are not deckable, and keep cost 0 as an explicit carve-out: the formula
requires `power >= 1`.  Cost 0 should stay reserved for "not playable", never
for "free card" — the budget check and the combo economy both assume a
staged card consumes at least 1 AP.

---

## 6. Guarantees

The formula is not just monotone; it has three properties worth testing.

### 6.1 Every card is affordable at the AP that unlocks it

```text
cost(material, weapon, effect) <= gate_ap(material)   for every legal card
```

Verified cell by cell against §4.1/§4.2: wood ≤ 1, bronze ≤ 2, iron ≤ 3 ≤ 4,
steel ≤ 4 ≤ 5, mythril ≤ 5.  The lone violators would be wood+riders, which
the generator and the synthesiser both forbid (§34.1, `rider_legal()`).

This is the invariant that makes the loot AP gate coherent: a card that can
drop is a card you can play, in the same battle, on the same pool.  Any
future tuning knob must preserve it (§8.4).

### 6.2 It never exceeds the pool ceiling

Max legal input: `9 + 4·4 + 8 = 33 -> 5`.  The formula cannot emit 6 or 7 for
table-built cards, so the demo's 5-AP pool ceiling is never violated and no
card is ever *unplayable* purely by arithmetic.  The `min(AP, 5)` is a guard
for injected/debug ids, not a real clamp.

### 6.3 Monotone in every axis

More power, higher material, or an added rider never lowers the cost
(all three terms are non-negative and the map `x -> ceil(x/8)` is monotone).
That is the property that lets designers *reason* about cards without
consulting a table: "strictly better card, never cheaper".

### 6.4 Deterministic and save-neutral

The cost is a pure function of immutable definition bytes: no RNG, no game
state, no frame timing, no build-order dependence.  The same card costs the
same in the release ROM, the debug ROM, the harness, and after a save/load
roundtrip.  Nothing new enters `GameState`; `BattleCard.cost`
(`src/battle/card.h`) stays the runtime copy it already is.

---

## 7. Design consequences

### 7.1 A 5-AP card is a whole turn

With the demo's pool ceiling, a mythril fire sword can never be combined
with anything.  Meanwhile five 1-AP cards can form a straight (210%) or five
of a kind (400%, `docs/combo-system.md`), so raw damage-per-AP favours
combos.  The expensive card is therefore an *option*, not an upgrade: it
buys certainty (a single card always "combos"), a rider, and the highest
single value on the table.

Two coherent answers, both fine, but pick one:

* **(a) Grow the pool with the ladder.**  `VARIABLE_ID_ENERGY_POOL` is
  uncapped — only the *loot gate* clamps at 5 (`docs/loot.md` §34.5).  If
  future AP sources take the pool to 7–8, a mythril card can sit next to two
  1-AP cards and the top of the ladder becomes combo fodder instead of a
  finisher.  Recommended: keep average cost per staged card near 2, so a
  3–4 card combo stays affordable at every stage of the game.
* **(b) Keep 5 as the hard ceiling.**  Then the top cards are deliberately
  solo plays and the ladder reads as "one big hit *or* a hand of small ones".

### 7.2 The floor is 1, and the early game gets tighter

The starter deck (`screens/hero.json`) is four wood swords, four wood
shields and **two bronze fire swords**.  Under this formula the fire swords
cost 2 while the hero's pool starts at 1
(`src/game/content.c:83`), so they are dead cards until the first AP grant.
That is arguably correct — they are the deck's "you are growing" card — but
it must be a decision, not a surprise:

* either accept it and document it in the manual,
* or start the pool at 2, which is what the player manual already claims
  (`docs/manual.md:167`: "You get **2 Energy** each round").

### 7.3 Block and heal are cheaper than damage, on purpose

Block (p2/4/6/8) and heal (p2/4/6/8) sit one band below damage (p1/3/5/7/9),
so a mythril shield or ring costs 3 while a mythril sword costs 4.  Blocking
only pays when the enemy attacks; a ring's heal is limited to `material + 1`
charges.  If either ever feels too cheap, the fix is the band, not the
formula.

### 7.4 Display work follows automatically

The battle hand shows AP as OAM pips: `AP_TILE_1/2` (OAM tiles 96/97,
`screens/ap_icons.json` → `src/game/ap_tiles_generated.h`) and a two-branch
chain in `src/battle/battle_oam_banked.c:236`.  Costs of 3–5 will appear the
moment the formula lands, so the pip chain has to grow.  The art already
exists on the shared sheet (`one/two/three/four/five ap` cells, also listed
in `tools/level_editor/tilesets/actors.json`), but only cells 1 and 2 are
wired; 3–5 need tile slots from the sprite budget and a lookup instead of
the if-chain.  The DECK tab already prints the cost as a BG digit
(`docs/deck-management.md`: "AP cost digit at cols 3-4"), so it needs
nothing.

---

## 8. Implementation plan

### 8.1 Add the formula

`src/rpg/loot.h`, next to `LOOT_POWER` (same arc, same constants):

```c
/* docs/card-ap-cost-formula.md: one AP step = 8 value points. */
#define CARD_AP_MATERIAL_PTS 4
#define CARD_AP_RIDER_PTS    8
#define CARD_AP_MAX          5

#define CARD_AP_VALUE(power, material, rider)                    \
    ((uint16_t)((uint16_t)(power) +                              \
                ((uint16_t)(material) << 2) +                    \
                ((uint16_t)(rider) << 3)))

#define CARD_AP(power, material, rider)                          \
    ((uint8_t)((CARD_AP_VALUE(power, material, rider) + 7) >> 3))
```

All three runtime call sites are already banked files
(`src/battle/battle_init_content.c` and `src/screens/item_content_banked.c`
are bank 2, `src/rpg/loot_banked.c` is bank 8), so they expand the macro
inline from their own bank-local table values.  That is deliberate: a
`card_ap()` helper in fixed-bank `src/rpg/loot.c` would spend `_CODE` bytes
on a two-instruction expression, and `_CODE` is the budget that corrupts
bank 2 when it overflows (AGENTS.md §52.18/§55.5).  No new fixed-bank
symbol, no new banked body, no trampoline.

### 8.2 Collapse the duplicated rules

| File | Change |
|---|---|
| `src/rpg/loot_banked.c` | delete `s_weapons[].cost` (the column) and compute `CARD_AP(power, material, status_id != STATUS_NONE)` |
| `src/battle/battle_init_content.c:67` | same, using the rider it already staged |
| `src/screens/item_content_banked.c:435` | same |
| `src/game/cards_content.c` | every row's cost byte becomes `CARD_AP(power, MAT_x, rider)` |
| `deck_init.c` / `battle_turn_draw_banked.c` | leave the phantom-card `1` alone: it is a safety net, not a card |

The rider term is free at all three runtime sites: they already compute
`status_id` for the burn/freeze/poison assignment, so
`(status_id != STATUS_NONE)` is the same fact, not a second decision.

### 8.3 Order of work

1. `loot.h` macros + `loot_card_ap()`; no behavior change (verify: the four
   runtime sites still emit 1s and the catalog still matches §4.3 by hand).
2. Switch the three runtime sites; run `loot_drop_victory`,
   `loot_ap_iron_cap`, `loot_ap_steel`, `cards_menu_loot_shift`,
   `loot_ring_uses_mythril`, `merchant_sell_loot`.
3. Switch the catalog rows; re-run `card_cost_energy`,
   `card_battle_heal`, `card_shield_defend`, `defend_dagger_preview_highcard`.
4. Pips 3–5 (`screens/ap_icons.json` + the OAM chain); `make memmap` and a
   sprite-budget check, then `make verify-oam`.
5. Balance pass on §7.1/§7.2 (pool start, tier grants) as a separate commit.

Steps 1–3 touch no struct, no WRAM layout, and no banked placement, so they
do not require the §52.19 sentinel run — but `make memmap` is still the gate
if the catalog rows grow (they shrink: the literal columns were wider than
the macro calls).

### 8.4 Tuning knobs (ranked)

1. **Pool growth** — the real lever (§7.1).  No formula change.
2. **Rider weight** — 8 (one step) today; 0 would make riders free, 16
   would price them at two steps.  Any change must keep §6.1.
3. **Material weight** — 4 today.  5 would push mythril plain swords to
   6 AP (a violation of §6.2), so this knob is effectively frozen unless
   the cap moves.
4. **Effect-specific riders** — see open questions.

---

## 9. Verification

| Check | What it proves |
|---|---|
| New scenario `card_ap_formula` | Stage the anchors with the existing hand-injection debug action (`set_hand_card`): `W SW` → 1, `B F SW` → 2, `I SW` → 2, `M BO` → 4, `M F SW` → 5. Assert the OAM pip tile per card (`oam_tile`) and, on the DECK tab, the AP digit on the row (`screen_row`). |
| New scenario `card_ap_loot_gate` | At AP 2 / 4 / 5, run the loot roll and assert `cost(drop) <= pool` for the dropped id (the §6.1 invariant on real drops, not on a hand-picked card). |
| `tools/dev.py` host check (optional, `make card-costs`) | Recompute the formula over the 40 legal (material, weapon, effect) triples and diff against §4.1/§4.2 of this document, so the tables here cannot rot. |
| Existing suite | Every scenario that asserts a hand, a detail page or a shop row: the AP display changes for 6 of the 8 catalog cards (only wood stays 1). Expect to update `card_cost_energy` and the DECK-tab assertions. |
| `make memmap`, `make test-sentinels` | Bank budget and SDCC-layout safety, even though the plan is layout-neutral. |

The negative test for §6.1 is cheap and worth writing: temporarily set the
steel rider row to weight 16 and confirm `card_ap_loot_gate` fails (a steel
fire sword would cost 6 > the pool).  A check that cannot fail is not a check
(AGENTS.md §52.16).

---

## 10. Alternatives considered and rejected

| Alternative | Why not |
|---|---|
| **Hand-maintained 100-cell cost table** (one number per material × effect × weapon) | Exactly the status quo with more cells: four copies of the rule, no guarantee of monotonicity, and a new material means 20 new numbers. |
| **Power only** (`AP = ceil(power / 2)`) | Bronze fire sword = 3 AP while its material only unlocks at pool 3, and the rider is free at the top (a mythril fire sword would cost the same as a plain one). |
| **Ceil(power / 2) with clamping** | Clamping hides the rider: mythril plain and mythril fire both land on 5, so the strongest card's best rider is free. |
| **Price-proportional** (`AP = price / 10`) | Couples tempo to the gold economy: a sell-price tweak would silently change what is playable, and the mythril bow (30 g) would cost 3 AP by coincidence, not by design. |
| **Percent-of-card rider** (`power × 1.2` for a rider) | Backwards: it charges the most for the rider on the card where the rider matters least (a flat 3-damage status). |
| **Exponential tiers** (`2^material`) | Only three usable steps (1, 2, 4) before the pool cap, and it makes bronze fire swords unplayable at their own gate. |
| **Cost stored per card and verified by a test** | Keeps two sources of truth and fails *late* (at test time). Computing at the definition makes a wrong cost unrepresentable. |

---

## 11. Open questions

1. **Freeze at weight 16?**  Freeze denies a whole enemy attack, worth more
   than 3 burn damage.  Pricing it at a second step breaks §6.1 for bronze
   ice swords (3 AP at gate 2).  Either accept the invariant break for that
   one row, or move the bronze gate to 3 — a loot-table change, not a cost
   change.
2. **Does the bow deserve a reach premium?**  A bow already takes the
   high-power band, so it matches a same-material sword.  A +1 role step
   would make every wood bow cost 2, violating §6.1 at AP 1.  If reach ever
   becomes mechanical, the role term must be material-gated.
3. **Should `uses_per_battle` discount?**  A 1-use wood ring at 2 AP is poor
   value by any measure, but a discount would let a 1-use ring undercut the
   heal premium that keeps rings scarce.
4. **Should the pool ceiling move to 7–8?**  This is the only lever that
   makes the top of the ladder feel like an upgrade rather than a trade
   (§7.1).
5. **Editor surfacing.**  Costs become derived, so the Hero/DECK views need
   no input — but a read-only AP badge on the card detail page would make
   the formula visible to players ("why does this cost 4?" → "9 damage +
   mythril").

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
p8 -> mythril   plain 3 AP   rider 4 AP
p9 -> mythril   plain 4 AP   rider 5 AP
```

Any card that breaks this ladder (a curated off-band card, a future weapon
with a different band) is still priced correctly, because the formula reads
material as an explicit fact (§5.1) rather than re-deriving it.
