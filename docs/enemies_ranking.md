# Enemy Ranking & Rank-Driven Loot

**Status:** implemented 2026-11-04 on branch `balancing` (Phases 0–2
landed: rank data model, rank-only loot, the equip gate, stat bands, the
audit gate and the scenario matrix).  Decisions resolved 2026-11-04
(see §8).  Code references are to the tree the proposal was written
against; numbers in §3–§6 are computed exactly from the 1/256 roll
tables (not sampled).  §1.3 documents the pre-rank AP-gated roll the
proposal replaced; §6 is the implementation plan as executed.

**Mechanism (decided).**  The drop roll is **rank-only**; the energy pool (AP)
becomes an **equip unlock**.  An enemy always rolls from its rank's row no
matter what the player's pool is, so a slime knight can hand over iron or steel
even at AP 2 — but a card whose derived AP cost exceeds the live pool cannot be
*decked* until the pool grows (§4.3, §4.8).  This replaces the earlier draft's
"`max(AP row, rank row)` + AP cap": rank alone decides quality, AP decides what
you can wear.

**Chain note.**  A slime knight is not one encounter: felling it spawns a
**slime trio in place**, so one kill is two battles and two loot rolls.  That
chain is part of the model (§1.6, §4.7, §5.2).

**Why this document exists.**  Loot quality today depends on exactly one
thing: the player's energy pool (AP).  A slime and a slime knight fought at
the same AP drop from the identical distribution, so the game has no way to
say "this enemy is worth more than that one" and no way to make killing a
slime knight feel like an upgrade over killing a slime.  This document
proposes an explicit **enemy rank** (R0–R6), wired into three places:

1. the material roll of the victory drop (quality skews and is *centered* on
   a material — a slime knight lives in iron/steel, a mimic in steel);
2. the enemy's **damage** band (its deck's swing powers);
3. the enemy's **HP** and gold bands.

Related documents: `docs/loot.md` (§17 enemy loot profiles, §34 the
authoritative derived-id spec), `docs/card-ap-cost-formula.md` (the AP cost
formula this document leans on), `AGENTS.md` §52.18/§52.24/§55.5 (fixed-bank
and stack constraints), `docs/DEBUG_PROTOCOL.md` (harness contract).

---

## 1. How loot works today

### 1.1 One roll per encounter, at battle start

The drop is decided **once, when the battle starts**, not when the enemy
dies:

```text
start_battle_from_world()          src/screens/overworld_screen.c
        │  battle_start(..., battle_id, solo)
        ▼
battle_start()                     src/battle/battle.c
        │  g_loot_id = game_loot_drop(battle_id)
        ▼
game_loot_drop(battle_type)        src/game/content.c   (thin fixed-bank wrapper)
        │  trampoline
        ▼
game_loot_drop_banked()            src/game/loot_drop_banked.c   (ROM bank 9)
        │  writes the derived card id into g_loot_id
        ▼
world_on_battle_end()              src/world/world.c
           victory → deck_collection_add(state.cards, g_loot_id, 1)
```

Every hostile encounter that is not `BATTLE_NONE`/`solo` is a **trio** (the
struck actor plus two clones of its stats), but there is still exactly **one
roll per encounter** — one card per victory, not one per enemy.  Gold works
the same way (`act->gold_reward` is granted once), so all per-encounter
numbers below are per *encounter*.

One enemy breaks that rule by being **two encounters in a row**: the slime
knight's defeat spawns a slime trio on the same tile, so one kill pays twice
(§1.6).  Every number in this document that says "per encounter" is
per encounter; the knight chain is two of them.

Two consequences that matter for this proposal:

* the roll only knows the **BattleId**; it never sees the enemy's HP, damage
  or type beyond that id;
* the rank only has to be known at battle start, which means it can be
  keyed by BattleId — no engine plumbing and no new `battle_start` argument
  is required.

### 1.2 What a loot card is: material × effect × weapon

Every drop is a point in a 5 × 4 × 8 grid, encoded as a **derived CardId**
(`docs/loot.md` §34.1):

```text
id = 0x60 + (material << 5) + (effect << 3) + weapon
      └─ LOOT_ID_BASE                 │           │
                                      │           └─ 0 sword, 1 shield, 2 bow,
                                      │              3 dagger, 4 ring (5-7 reserved)
                                      └─ 0 plain, 1 poison, 2 fire, 3 ice
material 0..4 = wood, bronze, iron, steel, mythril
```

`card_get_def()` synthesizes the definition from the id (`src/rpg/loot_banked.c`):
name (`W SW`, `I F BO`, `M P DA`), power, AP cost, rider/status, ring uses,
price.  The collection, deck, shops, save data and battle all treat loot
cards as ordinary cards; there is no instance layer.

### 1.3 The roll, step by step

The roll uses its own xorshift stream (`g_loot_rng_state`, seeded from the
main seed by `rng_set_seed`), never the shared gameplay RNG, so a drop can
never shift a scenario's input outcomes.  Exactly three 16-bit draws are
consumed (two for a bat):

```text
draw 1  (always consumed, even though the old 50% gate is gone)
draw 2  weapon:  mask &3 → sword | shield | dagger | ring   (bat: always bow)
draw 3  mat_roll = low byte, eff_roll = high byte
```

**Material — AP-gated weighted roll.**  `mat_roll` (0..255) walks up the
cut row while it exceeds the current threshold:

```c
while (material < cap && mat_roll > s_ap_cut[row][material]) material++;
```

The current table (`src/game/loot_drop_banked.c`) is:

| AP | cuts (1/256) | wood | bronze | iron | steel | mythril |
|---|---|---|---|---|---|---|
| 1 | `255` | 100% | – | – | – | – |
| 2 | `204, 255` | 80.1% | 19.9% | – | – | – |
| 3 | `166, 255` | 65.2% | 34.8% | – | – | – |
| 4 | `128, 204, 255` | 50.4% | 29.7% | 19.9% | – | – |
| 5+ | `89, 166, 230, 250, 255` | 35.2% | 30.1% | 25.0% | 7.8% | 2.0% |

`cap` comes from `s_ap_cap`: AP 1 = wood, AP 2–3 = bronze, AP 4 = iron,
AP 5+ = mythril.  So AP is both a *profile* and a *ceiling*.

**Effect — one flat rider gate, weapon legality decides which rider.**
A rider is rolled only when the material is bronze or better and
`eff_roll < 64` (**25%**); within that, bows get fire for
`eff_roll < 32` and ice otherwise (12.5% each), and daggers get poison
(25%).  Wood is always plain, shields/swords/rings can never carry a
rider, and a *mythril* rider is withheld until the pool reaches
`LOOT_AP_MYTHRIL_RIDER` (6), because a mythril rider card costs 6 AP
(`docs/card-ap-cost-formula.md` §6.1).  At AP 5 mythril is therefore always
plain.  (Both the ceiling and this rider hold-back are what §4.3 removes: the
proposed roll never reads AP.)

**Weapon — fixed archetype mix.**  Non-bat enemies pick sword/shield/
dagger/ring at 25% each.  Bats always drop a **bow**; the bow is otherwise
only purchasable (the merchant's `M BO`).

### 1.4 Two fixed grants on top of the roll

* **First victory ever** (`MONSTERS_DEFEATED == 0`) is overwritten with
  `B BRONZE SHIELD` so the opening loot teaches blocking.  Rolls still
  happen first, so the stream keeps its shape; this is rank-independent by
  design.
* The **mimic** pays AP through its story event instead of gold; its loot
  roll is a normal roll.  (Decision §8.3 adds a purse on top; the AP grant
  stays.)

### 1.5 What the drop is worth

Derived from the same material band (`LOOT_POWER` in `src/rpg/loot.h`):

```text
wood 1, bronze 2-3, iron 4-5, steel 6-7, mythril 8-9
sword and bow take the high end of the band, shield/ring the low end;
daggers run a softer band (wood 1, bronze 2, iron 3, steel 5, mythril 7)
```

Synthesized **sell price** = `power + 2·material`, `+2` for a poison dagger
(`docs/loot.md` §16/§34.6).  **Ring uses** = `material + 1`.  **AP cost** is
derived by `docs/card-ap-cost-formula.md`:

```text
value = power + 4·material + (mythril ? 12 : 0) + (rider ? 8 : 0)
AP    = ceil(value / 8)                       /* 1..6 */
```

### 1.6 The slime-knight split: one kill, two encounters

A slime knight is the one enemy whose death is a **chain**, and the rank
model has to account for it: felling the knight does not end the reward,
because the armor breaks open and the crew inside becomes a second
encounter.

```text
bump the knight actor                start_battle_from_world()
        ▼
knight battle (BATTLE_SLIME_KNIGHT)  trio of 3 knights (3 × 15 = 45 HP)
        │  loot roll #1, +12 gold
        ▼
world_on_battle_end(victory)         src/world/world.c
        │  actor deactivated, gold paid, defeat recorded,
        │  then event_resolve_actor_defeated()
        ▼
EVENT_ID_KNIGHT_SPLIT                src/game/events_content.c
        │  trigger ACTOR_DEFEATED, actor ENTITY_ID_SLIME_KNIGHT, any map
        │  action 1: DIALOGUE_ID_KNIGHT_SPLIT ("... in fact 3 Slimes")
        │  action 2: EVENT_ACTION_SPAWN_SLIME_TRIO, ow 9
        ▼
game_slime_trio_spawn_banked()       src/game/slime_trio_spawn_banked.c (bank 10)
        │  the knight's runtime slot is rewritten IN PLACE at the same
        │  tile: id SLIME, hp/max 10, gold 5, BATTLE_SLIME, solo = 0,
        │  AI_CHASE, actor_id = 0 (untracked)
        ▼
bump that tile                      trio of 3 slimes (3 × 10 = 30 HP)
        │  loot roll #2, +5 gold
        ▼
overworld
```

So one knight kill is **two battles, 75 enemy HP, two loot rolls, two gold
grants (12 + 5 = 17 g) and six loot-RNG draws** (three per roll; a bat's bow
roll is two).  The "trio" in the action name is not a bespoke spawn of
three: the spawned actor is `solo = 0`, so the ordinary trio rule (struck
actor + two clones) yields three slimes.  The knight's own battle is a trio
for the same reason unless its actor row is flagged `solo` (the three castle
knights), which is why a field knight costs 45 HP and a castle solo knight
only 15 HP before the split.

Four consequences the rest of this document depends on:

* **The loot path needs no change.**  The spawned actor carries
  `BATTLE_SLIME`, i.e. rank **R1**, so the rank table already resolves the
  second roll as a fodder roll.  A knight chain is "one R4 roll + one R1
  roll" — not one R4 roll, and not two R4 rolls.
* **The second card is optional in practice.**  The trio is dynamic and
  untracked (`actor_id = 0`), chases the player, and despawns on scene reload
  like all runtime-only actors (`AGENTS.md` §53.1); the knight's own defeat
  is persistent.  A player who walks away after the briefing keeps the
  knight's card + 12 g and forgoes the second roll + 5 g.  Rank must be
  judged on the full chain, but content must not *assume* the player
  collects all of it.
* **The knight's own reveal is skipped.**  The split event opens a dialogue
  inside `world_on_battle_end`, so the VICTORY screen and its loot reveal
  never show for the knight battle — the card is still granted and still
  emits `LOOT_CARD_ADDED`.  Only the follow-up trio's victory shows a reveal
  card.  (`tests/battle_slime_knight_split.json` pins the dialogue,
  `tests/battle_slime_trio_spawn.json` pins the second reward and the total
  37 g = 20 staged + 12 + 5.)
* **The trio's stats are duplicated content.**  `slime_trio_spawn_banked.c`
  hardcodes HP 10, gold 5, the name `SLIME` and `ow 9` instead of reading the
  slime's content row.  Any rank/stat audit has to compare those constants
  against `screens/enemy_types/slime.json` and `src/game/actors_content.c`, or
  the chain drifts from the rank bands silently (§5.3).

---

## 2. How weapons are ranked today

There is no explicit rarity or weapon level.  A card's rank falls out of
four orthogonal properties, and the proposal must not duplicate them — the
enemy rank is a *fifth* input to the roll, not a replacement for any of
these.

### 2.1 The quality ladder is the material

Material is the only purely vertical axis: for a given weapon, each tier
step adds 2 power (wood 1; bronze 2–3; iron 4–5; steel 6–7; mythril 8–9),
with sword/bow at the top of each band and shield/ring at the bottom.
Daggers are the exception: a softer band (wood 1, bronze 2, iron 3,
steel 5, mythril 7) so the free counterattack doesn't outclass the
sword -- every dagger still beats the tier below it, and bronze (2)
always beats wood (1).

### 2.2 The tempo ladder is the AP cost

Material is *also* the dominant term in the AP cost (4 points per tier, and
mythril carries a 12-point premium).  This is why the material ladder and
the tempo ladder mostly agree but deliberately not always — a mythril bow
costs 5 AP and sells for 17 g, a mythril fire bow costs 6 AP and sells
for the same 17 g.

### 2.3 The rider ladder

A rider is worth a whole AP step (8 points) and is only legal on some
weapons (§34.2): fire/ice on bows, poison on daggers, nothing on swords,
shields or rings.  Swords are the plain all-rounder: they hit hard and
cost what the band says, no riders ever.  Riders are therefore a quality
signal that is *not* monotone in material: a bronze fire bow (2 AP)
beats a bronze plain sword (1 AP) but not an iron plain sword (2 AP).

### 2.4 The archetype ladder

| Weapon | Role | Band position | Riders | Note |
|---|---|---|---|---|
| Sword | damage | high end | none | the plain all-rounder attack card |
| Dagger | damage | own soft band (1/2/3/5/7) | poison | defense-phase counterattack line |
| Bow | damage | high end | fire, ice | drop source today: **bats only** (plus merchant) |
| Shield | block | low end | none | plain shields never heal (over-block clamps to 0) |
| Ring | heal | low end | none | the *only* healing vector; uses = tier + 1 |

### 2.5 The resulting ladder, by example

Power / AP / sell price for representative cards (all formulas from §1.5):

| Card | Power | AP | Sells | Uses |
|---|---|---|---|---|
| `W SW` | 1 | 1 | 1 g | – |
| `B SW` | 3 | 1 | 5 g | – |
| `I SW` | 5 | 2 | 9 g | – |
| `S SW` | 7 | 3 | 13 g | – |
| `M SW` | 9 | 5 | 17 g | – |
| `B F BO` | 3 | 2 | 5 g | – |
| `I F BO` | 5 | 3 | 9 g | – |
| `S F BO` | 7 | 4 | 13 g | – |
| `M F BO` | 9 | 6 | 17 g | – |
| `B P DA` | 2 | 2 | 6 g | – |
| `I RG` | 4 | 2 | 8 g | 3/battle |
| `M BO` | 9 | 5 | 17 g | – |

### 2.6 What is missing

Every card's rank is a function of itself.  Nothing in the roll knows how
strong the enemy was.  At AP 4 a slime and a slime knight hand out the same
ladder, so:

* the only way to improve drops is to raise AP (a story-gated, one-way
  lever), and
* enemies are interchangeable as loot sources — the game has no reason to
  prefer one fight over another.

---

## 3. Drop probabilities today (no rank)

All numbers are per **encounter** (one roll), for a non-bat enemy; a bat
multiplies each material figure by 100% instead of 25% for the weapon, and
always gets the bow.

### 3.1 `I F BO` (the bat iron fire bow) — the worked example

Rider bows drop from bats (the only bow source in the demo), so the
worked example is a bat roll at R2.  The probability chain for one
specific card:

```text
P(I F BO) = P(bow | bat) × P(iron | R2) × P(rider | R2) × P(fire | rider)
          = 1.0          × 22.3%        × 31.25%       × 0.5
          = 3.48%  (≈ 1 in 29 bat encounters)
```

P(iron | R2) is the R2 cut row (57/256); the rider gate at R2 is 80/256;
fire/ice split the rider roll half each.  Non-bat encounters never roll
bows, and swords never roll riders, so this is the only fire card in
the demo drop pool.

### 3.2 Related cards, same AP rows

| AP | `B SW` | `I SW` |
|---|---|---|
| 2 | 3.74% | – |
| 3 | 6.52% | – |
| 4 | 5.57% | 3.74% |
| 5+ | 5.64% | 4.69% |

Sword riders are gone (swords are plain), so the old `I F SW` / `S F SW` /
`M F SW` columns have no entries anymore.  The remaining rider on
non-bat encounters is dagger poison (25% of dagger rolls at the §4.5
gate); fire/ice live on bats — see the §3.1 chain.

### 3.3 A convenient summary metric

`E[material]` (0 = wood … 4 = mythril) and expected sell value of a drop,
computed over the weapon mix, give a single quality number per situation:

| AP | E[material] | P(bronze+) | P(iron+) | P(steel+) | E[sell] |
|---|---|---|---|---|---|
| 1 | 0.00 | 0% | 0% | 0% | 1.1 g |
| 2 | 0.20 | 19.9% | 0% | 0% | 1.8 g |
| 3 | 0.35 | 34.8% | 0% | 0% | 2.3 g |
| 4 | 0.70 | 49.6% | 19.9% | 0% | 3.5 g |
| 5+ | 1.11 | 64.8% | 34.8% | 9.8% | 5.1 g |

Every enemy in the game uses exactly one of these rows.  That is the gap
this proposal closes.

---

## 4. Proposal: an explicit enemy rank

### 4.1 The scale

Six playable ranks plus a rank-0 "no contest" bucket.  Names are the design
vocabulary (shipped strings can stay internal; the player never needs to
read "VETERAN"):

| Rank | Name | Meaning | Loot centre | Examples |
|---|---|---|---|---|
| 0 | LESSON | not a real fight: no drop, no gold | – | Carl's spar, NPCs, chests, dogs |
| 1 | FODDER | the tutorial-tier body: 2–3 swings, no riders | wood / bronze | slime, slime trio |
| 2 | VERMIN | fast or venomous challengers: 2–4 swings, one rider | bronze | bat, spider |
| 3 | GRUNT | the first real upgrade: 3–4 swings | bronze / iron | kobold |
| 4 | VETERAN | armed and armored: 4–5 swings | **iron** | slime knight |
| 5 | ELITE | optional set-piece: 5–6 swings, riders | **steel** | mimic |
| 6 | BOSS | the run's wall: 5–8 swings, flat/scripted | iron–mythril | Lord of Slimes |

The user-facing ordering this encodes: *slime knights are a real step up;
slimes, bats and spiders are trash; kobolds sit in the middle; the mimic is
elite (and pays its AP through the story *and*, by decision §8.3, a purse); the
Lord is the boss.*

### 4.2 Rank against current content

| Enemy | BattleId | HP (actor row) | Swing powers | Riders | Gold | Rank |
|---|---|---|---|---|---|---|
| Slime | `BATTLE_SLIME` | 10 | 2, 2, 2, heal 2 | – | 5 | **R1** |
| Slime trio (Mountain Pass) | `BATTLE_SLIME_TRIO` | 10 | 2, 2, 2, 2, heal 2 | – | 5 | **R1** |
| Bat | `BATTLE_BAT` | 8 | 2, 3, 4, 3, 3 | poison 60/255 | 8 | **R2** |
| Spider | `BATTLE_SPIDER` | 10 | 2, 3, 3, heal 2, 3 | poison 70/255 | 8 | **R2** |
| Kobold | `BATTLE_KOBOLD` | 15 | 2, 2, 3, heal 2 | – | 8 | **R3** |
| Slime Knight | `BATTLE_SLIME_KNIGHT` | 15 | 3, 3, 4, heal 2 | – | 12 | **R4** |
| Mimic | `BATTLE_MIMIC` | 50 (solo) | 4, 4, 5, 3, heal 3 | – | 0 (AP) → 20 (§8.3) | **R5** |
| Lord of Slimes | `BATTLE_NONE` (solo) | 70 (solo) | flat 4 → 6 (no deck, §8.4) | – | 50 | **R6** |
| Carl's spar | `BATTLE_CARL` | 10 | 0 | – | 5 → 0 (§8.2) | **R0** |

Note the duplicate HP sources: the battle uses the **actor row** from
`src/game/actors_content.c` (Lord 70), while `screens/enemy_types/*.json`
carries its own `hp` (Lord 99) that only the editor and tooling read.  The
rank audit in §5.2 must check both until the duplication is removed.

Three slime knights are also flagged `solo` (one 15 HP enemy instead of a trio).
That stays as it is by decision §8.5: they split into three slimes on death, so
the split is the encounter (§5.2 records the exception).

Every slime-knight number in this table is **per battle**.  A knight kill is
two battles (§1.6): add the spawned trio's 30 HP, 5 g and one R1 roll before
comparing a knight to a kobold or a mimic.

### 4.3 How rank enters the loot roll

**Decided (§8.1): rank = shape, AP = unlock.**  The material walk reads the
rank row only — the drop body no longer looks at the pool at all:

```c
row = g_enemy_rank_by_battle[battle_type];       /* 0..6 */
if (row == 0) { g_loot_id = CARD_NONE; return; } /* rank 0 = no drop */

material = MAT_WOOD;
while (material < MAT_MYTHRIL && mat_roll > s_rank_cut[row][material])
    material++;
```

Three consequences, each deliberate:

* **No AP read at all.**  `game_loot_drop_banked` stops reading
  `VARIABLE_ID_ENERGY_POOL`, and with it goes the unset-pool normalization
  (an unset variable used to roll the AP-1 wood-only row).  A drop becomes a
  function of (BattleId → rank, weapon roll, material roll, effect roll) only,
  so the same seed yields the same card at every pool.  That is the point of
  the decision: **a slime knight is a slime knight**, and the reason to hunt
  one is its rank, not the player's current AP.
* **No AP cap.**  At AP 2 a slime knight can drop steel (25.0%) and mythril
  (5.1%).  The gate moves to *equipping* (§4.8), where it is visible
  ("NEED AP 4") and lifts by itself when the pool grows, instead of silently
  clamping every high-rank roll down onto iron.
* **No mythril-rider hold-back.**  `LOOT_AP_MYTHRIL_RIDER` exists today only
  because a mythril rider costs 6 AP; with the equip gate the generator can
  hand one out early and the player simply cannot deck it yet.  One less AP
  dependency in the roll, and the locked card becomes a visible "come back
  with AP 6" marker in the collection.

**The rank rows** (one ordered table; row index = rank):

| Rank | Name | wood | bronze | iron | steel | mythril | E[mat] |
|---|---|---|---|---|---|---|---|
| 0 | LESSON | – | – | – | – | – | – |
| 1 | FODDER | 54.7% | 30.1% | 12.1% | 2.3% | 0.8% | 0.65 |
| 2 | VERMIN | 34.8% | 37.9% | 22.3% | 4.3% | 0.8% | 0.98 |
| 3 | GRUNT | 19.9% | 34.8% | 30.1% | 12.1% | 3.1% | 1.44 |
| 4 | VETERAN | 9.8% | 19.9% | **40.2%** | 25.0% | 5.1% | 1.96 |
| 5 | ELITE | 2.0% | 7.8% | 25.0% | **50.0%** | 15.2% | 2.69 |
| 6 | BOSS | 0.4% | 4.3% | 20.3% | 44.9% | **30.1%** | 3.00 |

The cut rows in 1/256 form (the ROM representation) are in Appendix A.  The
percentages are the realized outcomes of the 1/256 walk, not design targets;
each row sums to 100% (±0.4 for quantization).  Rows are also the reason the
"centered on a material" requirement works: R4 peaks on iron, R5 on steel, R6
on mythril.

**Rider gate by rank.**  Rank also raises the chance of a rider (a quality
signal that does not shift material), one step per rank:

| Rank | `eff_roll` gate | P(rider) | fire (bow) | ice (bow) | poison (dagger) |
|---|---|---|---|---|---|
| 0–1 | 64 | 25% | 12.5% | 12.5% | 25% |
| 2 | 80 | 31.25% | 15.6% | 15.6% | 31.25% |
| 3 | 96 | 37.5% | 18.75% | 18.75% | 37.5% |
| 4 | 112 | 43.75% | 21.9% | 21.9% | 43.75% |
| 5–6 | 128 | 50% | 25.0% | 25.0% | 50% |

The fire/ice figures are conditional on a bow and material ≥ bronze (the
only legal pairing); the poison figure is conditional on a dagger.
Bows only roll on bat encounters (100% bow there), so the unconditional
per-bat-encounter fire/ice chances are those numbers × the bronze+
share of the rank's material roll.  On non-bat encounters the only
rider is poison at 25% of dagger rolls.  Wood is still always plain;
swords, shields and rings never carry a rider.

**Rank 0 means no drop** (decided, §8.2): the body writes `CARD_NONE`, which
both `world_on_battle_end` and the victory reveal in `battle_screen.c` already
treat as "no card".  Gold is not part of the drop path, so rank-0 no-gold is a
content change (Carl's actor row → 0, §5.2).  Today a won spar rolls loot and
can even consume the first-victory bronze-shield grant, which is a lesson
fight paying out like a real one.

### 4.3.1 What AP still buys

The pool no longer touches odds; it decides **how much of what drops can be
worn**.  A card's AP cost is a pure function of (material, rider) — the weapon
does not matter, because the sword/bow +1 power is exactly absorbed by the
`ceil(value/8)` step (`docs/card-ap-cost-formula.md`):

| Material | plain | with a rider |
|---|---|---|
| wood | 1 | – (riders are bronze and above) |
| bronze | 1 | 2 |
| iron | 2 | 3 |
| steel | 3 | 4 |
| mythril | 5 | 6 |

That makes the **wearable share of a drop** the table that motivates raising
AP (share of drops whose card can be decked at that pool):

| Rank | pool 1 | pool 2 | pool 3 | pool 4 | pool 5 |
|---|---|---|---|---|---|
| R1 FODDER | 81% | 95% | 99% | 99% | 100% |
| R2 VERMIN | 67% | 91% | 99% | 99% | 100% |
| R3 GRUNT | 48% | 79% | 95% | 97% | 99% |
| R4 VETERAN | 25% | 61% | 89% | 95% | 99% |
| R5 ELITE | 8% | 29% | 72% | 85% | 96% |
| R6 BOSS | 4% | 20% | 59% | 70% | 92% |

Read the R4 row as the design statement: a knight killed at the starting pool
hands over a card the hero can wear one time in four, and by pool 4 that is
95%.  The locked remainder is the "I want more AP" pressure, and at pool 5 a
knight owes the player nothing.

### 4.4 What changes, concretely

Every rank now has one fixed distribution, so the comparison is against today's
*ceiling* (AP 5) as well as today's floor (AP 2):

| Rank | E[sell] under rank | today at AP 2 | today at AP 5 | P(iron+) under rank | P(iron+) today (AP 5) |
|---|---|---|---|---|---|
| R1 slime | **3.29 g** | 1.67 g | 5.05 g | 15.2% | 34.8% |
| R2 bat / spider | **4.55 g** | 1.67 g | 5.05 g | 27.3% | 34.8% |
| R3 kobold | **6.30 g** | 1.67 g | 5.05 g | 45.3% | 34.8% |
| R4 slime knight | **8.35 g** | 1.67 g | 5.05 g | 70.3% | 34.8% |
| R5 mimic | **11.26 g** | 1.67 g | 5.05 g | 90.2% | 34.8% |
| R6 Lord | **12.50 g** | 1.67 g | 5.05 g | 95.3% | 34.8% |

* **Fodder is worth less than today's AP-5 roll** (3.29 vs 5.05 g) and more
  than today's AP-2 roll (1.67 g): the pool stops subsidising trash, so
  farming slimes no longer converges on the same loot as killing knights.
* **The top ranks are 2–2.5× the old ceiling** (11.26 / 12.50 g vs 5.05 g):
  the mimic and the Lord now read as prizes rather than as slightly luckier
  slimes.
* **The early game gets richer.**  At the starting pool of 1 a slime can drop
  bronze (30.1%) and even iron (12.1%), where today it is wood-only until
  AP 2.  The first-victory bronze shield still overrides the very first drop,
  so the taught lesson is unchanged; whether the fodder tail is *too*
  generous is open question §8.8.
* **Nothing else about the ladder changes**: same weapon mix, same rider
  legality, same material band → power / sell / uses mapping.

### 4.5 Rider bows under ranks (demo: bats only)

Fire/ice bows roll only on bat encounters (R2, 100% bow), so there is
one rank row instead of a per-rank table.  At the R2 gate (80/256)
with a half-half fire/ice split:

| Bow | P(drop \| bat) | ≈ 1 in |
|---|---|---|
| `B F BO` / `B I BO` | 5.92% each | 17 bat encounters |
| `I F BO` / `I I BO` | 3.48% each | 29 |
| `S F BO` / `S I BO` | 0.67% each | 149 |
| `M F BO` / `M I BO` | 0.12% each | ~800 |

(Material shares from the R2 cut row: bronze 37.9%, iron 22.3%, steel
4.3%, mythril 0.8%; rider gate 31.25%, fire/ice half each.)  The old
per-rank chase (iron fire sword peaking at R4, steel at R5, mythril at
R6) is gone with sword riders; it comes back when higher-rank bow
enemies extend the roll (§34.5 note).  On non-bat encounters the only
rider is dagger poison at the §4.3 gate rates.

Plain swords on the same rows, for scale (`I SW` / `S SW` / `M SW`): R4
10.06% / 6.25% / 1.27%, R5 6.25% / 12.50% / 3.81%, R6 5.08% / 11.23% / 7.52%.

### 4.6 Per-enemy spot check

The pool each enemy is typically met at (walkthrough progression) and the share
of its drops that pool can actually wear (§4.3):

| Enemy | Rank | E[sell] | P(iron+) | P(steel+) | typical pool | wearable there |
|---|---|---|---|---|---|---|
| slime | R1 | 3.29 g | 15.2% | 3.1% | 2 | 95% |
| spider | R2 | 4.55 g | 27.3% | 5.1% | 3 | 99% |
| kobold | R3 | 6.30 g | 45.3% | 15.2% | 4 | 97% |
| slime knight | R4 | 8.35 g | 70.3% | 30.1% | 4 | 95% |
| mimic | R5 | 11.26 g | 90.2% | 65.2% | 5 | 96% |
| Lord | R6 | 12.50 g | 95.3% | 75.0% | 5 | 92% |

Bats substitute the bow for every weapon roll, so a bat's card is always a
bow of its rolled material: bronze 37.9%, iron 22.3%, steel 4.3%,
mythril 0.8% at R2 — of each material's share, 31.25% of bronze+ rolls
carry a rider at the R2 gate, split half fire / half ice (see the §3.1
chain for the worked fire-bow probability).

E[sell] follows §3.3 (sell = power + 2·material, plus the poison dagger's +2
weighted by its rider chance).  It is the merchant's price, not the card's
power.

### 4.7 The slime-knight chain under ranks

Because one knight kill is two encounters (§1.6), the rank rows compose: the
knight rolls R4, the spawned trio rolls R1.  Phase 1 needs **no extra code** for
this — the mapping is a consequence of `BATTLE_SLIME → R1` in §4.2 — and the
double roll is **kept by decision** (§8.7).

| Roll | Row | E[sell] | P(iron+) | P(steel+) | gold |
|---|---|---|---|---|---|
| knight battle | R4 | 8.35 g | 70.3% | 30.1% | 12 |
| spawned trio | R1 | 3.29 g | 15.2% | 3.1% | 5 |
| **chain** | R4 + R1 | **11.64 g** | **74.8%** (≥ 1) | 32.5% (≥ 1) | **17** |

Same convention as §4.6; `P(≥ 1) = 1 − (1 − p₁)(1 − p₂)`.  AP no longer appears
in the table at all: a knight chain pays the same cards at pool 1 and at pool 5
— only the wearable share moves (knight 25% → 99%, trio 81% → 100%).

Why keeping it is right: the second card is an R1 roll, i.e. fodder quality,
matching the second fight, so the chain is "a veteran card plus a fodder card"
— exactly the shape of the two battles, with no code and no new state.

The two rejected variants, for the record: **suppressing the spawned trio's
roll** would need new actor/WRAM state to tell the spawned slime apart from a
wild one (`AGENTS.md` §52.18/§52.24), and **downgrading the knight to R3**
erases the "a slime knight is a real step up" claim this document exists for.
If playtesting says the chain is too generous, the cheapest lever is the
knight's *gold* (12 → 10 keeps the chain at 15 g) — a content constant, not the
ladder.

### 4.8 The equip gate (how "AP only unlocks" is enforced)

Without a gate the decision in §8.1 is half-implemented: a knight could drop a
6-AP card at pool 1, the player could deck it, and every battle would reject
it at selection — a dead deck slot with no explanation.  The gate is therefore
part of the loot feature, not a follow-up.

**Rule.**  Any card whose `cost` exceeds the live pool cannot be added
to the deck -- curated or loot-derived (`loot_is_loot_id(id)`, i.e. id ≥
`LOOT_ID_BASE`).  A card you cannot pay for would be a dead deck slot.
Pre-decked grants are unaffected: the starter deck, shop/quest rewards and
scenario staging arrive via the ungated `deck_add_card`, so the plain
bronze swords in the pool-1 starter deck stay put.  Only *player-chosen
equipping* (quick-screen A, harness `deck add`) is gated.

**Shape.**

* `deck_equip_card(cs, id, pool)` in `src/rpg/deck.c` — the existing
  `deck_add_card` rules plus the lock, returning a reason
  (`EQUIP_OK` / `EQUIP_FULL` / `EQUIP_MIN` / `EQUIP_LOCKED` / `EQUIP_REJECTED`).
  `deck_add_card` stays ungated for authored grant paths, so `game_new_game`,
  scenario staging and shop grants keep working unchanged.
* The quick screen's A action calls the gated wrapper with the pool it already
  has in `g->state.variables`, and maps `EQUIP_LOCKED` to a `NEED AP n`
  message.  The digit is read from the selected card's `cost` at draw time, so
  **no new `Game` field and no WRAM growth** (§52.24).
* The DECK list shows the lock before the keypress (the AP column renders the
  digit differently on a locked row), because a rejection the player cannot
  predict is worse than no gate.
* The harness `deck add` debug action uses the same wrapper, so scenarios drive
  the real gate rather than a test-only path (§44).
* Telemetry `CARD_EQUIP_LOCKED` (`d0` card id, `d1` required AP) makes the
  rejection assertable, and no `CARD_ADDED_TO_DECK` fires with it.

**Why it is not a shop gate.**  Buying a card you cannot wear yet is allowed
(§8.11): the locked card in the collection is exactly the "keep looking for AP"
motivation, and refusing the sale would hide it.  Selling a locked card stays
allowed too.

---

## 5. Rank informs stats

Rank is meaningless if it only touches loot: a rank-6 enemy that hits for 3
is a lie.  The rank table therefore also owns the **per-enemy bands** for
HP, swing power, rider count and gold.  These are *bands checked by
tooling*, not numbers generated at runtime — content stays authored, the
rank only constrains it.

### 5.1 Bands

| Rank | HP (trio member) | HP (solo build) | Swing power | Rider cards in deck | Gold |
|---|---|---|---|---|---|
| 0 | scripted (Carl 10) | – | 0 | – | 0 |
| 1 | 6–10 | 18–24 | 2–3 | 0 | 3–6 |
| 2 | 8–12 | 24–32 | 2–4 | ≤ 1 | 6–10 |
| 3 | 14–18 | 36–48 | 3–4 | ≤ 1 | 8–12 |
| 4 | 16–22 | 48–64 | 4–5 | ≤ 2 | 12–18 |
| 5 | 30–40 | 50–65 | 5–6 | ≤ 2 | 15–25 |
| 6 | – (solo only) | 70–99 | 5–8 | ≤ 2 | 40–60 |

Rules behind the table:

* the trio is 3 × the member, so the "trio member" column is what a normal
  encounter costs in total (rank 4 trio = 48–66 HP, versus 45 today);
* a **splitting** enemy (today only the slime knight, §1.6) is audited as a
  **chain**: its own band plus the spawned trio's R1 band.  R4 + R1 =
  66–96 HP, 15–24 g, 2 rolls per kill — the current 75 HP / 17 g already fits,
  so do not raise the knight's HP to the member band *and* re-rank the trio in
  the same change;
* a solo build must be 2.5–3× a trio member, or the solo flag turns a
  miniboss into a speed bump.  The castle knights are the one documented
  exception (§5.2, decision §8.5);
* **one enemy swings per round** (shared deck, one attacker), so the swing band
  is per round, not per enemy.  Rank 6's floor is 5, and the Lord sits at 6
  after decision §8.4 — no exception needed any more;
* gold is granted once per victory, so the gold band is per encounter, not per
  clone.  **Rank 5 earns gold** (decision §8.3): the mimic's AP story grant is
  a separate reward, not a substitute for a purse;
* **rank 0 earns nothing** (decision §8.2): no drop, no gold — a lesson is not
  a farm;
* "rider cards" counts poison/burn/freeze entries in the deck, not the chance:
  the chance stays authored (60–70/255 style).

### 5.2 Current content audit

| Item | Current | Band | Verdict |
|---|---|---|---|
| slime HP 10, deck 2, gold 5 | R1 | 6–10 / 2–3 / 3–6 | passes |
| slime trio deck 2 | R1 | 2–3 | passes |
| bat HP 8, deck 2–4 + poison, gold 8 | R2 | 8–12 / 2–4 / 6–10 | passes |
| spider HP 10, deck 2–3 + poison, gold 8 | R2 | 8–12 / 2–4 / 6–10 | passes (low end) |
| kobold HP 15, gold 8 | R3 | 14–18 / 8–12 | HP and gold pass |
| **kobold deck 2, 2, 3** | R3 | **3–4** | **fails — below rank** |
| slime knight HP 15, gold 12 (member) | R4 | 16–22 / 12–18 | **HP fails by 1 (15 < 16)** |
| slime knight **chain**: 3×15 + 3×10 = 75 HP, 17 g, 2 rolls | R4 + R1 | 66–96 / 15–24 | passes |
| **slime knight deck 3, 3, 4** | R4 | **4–5** | **fails — below rank** |
| mimic HP 50 (solo), gold 0 | R5 | 50–65 / 15–25 | HP passes, **gold fails (decided: 20)** |
| **mimic deck 4, 4, 5, 3** | R5 | **5–6** | **fails — the 3 and 4s are below rank** |
| Lord HP 70 (solo), gold 50 | R6 | 70–99 / 40–60 | passes |
| **Lord swing flat 4** | R6 | **5–8** | **fails (decided: raise to 6)** |
| castle slime knights: solo 15 HP, then the spawned trio (45 HP chain, 17 g, 2 rolls) | R4 solo + R1 | 48–64 | **accepted exception (decided §8.5) — do not "fix"** |
| **Carl gold 5, spar rolls loot** | R0 | 0 / no drop | **fails (decided: no drop, gold 0)** |

Recommended content changes (proposal, not applied here; §8 items marked as
decided):

```text
kobold deck      {3,3,4, heal 3}                 (was 2,2,3, heal 2)
slime knight hp  20   (was 15)   gold 15 (was 12)
slime knight deck {4,4,5, heal 3}                (was 3,3,4, heal 2)
mimic deck       {5,5,6, 4, heal 4}              (was 4,4,5,3, heal 3)
mimic gold       20                              (was 0)   decided §8.3
Carl             rank 0 -> no loot, gold 0       (was drop + 5)  decided §8.2
Lord swing       6                               (was flat 4)    decided §8.4
                 + the merchant stocks a shield so the answer is always
                   purchasable: either CARD_WOODEN_SHIELD (owned 4x at new
                   game, so it only helps a player who sold them) or a new
                   S SH catalog card (power 6, AP 3, ~12 g) that answers the
                   swing alone (open question §8.10)
castle knights   unchanged: solo 15 HP, split trio, 2 rolls  decided §8.5
```

Chain check on the stat nudges: knight HP 20 puts the kill at
3×20 + 3×10 = **90 HP** (inside the 66–96 chain band) and gold 15 at **20 g**
(inside 15–24), so the nudges stay legal *because* the spawn trio is counted.
The current 15 / 12 already fit the chain bands too (75 HP, 17 g); the HP nudge
is about the member band and the "step up" feel, and it must not be paired with
a second trio buff.

**The Lord's swing.**  With decision §8.4 the flat fallback in
`src/battle/battle_telegraph_banked.c` (`b->enemy_incoming_dmg = 4`) becomes
**6**.  The old two-`SH2` answer now leaves 2 damage through, so the puzzle
becomes three `SH2` (3 × 2 = 6) or one steel shield (`S SH`, power 6) — which
is why a shield has to be purchasable (§8.10).  The Lord is deckless, so this
constant is the whole of its offense and the band no longer needs an exception.

### 5.3 Where the numbers live

Rank does **not** generate stats at runtime: the actor row already carries
the HP/damage the battle uses, and computing stats in the ROM would add
code (and any 8-bit multiply would pull `__mulsuchar` into the fixed bank,
`AGENTS.md` §52.18) for no benefit.  The band table lives in tooling:

* `screens/enemy_types/<id>.json` gains `"rank": n` (editor-authored);
* a host-side audit (`tools/enemy_audit.py`, `make enemy-ranks`) checks
  every enemy type's `hp`/`max_hp`/`gold_reward` against the band and every
  `BattleId`'s deck swings/riders against the band of the rank that battle
  resolves to, and fails loudly with expected/actual — same shape as
  `validate.py` and `card-costs`.  It also checks **both** number sources
  the battles actually read: the JSON type defaults *and* every actor-row
  placement in `levels/` + the test fixtures (HP/gold against the member
  band, or the solo band for solo placements; the wall-embedded
  `wall_slime` test double is the one exempt row).  The Lord's deckless
  fallback swing and the loot cut/gate tables are pinned the same way
  (partition shape, strictly increasing expected material, Appendix A
  parity), so `loot_rank_boss` can cite the audit for the R6 shape it
  cannot roll for itself;
* the same audit must know about **split chains** (§1.6): it reads the
  `EVENT_ACTION_SPAWN_SLIME_TRIO` row, adds the spawned trio's HP/gold to the
  owning enemy's totals, checks the sum instead of the single battle, and
  checks that the constants hardcoded in `src/game/slime_trio_spawn_banked.c`
  (HP 10, gold 5, name `SLIME`, `ow 9`) still match the slime's content row
  and `screens/enemy_types/slime.json`.  If a second splitting enemy is ever
  added, the action type — not the entity id — is what the audit keys on;
* the deck tables stay hand-authored C in `src/battle/enemy_deck_content.c`
  for now.  Moving them into the enemy JSON and generating the file is a
  follow-up only if the audit proves too weak (decks are shared by battle
  id, and the trio deck has no owning enemy type, so the move is more than
  mechanical).

---

## 6. Implementation plan

Implemented as staged (each phase independently testable and reversible).
What follows is the plan as executed.

### Phase 0 — data model (no gameplay change)

1. `screens/schema/enemy_type.schema.json`: add `rank` (integer 0–6, required;
   0 for non-combat types).
2. `screens/enemy_types/*.json`: add the ranks from §4.2.
3. `tools/level_editor/src/EnemyManager.tsx`: rank selector with the names from
   §4.1 (the editor is the source of content truth; the rank must be editable
   there, not in JSON by hand).  The selector is tooling — the rank never
   reaches a player-facing screen (§8.6).
4. `tools/screen_compiler/battle_compile.py`:
   * validate `rank` (range, required);
   * emit `rank` into each `EnemyTypeDef` row (`src/game/battle_types.c`,
     bank 4 — +1 byte per type, 14 B total);
   * emit `src/game/loot_rank_content.c` (`#pragma bank 9`) with
     `const uint8_t g_enemy_rank_by_battle[N]`, built from the enemy types plus
     one authored mapping table for battle ids with no owning enemy type
     (`BATTLE_SLIME_TRIO → slime`, `BATTLE_CARL → 0`, `BATTLE_NONE →
     slime_lord`).  The mapping mirrors `game_battle_enemy_type_id()` in
     `src/game/enemy_art_content.c`; when that hand-written switch is replaced
     by compiler output, both should come from one table.
5. `src/battle/battle_data.h`: append `uint8_t rank;` to `EnemyTypeDef` (last
   field, so the existing generated offsets stay readable).
6. Makefile: build the new bank-9 object (debug and release in sync).
7. `make memmap`; update `docs/loot.md` §17 to point at this document.

Phase 0 ships a ROM that behaves exactly like today (rank is data nobody reads
yet) — a clean baseline for the loot change.

### Phase 1 — rank-only loot

1. `src/game/loot_drop_banked.c`:
   * replace `s_ap_cut` + `s_ap_cap` with `s_rank_cut[7][5]` (§Appendix A);
   * `row = g_enemy_rank_by_battle[battle_type]`; write `CARD_NONE` for rank 0
     and return;
   * delete the pool read (`VARIABLE_ID_ENERGY_POOL`), the unset-pool
     normalization and the `if (pool > 255)` clamp — the body no longer needs
     `BATTLE_ENERGY_PER_TURN`, `game.h` or `battle.h` for this path;
   * delete the `LOOT_AP_MYTHRIL_RIDER` condition (the equip gate covers it);
   * rider gate = `s_rank_gate[rank]`;
   * **keep the draw count identical** (1 warm-up + weapon + material, bat = 2)
     so the stream structure stays stable and only the table mapping changes.
2. Battle victory reveal: confirm a `CARD_NONE` drop shows no reveal card
   (`battle_screen.c` already gates on `g_loot_id != CARD_NONE`; the
   `loot_rank_carl_no_drop` scenario pins it).
3. Telemetry: `LOOT_CARD_ADDED` gains the rank as `d2` (the host prints
   `rank=`); document it in `docs/DEBUG_PROTOCOL.md`.  The derived id already
   identifies material/effect/weapon, so no new id encoding.
4. Scenarios (new; seeds pinned through the harness):
   * `loot_rank_slime` — R1: material never above the R1 tail, `I F SW`
     unlikely, id pinned for the seed;
   * `loot_rank_knight` — R4: iron-or-steel is the dominant outcome; pin the
     exact id;
   * `loot_rank_same_at_any_pool` — the same knight + seed at pool 2 and pool 5
     yields the **same card id** (the point of §8.1: AP no longer shapes
     drops);
   * `loot_rank_knight_chain` — kill the knight, clear the spawned trio: two
     `LOOT_CARD_ADDED` events, R4-shaped then R1-shaped, six draws consumed.
     Keep `battle_slime_knight_split` / `battle_slime_trio_spawn` green;
   * `loot_rank_mimic` — R5: steel-heavy (assert the material band);
   * `loot_rank_boss` — R6: steel/mythril only;
   * `loot_rank_carl_no_drop` — rank 0 grants no card and no gold.
   Re-pin the existing loot scenarios whose expectations move:
   `loot_ap_steel`, `loot_ap_iron_cap`, `loot_ap_bronze*`, `loot_drop_slime`,
   `loot_drop_victory`, `card_ap_loot_gate*`, `loot_rider_strip*`,
   `killing_blow_loot_intact`, `loot_reveal_*` (the AP-named ones now mean
   "pool staged, drop unaffected").
5. `make memmap`, `make test-sentinels` (§52.19), then the touched scenarios;
   the full `make validate` only at the end of the work.

### Phase 1b — the equip gate (decision §8.1)

Without this phase a high-rank drop is a card the player can deck but can never
pay for, so it is a dead deck slot instead of a goal.

1. `src/rpg/deck.{h,c}`: add `deck_equip_card(CardState *cs, CardId id,
   uint8_t pool)` returning an outcome (`EQUIP_OK`, `EQUIP_FULL`,
   `EQUIP_MIN`, `EQUIP_LOCKED`, `EQUIP_REJECTED`).  It applies the existing
   `deck_add_card` rules plus one new one: **a loot-derived id
   (`loot_is_loot_id(id)`) whose `card_get_def(id)->cost` exceeds `pool` cannot
   be decked.**  `deck_add_card` itself stays ungated, because authored grant
   paths — `game_new_game`'s starter deck (plain bronze swords, live at
   pool 1), shop grants and scenario setup — are written
   against the pool they appear at.  Only player-chosen equipping is gated.
2. `src/screens/item_screen.c`: the A action calls `deck_equip_card(...)` with
   the live pool (already in `g->state.variables`), maps `EQUIP_LOCKED` to a new
   `MSG_NEED_AP`, and keeps the existing `DECK FULL` / `DECK MIN` messages.
3. `src/screens/item_content_banked.c`: draw `NEED AP n` on row 16 by reading
   the selected card's `cost` at draw time — **no new `Game` field**, so no WRAM
   layout shift (`AGENTS.md` §52.24).  In the list, the AP column renders
   locked rows distinctly (e.g. `!` + digit) so the player sees the lock before
   pressing A.
4. Harness: the `deck add` debug action goes through `deck_equip_card` too, so
   scenarios exercise the real gate (`AGENTS.md` §44).
5. Telemetry: `EVENT_CARD_EQUIP_LOCKED` with `d0 = card id`,
   `d1 = required AP`; document in `docs/DEBUG_PROTOCOL.md`.  No
   `CARD_ADDED_TO_DECK` fires on a locked attempt.
6. Scenario `deck_equip_locked_loot`: pool 2 with an `S F SW` (cost 4) in the
   collection → A rejected, `NEED AP 4` message, `CARD_EQUIP_LOCKED` emitted,
   no `CARD_ADDED_TO_DECK`; raise the pool to 4 and the same A succeeds.
7. Fixed-bank budget: this adds a compare, a call and a message id to `_HOME`.
   Run `make memmap` and watch for `Possible overflow` (§52.18).  If the debug
   fixed bank is too tight, keep the whole check inside `deck.c` as a bare
   `def->cost > pool` compare and let the screen set the existing message byte —
   no new state either way.

### Phase 2 — stat bands

1. `tools/enemy_audit.py` + `make enemy-ranks` implementing §5.2's checks (reads
   the JSONs and the deck tables; prints expected/actual and exits non-zero on a
   violation).  Wire it into `validate`.  It carries an explicit **exception
   list** (the castle knights, §8.5) so a deliberate departure is visible in
   the tool rather than silently passing.
2. Apply the content changes from §5.2: decks, knight HP/gold, mimic gold 20,
   Carl rank 0 with no drop and no gold, Lord swing 4 → 6, and a purchasable
   shield.  Each is a `src/game/` content change, not an engine change
   (`AGENTS.md` §55.1).  The knight's HP/gold nudge is validated against the
   **chain** totals (knight battle + spawn trio, §1.6), not the single battle.
3. Re-run the battle-heavy sentinels and the walkthrough: HP changes alter
   battle lengths, and `verify-walkthrough` asserts party HP and gold across the
   Carl spar and the field fights — Carl paying 0 gold and the Lord hitting for
   6 both touch expected values there.

### Phase 3 — later (explicitly not now)

* Rank-gated **archetype** pools (e.g. rings only from rank ≥ 3, more bows from
  rank ≥ 4).  Phase 1 keeps the fixed 25% archetype mix; changing it is a
  second, independent balance lever and would need its own scenario matrix.
* Area/luck modifiers and procedural rank drift (a rank-2 enemy that spawns as
  rank 3 in a later area).  The table makes this easy later: a rank is an
  index.
* **Not** rank in the UI (decision §8.6): no name-plate colour, no "VETERAN"
  tag.  The player's rank vocabulary is the card AP and the `NEED AP n` gate.

### Constraints checklist (must hold)

* **Fixed bank: at most a compare.**  The drop wrapper already exists and all
  loot logic is bank 9; Phase 1b's gate (a compare + a message id) is the only
  fixed-bank addition.  `make memmap` must stay green and the link must not
  print `Possible overflow` (`AGENTS.md` §52.18/§55.5).
* **No WRAM growth.** The rank table lives in ROM (bank 9); the battle→rank
  byte is read from ROM; the gate reads the pool through the screen's existing
  state pointer and derives the digit at draw time.  A WRAM layout shift is the
  documented cause of the `battle_boot` ghost-input regression
  (`AGENTS.md` §52.24) — do not add a global for this.
  Status (Nov 2026): held for the gate itself.  Later bundle growth lives
  elsewhere and is accounted at its own site: the 50-row collection (+76 B
  `GameState`, `src/rpg/deck.h`) and the dog-trade staging byte (+1 B
  `g_bk_byte_e`, `src/core/banked.h`).
* **No multiply/divide, no function pointers** in the roll (§52.1, §52.18);
  the walk and the compares are compare-only.
* **No `di`/`ei`** in the banked body (§52.11.3); the drop body is pure WRAM +
  same-bank reads today and stays that way.
* **Determinism:** same seed + same state ⇒ same id, and now *independently of
  the pool* — which is also the regression the new scenario pins.
* **Memory:** bank 4 +14 B (enemy rows), bank 9 ≈ +44 B (7×5 cut rows, 7 gate
  bytes, 7 battle→rank bytes) and −(the 5×5 `s_ap_cut` + `s_ap_cap` it
  replaces).  Release bank 9 has ~5.4 KB free; bank 4 is the fixture bank in
  debug and must be checked.
* **Engine purity:** rank is game content.  The engine (`battle.c`) still only
  passes the `BattleId` through `game_loot_drop`; no engine file learns about
  ranks and none branches on one.

## 7. Alternatives considered

1. **"The better ladder wins, AP still caps."**  The first recommendation: rows
   frozen by expected material, `row = max(ap_row, rank_row)`, then clamp to the
   AP cap.  Rejected by decision §8.1.  It cannot express the intended
   motivation: at AP 2 a slime knight's steel/mythril mass would be clamped down
   onto iron, so a knight met early is *the same loot as a slime* — rank would
   only matter after the story had already raised AP, which is the situation the
   proposal set out to fix.  The chosen design costs one thing the cap got for
   free: a locked card in the collection, and therefore a gate in the equip path
   (§4.8 / Phase 1b).
2. **Pure rank shape with no equip gate.**  Simplest possible rule (just the
   roll change), but a 6-AP card decked at pool 1 is a deck slot that can never
   be paid for — battle selection rejects it every turn and the player has to
   work out why.  Rejected: the gate names the reason (`NEED AP 4`) instead of
   hiding it, which is what makes the locked drop motivating rather than
   confusing.
3. **Rank as extra AP rows** (`row = ap_row + rank`).  Trivial, but it cannot be
   *centered* on a material (the shape is always monotone toward the top), which
   is the stated goal.  Rejected.
4. **Weighted blending of an AP table and a rank table.**  Mathematically
   nicest, but it needs an 8-bit multiply (pulls `__mulsuchar` into the fixed
   bank, §52.18) and a renormalizing divide.  Rejected for ROM cost.
5. **Explicit card rarity (common/rare/epic) instead of material.**  Matches the
   earlier `docs/loot.md` §6 vocabulary, but would need a second axis in the id
   space and a duplicated definition of quality.  Material already *is* the
   quality ladder; rank composes with it instead of duplicating it.

## 8. Decisions (resolved 2026-11-04)

| # | Question | Decision | Where it lands |
|---|---|---|---|
| 1 | AP vs rank precedence | **Rank wins; AP only unlocks.**  The roll is rank-only; a card above the pool cannot be equipped until the pool grows. | §4.3, Phase 1, Phase 1b |
| 2 | Rank 0 loot | **No loot** for Carl's spar and lesson fights (and no gold), so a spar can no longer consume the first-victory bronze shield. | §5.1, §5.2, Phase 2 |
| 3 | Mimic gold | **Rank 5 earns gold too** (band 15–25; recommend 20).  The AP story grant stays a separate reward. | §5.1, §5.2 |
| 4 | Lord's flat swing | **Raise 4 → 6.**  The answer becomes three `SH2` or one steel shield, and a shield is added to the merchant's stock so preparation is always possible.  No band exception needed. | §5.2, Phase 2 |
| 5 | Solo castle knights | **Leave them alone.**  They are slime knights and they split into three; the split is the encounter, not the HP number.  Recorded as an explicit audit exception. | §5.1, §5.2, Phase 2 |
| 6 | Ranks in the player UI | **No.**  Rank stays debug/tooling vocabulary; the player sees card AP and the `NEED AP n` gate, nothing rank-shaped. | Phase 0, Phase 3 |
| 7 | Knight chain double roll | **Keep it** (R4 + R1).  The second roll is fodder quality, matching the second fight. | §4.7 |

Remaining open questions (none blocks Phase 0/1):

8. **Fodder tail.**  With no AP cap, R1 can drop iron (12.1%), and at the
   starting pool a slime can hand over bronze (30.1%).  Is that "rewards
   exploring" or "the tutorial enemy is too generous"?  A one-row shift — e.g.
   `R1 { 166, 236, 253, 255, 255 }` → wood 65.2% / bronze 27.3% / iron 6.6% /
   steel 0.8% / mythril 0% — halves the iron tail without touching another row.
9. **Mimic gold value.**  20 is the proposal (top of 15–25, because the mimic is
   a solo set-piece); 18 is the conservative pick.
10. **Which shield the merchant stocks.**  The existing `CARD_WOODEN_SHIELD`
    (owned 4× at new game, so it only helps a player who sold them) or a new
    `S SH` catalog card (power 6, AP 3, ~12 g) that answers the Lord's swing
    alone.  The second is a `g_cards` row plus a `CARD_` id — content, not
    engine.
11. **Does the gate apply to shop purchases?**  Proposal: no.  Buying is
    allowed, equipping is gated; a locked card sitting in the collection is the
    motivation to raise AP, and refusing the sale would hide it.

## Appendix A — exact cut tables and reproduction notes

Rank cut rows (as they would appear in `src/game/loot_drop_banked.c`):

```c
static const uint8_t s_rank_cut[7][5] = {
    {   0,   0,   0,   0,   0 },   /* R0 LESSON   (never rolled) */
    { 139, 216, 247, 253, 255 },   /* R1 FODDER   */
    {  88, 185, 242, 253, 255 },   /* R2 VERMIN   */
    {  50, 139, 216, 247, 255 },   /* R3 GRUNT    */
    {  24,  75, 178, 242, 255 },   /* R4 VETERAN  */
    {   4,  24,  88, 216, 255 },   /* R5 ELITE    */
    {   0,  11,  63, 178, 255 }    /* R6 BOSS     */
};

static const uint8_t s_rank_gate[7] = { 64, 64, 80, 96, 112, 128, 128 };
```

`s_ap_cut` and `s_ap_cap` disappear from the body (the AP row is no longer
read), and the index maps from the earlier draft (`s_ap_ladder`,
`s_rank_ladder`) go with them — the rank *is* the row index now.

Sanity checks a reader (or a future test) can redo by hand:

* each row's percentages sum to 100% (±0.4 for 1/256 quantization);
* rows are strictly increasing in expected material (0.65 → 3.00), which is what
  makes "rank = quality" a comparison rather than a label;
* a row with `255` in a slot can never advance past that tier (the walk compares
  `mat_roll > cut`, and `mat_roll ≤ 255`);
* `P(I F BO) = P(bow | bat) · P(iron | row) · (gate/256) · 0.5
  = 1.0 · P(iron | R2) · gate/256` (bats are R2-only, so the row is R2);
* card AP = `1 + {wood 0, bronze 0, iron 1, steel 2, mythril 4} + (rider ? 1 :
  0)`, the same for every weapon (the sword/bow +1 power is absorbed by the
  `/8` ceiling step);
* a row's wearable share at pool `p` is the probability mass of (material,
  rider) pairs with `AP ≤ p`, which reproduces the §4.3 table.
