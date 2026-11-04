# Enemy Ranking & Rank-Driven Loot

**Status:** proposal, nothing implemented. Written 2026-11-04 on branch
`balancing`.  Code references are to the current tree; numbers in §3–§6 are
computed exactly from the 1/256 roll tables (not sampled).

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
name (`W SW`, `I F SW`, `M P DA`), power, AP cost, rider/status, ring uses,
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
`eff_roll < 64` (**25%**); within that, swords get fire for
`eff_roll < 32` and ice otherwise (12.5% each), and daggers get poison
(25%).  Wood is always plain, shields/bows/rings can never carry a rider,
and a *mythril* rider is withheld until the pool reaches
`LOOT_AP_MYTHRIL_RIDER` (6), because a mythril rider card costs 6 AP
(`docs/card-ap-cost-formula.md` §6.1).  At AP 5 mythril is therefore always
plain.

**Weapon — fixed archetype mix.**  Non-bat enemies pick sword/shield/
dagger/ring at 25% each.  Bats always drop a **bow**; the bow is otherwise
only purchasable (the merchant's `M BO`).

### 1.4 Two fixed grants on top of the roll

* **First victory ever** (`MONSTERS_DEFEATED == 0`) is overwritten with
  `B BRONZE SHIELD` so the opening loot teaches blocking.  Rolls still
  happen first, so the stream keeps its shape; this is rank-independent by
  design.
* The **mimic** pays AP through its story event instead of gold; its loot
  roll is a normal roll.

### 1.5 What the drop is worth

Derived from the same material band (`LOOT_POWER` in `src/rpg/loot.h`):

```text
wood 1, bronze 2-3, iron 4-5, steel 6-7, mythril 8-9
sword and bow take the high end of the band, shield/dagger/ring the low end
```

Synthesized **sell price** = `power + 2·material`, `+2` for a poison dagger
(`docs/loot.md` §16/§34.6).  **Ring uses** = `material + 1`.  **AP cost** is
derived by `docs/card-ap-cost-formula.md`:

```text
value = power + 4·material + (mythril ? 12 : 0) + (rider ? 8 : 0)
AP    = ceil(value / 8)                       /* 1..6 */
```

---

## 2. How weapons are ranked today

There is no explicit rarity or weapon level.  A card's rank falls out of
four orthogonal properties, and the proposal must not duplicate them — the
enemy rank is a *fifth* input to the roll, not a replacement for any of
these.

### 2.1 The quality ladder is the material

Material is the only purely vertical axis: for a given weapon, each tier
step adds 2 power (wood 1; bronze 2–3; iron 4–5; steel 6–7; mythril 8–9),
with sword/bow at the top of each band and shield/dagger/ring at the bottom.

### 2.2 The tempo ladder is the AP cost

Material is *also* the dominant term in the AP cost (4 points per tier, and
mythril carries a 12-point premium).  This is why the material ladder and
the tempo ladder mostly agree but deliberately not always — a mythril bow
costs 5 AP and sells for 17 g, a mythril fire sword costs 6 AP and sells
for the same 17 g.

### 2.3 The rider ladder

A rider is worth a whole AP step (8 points) and is only legal on some
weapons (§34.2): fire/ice on swords, poison on daggers, nothing on bows,
shields or rings.  Riders are therefore a quality signal that is *not*
monotone in material: a bronze fire sword (2 AP) beats a bronze plain sword
(1 AP) but not an iron plain sword (2 AP).

### 2.4 The archetype ladder

| Weapon | Role | Band position | Riders | Note |
|---|---|---|---|---|
| Sword | damage | high end | fire, ice | the main attack card |
| Dagger | damage | low end | poison | defense-phase counterattack line |
| Bow | damage | high end | none | drop source today: **bats only** (plus merchant) |
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
| `B F SW` | 3 | 2 | 5 g | – |
| `I F SW` | 5 | 3 | 9 g | – |
| `S F SW` | 7 | 4 | 13 g | – |
| `M F SW` | 9 | 6 | 17 g | – |
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

### 3.1 `I F SW` (the iron fire sword) — the worked example

The probability chain for one specific card:

```text
P(I F SW) = P(sword) × P(iron) × P(rider rolled) × P(fire | rider rolled)
          = 0.25     × P(iron) × 0.25            × 0.5
          = 0.03125  × P(iron)
```

| AP | P(iron) | P(`I F SW`) | ≈ 1 in |
|---|---|---|---|
| 1 | – | 0% | – |
| 2 | – | 0% | – |
| 3 | – | 0% | – |
| 4 | 19.9% | **0.62%** | 161 encounters |
| 5+ | 25.0% | **0.78%** | 128 encounters |

### 3.2 Related cards, same AP rows

| AP | `B SW` | `I SW` | `I F SW` | `S F SW` | `M F SW` |
|---|---|---|---|---|---|
| 2 | 3.74% | – | – | – | – |
| 3 | 6.52% | – | – | – | – |
| 4 | 5.57% | 3.74% | 0.62% | – | – |
| 5+ | 5.64% | 4.69% | 0.78% | 0.24% | 0.06% |

Two things to notice:

* `S F SW` and `M F SW` are *far* rarer than `S SW` (1.46% at AP 5) and
  `M SW` (0.37%) because riders are only 25% of rolls.  A steel plain sword
  is 1.46%, a steel fire sword 0.24%: **6×**.
* Because the material roll is uniform-ish over 256 values, "rare tier"
  currently means "a few percent", not "a chase item".

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
elite (and pays its AP through the story instead of gold); the Lord is the
boss.*

### 4.2 Rank against current content

| Enemy | BattleId | HP (actor row) | Swing powers | Riders | Gold | Rank |
|---|---|---|---|---|---|---|
| Slime | `BATTLE_SLIME` | 10 | 2, 2, 2, heal 2 | – | 5 | **R1** |
| Slime trio (Mountain Pass) | `BATTLE_SLIME_TRIO` | 10 | 2, 2, 2, 2, heal 2 | – | 5 | **R1** |
| Bat | `BATTLE_BAT` | 8 | 2, 3, 4, 3, 3 | poison 60/255 | 8 | **R2** |
| Spider | `BATTLE_SPIDER` | 10 | 2, 3, 3, heal 2, 3 | poison 70/255 | 8 | **R2** |
| Kobold | `BATTLE_KOBOLD` | 15 | 2, 2, 3, heal 2 | – | 8 | **R3** |
| Slime Knight | `BATTLE_SLIME_KNIGHT` | 15 | 3, 3, 4, heal 2 | – | 12 | **R4** |
| Mimic | `BATTLE_MIMIC` | 50 (solo) | 4, 4, 5, 3, heal 3 | – | 0 (AP) | **R5** |
| Lord of Slimes | `BATTLE_NONE` (solo) | 70 (solo) | flat 4 (no deck) | – | 50 | **R6** |
| Carl's spar | `BATTLE_CARL` | 30 | 0 | – | 5 | **R0** |

Note the duplicate HP sources: the battle uses the **actor row** from
`src/game/actors_content.c` (Lord 70), while `screens/enemy_types/*.json`
carries its own `hp` (Lord 99) that only the editor and tooling read.  The
rank audit in §5.2 must check both until the duplication is removed.

Three castle slime knights are also flagged `solo` (one 15 HP enemy
instead of three); see §5.2 for why that is a finding.

### 4.3 How rank enters the loot roll

Rank does not replace AP.  The two are separate knobs with separate jobs:

* **AP = ceiling.**  Raising the pool unlocks higher tiers and is the
  story-gated progression lever the demo already ships.  It must not be
  weakened: at AP 2 nothing above bronze may drop, no matter the enemy.
* **Rank = shape.**  It decides *where inside the allowed range the mass
  sits* — peaked on a material, per §4.1's "loot centre".

Recommended rule, five lines of bank-9 code:

```text
ap_row   = ladder index of the live AP (1→0, 2→1, 3→2, 4→4, 5+→6)
rank_row = ladder index of the rank (0→0, 1→3, 2→5, 3→7, 4→8, 5→9, 6→10)
row      = max(ap_row, rank_row)          /* the better of the two ladders */
material = walk cut[row] with mat_roll
if (material > ap_cap) material = ap_cap  /* preserve the AP unlock contract */
```

**Why a single ordered ladder.**  The rows are frozen in one order by
expected material tier, so `max()` is meaningful and quality is monotone in
both knobs:

| idx | Row | wood | bronze | iron | steel | mythril | E[mat] |
|---|---|---|---|---|---|---|---|
| 0 | AP 1 | 100% | – | – | – | – | 0.00 |
| 1 | AP 2 | 80.1% | 19.9% | – | – | – | 0.20 |
| 2 | AP 3 | 65.2% | 34.8% | – | – | – | 0.35 |
| 3 | **R1** | 54.7% | 30.1% | 12.1% | 2.3% | 0.8% | 0.64 |
| 4 | AP 4 | 50.4% | 29.7% | 19.9% | – | – | 0.70 |
| 5 | **R2** | 34.8% | 37.9% | 22.3% | 4.3% | 0.8% | 0.98 |
| 6 | AP 5+ | 35.2% | 30.1% | 25.0% | 7.8% | 2.0% | 1.11 |
| 7 | **R3** | 19.9% | 34.8% | 30.1% | 12.1% | 3.1% | 1.44 |
| 8 | **R4** | 9.8% | 19.9% | **40.2%** | 25.0% | 5.1% | 1.96 |
| 9 | **R5** | 2.0% | 7.8% | 25.0% | **50.0%** | 15.2% | 2.69 |
| 10 | **R6** | 0.4% | 4.3% | 20.3% | 44.9% | **30.1%** | 3.00 |

Cut rows in 1/256 form (the ROM representation; `s_ap_cut` rows are
unchanged and simply move to indices 0/1/2/4/6):

```text
R1  { 139, 216, 247, 253, 255 }
R2  {  88, 185, 242, 253, 255 }
R3  {  50, 139, 216, 247, 255 }
R4  {  24,  75, 178, 242, 255 }
R5  {   4,  24,  88, 216, 255 }
R6  {   0,  11,  63, 178, 255 }
```

The percentages above are the realized 1/256 outcomes, not the design
targets (quantization moves things by up to ~0.5 points).

**Rider gate by rank.**  Rank also raises the chance of a rider (it is a
quality signal that does not shift material), one step per rank:

| Rank | `eff_roll` gate | P(rider) | fire (sword) | ice (sword) | poison (dagger) |
|---|---|---|---|---|---|
| 0–1 | 64 | 25% | 12.5% | 12.5% | 25% |
| 2 | 80 | 31.25% | 15.6% | 15.6% | 31.25% |
| 3 | 96 | 37.5% | 18.75% | 18.75% | 37.5% |
| 4 | 112 | 43.75% | 21.9% | 21.9% | 43.75% |
| 5–6 | 128 | 50% | 25.0% | 25.0% | 50% |

The fire/ice figures are conditional on a sword and material ≥ bronze
(the only legal pairing); the poison figure is conditional on a dagger.
Unconditional per-drop chances are those numbers × 25% (the sword or
dagger share of the weapon roll).

Unchanged: wood is still always plain, bows/shields/rings never carry a
rider, and the mythril-rider hold-back at AP < 6 stays.

**Rank 0 means no drop.**  `game_loot_drop` returns `CARD_NONE` (0), which
both `world_on_battle_end` and the victory reveal in `battle_screen.c`
already treat as "no card".  Gold is not part of the drop path, so rank 0
no-gold is a content change (Carl's actor row), not engine logic.  Today a
won spar against Carl rolls loot and can even consume the first-victory
bronze shield grant, which is a lesson fight paying out like a real one.

### 4.4 What changes, concretely

Relative to today, at a given AP:

* **slime (R1)**: unchanged in shape once AP ≥ 4 (the AP row outranks it);
  at AP 2–3 it is better than today (45.3% bronze instead of 20–35%),
  because R1 sits above the AP 2/3 rows.
* **spider (R2)**: better from AP 3 on; ~27.3% iron once AP 4 allows it.
* **kobold (R3)**: a clear upgrade — 30.1% iron at full shape, 45.3%
  iron-or-steel at AP 4 (vs 19.9% today).
* **slime knight (R4)**: the headline case — **70.3% iron-or-steel at AP 4**
  and 65.2% at AP 5, versus 19.9% / 32.8% for a slime.  Expected sale value
  6.9 / 8.3 g against 3.5 / 5.1 g today.
* **mimic (R5)**: centered on steel (50%), with 15.2% mythril.
* **Lord (R6)**: centered on steel, 30.1% mythril — the only reliable
  mythril source.

### 4.5 `I F SW` under ranks

`P(I F SW) = 0.25 × P(iron) × gate/512`:

| Rank | AP 1 | AP 2 | AP 3 | AP 4 | AP 5 |
|---|---|---|---|---|---|
| R1 (slime) | 0% | 0% | 0% | 0.62% | 0.78% |
| R2 (spider) | 0% | 0% | 0% | 1.07% | 0.98% |
| R3 (kobold) | 0% | 0% | 0% | 2.12% | 1.41% |
| R4 (slime knight) | 0% | 0% | 0% | **3.85%** | **2.20%** |
| R5 (mimic) | 0% | 0% | 0% | 5.64% | 1.56% |
| R6 (Lord) | 0% | 0% | 0% | 5.96% | 1.27% |

The AP 4 column being *higher* than AP 5 for high ranks is not a bug: at
AP 4 the steel/mythril rolls are clamped down onto iron (the ceiling), so
"iron fire sword" is more likely precisely because better cards are
impossible.  At AP 5 the same mass spreads into steel and mythril
(`S F SW`: 1.37% from a knight, 3.12% from a mimic, 2.81% from the Lord).

### 4.6 Per-enemy spot check (plausible AP for where the enemy appears)

| Enemy | Rank | AP | `B SW` | `I SW` | `I F SW` | P(iron+) | P(iron or steel) | E[sell] |
|---|---|---|---|---|---|---|---|---|
| slime | R1 | 2 | 8.50% | – | – | 0% | 0% | 2.6 g |
| spider | R2 | 3 | 11.21% | – | – | 0% | 0% | 3.2 g |
| kobold | R3 | 4 | 5.43% | 7.08% | 2.12% | 45.3% | 45.3% | 5.5 g |
| slime knight | R4 | 4 | 2.80% | 9.89% | 3.85% | 70.3% | 70.3% | 6.9 g |
| slime knight | R4 | 5 | 2.80% | 5.66% | 2.20% | 70.3%* | 65.2% | 8.3 g |
| mimic | R5 | 5 | 0.98% | 3.12% | 1.56% | 90.2% | 75.0% | 11.1 g |
| Lord | R6 | 5 | 0.54% | 2.54% | 1.27% | 95.3% | 65.2% | 12.4 g |

`P(iron+)` counts iron + steel + mythril; the "iron or steel" column
excludes mythril.  The two coincide whenever the AP cap clamps the
steel/mythril mass down onto iron (AP 4); at AP 5 the knight keeps its
5.1% mythril outside the "iron or steel" figure.  Bats substitute the bow
for every weapon roll, so a bat at AP 2 drops a bronze bow 65.2% of the
time (R2 outranks AP 2, and the AP cap pulls the steel/mythril mass down
onto bronze).

At full shape (the rank row actually outranks the AP row) the expected
sell values are **3.4 / 4.6 / 6.3 / 8.3 / 11.1 / 12.4 g** for R1–R6, to be
compared with the flat **5.1 g** every enemy offers at AP 5 today.  The
low-rank rows (R1 3.4 g, R2 4.6 g) only apply while AP is below them —
from AP 5 on, the AP row wins and a slime keeps today's 5.1 g.  Rank never
makes a drop worse than the pre-rank game.

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
| 0 | scripted (Carl 30) | – | 0 | – | 0 |
| 1 | 6–10 | 18–24 | 2–3 | 0 | 3–6 |
| 2 | 8–12 | 24–32 | 2–4 | ≤ 1 | 6–10 |
| 3 | 14–18 | 36–48 | 3–4 | ≤ 1 | 8–12 |
| 4 | 16–22 | 48–64 | 4–5 | ≤ 2 | 12–18 |
| 5 | 30–40 | 50–65 | 5–6 | ≤ 2 | 0 (AP) or 15–25 |
| 6 | – (solo only) | 70–99 | 5–8 | ≤ 2 | 40–60 |

Rules behind the table:

* the trio is 3 × the member, so the "trio member" column is what a normal
  encounter costs in total (rank 4 trio = 48–66 HP, versus 45 today);
* a solo build must be 2.5–3× a trio member or the solo flag turns a
  miniboss into a speed bump;
* **one enemy swings per round** (shared deck, one attacker), so the swing
  band is per round, not per enemy;
* gold is granted once per victory, so the gold band is per encounter, not
  per clone;
* "rider cards" counts poison/burn/freeze entries in the deck, not the
  chance: the chance stays authored (60–70/255 style).

### 5.2 Current content audit

| Item | Current | Band | Verdict |
|---|---|---|---|
| slime HP 10, deck 2, gold 5 | R1 | 6–10 / 2–3 / 3–6 | passes |
| slime trio deck 2 | R1 | 2–3 | passes |
| bat HP 8, deck 2–4 + poison, gold 8 | R2 | 8–12 / 2–4 / 6–10 | passes |
| spider HP 10, deck 2–3 + poison, gold 8 | R2 | 8–12 / 2–4 / 6–10 | passes (low end) |
| kobold HP 15, gold 8 | R3 | 14–18 / 8–12 | HP and gold pass |
| **kobold deck 2, 2, 3** | R3 | **3–4** | **fails — below rank** |
| slime knight HP 15, gold 12 | R4 | 16–22 / 12–18 | **HP fails (15 < 16)** |
| **slime knight deck 3, 3, 4** | R4 | **4–5** | **fails — below rank** |
| mimic HP 50 (solo), gold 0 | R5 | 50–65 / 0 | passes |
| **mimic deck 4, 4, 5, 3** | R5 | **5–6** | **fails — the 3 and 4s are below rank** |
| Lord HP 70 (solo), gold 50 | R6 | 70–99 / 40–60 | passes |
| **Lord swing flat 4** | R6 | **5–8** | **fails, but it is the two-`SH2` shield puzzle** |
| **castle slime knights: solo, HP 15** | R4 solo | **48–64** | **fails — a solo miniboss weaker than a trio** |
| **Carl gold 5, spar rolls loot** | R0 | 0 / no drop | **fails — lesson fight pays out** |

Recommended content nudges (proposal, not applied here):

```text
kobold deck      {3,3,4, heal 3}                 (was 2,2,3, heal 2)
slime knight hp  20   (was 15)   gold 15 (was 12)
slime knight deck {4,4,5, heal 3}                (was 3,3,4, heal 2)
mimic deck       {5,5,6, 4, heal 4}              (was 4,4,5,3, heal 3)
castle solo knights  hp 45-50, or drop the solo flag (back to a trio)
Carl             rank 0 → no loot, gold 0
Lord swing       keep flat 4 (puzzle) as a documented R6 exception, or
                 raise to 6 and re-tune the intended shield count
```

The Lord exception is the only place where the recommended band has to be
overridden by a puzzle: the enemy-strike telegraph (`src/battle/battle.c`
today; `src/battle/battle_telegraph_banked.c` on the working tree's battle
split) says two `SH2` shields block the flat 4 exactly, which is a designed
puzzle rather than a balance number.  Either keep the band's lower bound at 4 and document the
exception, or raise the swing and accept that the puzzle becomes "three
shields".  This is a **decision**, not an implementation detail.

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
  `validate.py` and `card-costs`;
* the deck tables stay hand-authored C in `src/battle/enemy_deck_content.c`
  for now.  Moving them into the enemy JSON and generating the file is a
  follow-up only if the audit proves too weak (decks are shared by battle
  id, and the trio deck has no owning enemy type, so the move is more than
  mechanical).

---

## 6. Implementation plan

No code is written yet.  The change is deliberately staged so each phase is
independently testable and reversible.

### Phase 0 — data model (no gameplay change)

1. `screens/schema/enemy_type.schema.json`: add `rank` (integer 0–6,
   required; 0 for non-combat types).
2. `screens/enemy_types/*.json`: add the ranks from §4.2.
3. `tools/level_editor/src/EnemyManager.tsx`: rank selector with the
   names from §4.1 (the editor is the source of content truth; the rank must
   be editable there, not in JSON by hand).
4. `tools/screen_compiler/battle_compile.py`:
   * validate `rank` (range, required);
   * emit `rank` into each `EnemyTypeDef` row (`src/game/battle_types.c`,
     bank 4 — +1 byte per type, 14 B total);
   * emit `src/game/loot_rank_content.c` (`#pragma bank 9`) with
     `const uint8_t g_enemy_rank_by_battle[N]`, built from the enemy types
     plus one authored mapping table for battle ids that have no owning
     enemy type (`BATTLE_SLIME_TRIO → slime`, `BATTLE_CARL → 0`,
     `BATTLE_NONE → slime_lord`).  The mapping mirrors
     `game_battle_enemy_type_id()` in `src/game/enemy_art_content.c`; when
     that hand-written switch is eventually replaced by compiler output,
     both should come from one table.
5. `src/battle/battle_data.h`: append `uint8_t rank;` to `EnemyTypeDef`
   (last field, so existing offsets in the generated table stay readable).
6. Makefile: build the new bank-9 object (and keep debug/release in sync).
7. `make memmap`; update `docs/loot.md` §17 to point at this document.

Phase 0 ships a ROM that behaves exactly like today (rank is data nobody
reads yet) — a clean baseline for the loot change.

### Phase 1 — rank-driven loot

1. `src/game/loot_drop_banked.c`:
   * replace `s_ap_cut` with the 11-row ladder (§4.3), keep the existing
     five AP rows byte-identical at indices 0/1/2/4/6;
   * add the two index maps (`ap → ladder`, `rank → ladder`) and
     `rank = g_enemy_rank_by_battle[battle_type]` (same bank 9: no
     trampoline, no fixed-bank cost);
   * `row = max(...)`, then the existing walk, then the AP cap compare;
   * rider gate = `s_rank_gate[rank]` (rank 0 uses 64);
   * rank 0 → `g_loot_id = CARD_NONE`;
   * **keep the draw count identical** (1 warm-up + weapon + material, bat
     = 2) so the stream structure stays stable and only the table mapping
     changes.
2. Battle victory reveal: confirm a `CARD_NONE` drop shows no reveal card
   (`battle_screen.c` already gates on `g_loot_id != CARD_NONE`; the
   `loot_rank_carl_no_drop` scenario pins the behavior).
3. Telemetry: `LOOT_CARD_ADDED` gains the rank as `d2` (the host prints
   `rank=`); document it in `docs/DEBUG_PROTOCOL.md`.  The derived id
   already identifies material/effect/weapon, so no new id encoding.
4. Scenarios (new, deterministic seeds pinned via the harness):
   * `loot_rank_slime_ap2` — AP 2, slime: material must be wood/bronze
     (cap) and never iron;
   * `loot_rank_knight_ap4` — AP 4, slime knight: iron-or-steel is the
     dominant outcome; pin the exact id for the chosen seed;
   * `loot_rank_cap_ap2` — AP 2, slime knight: no iron/steel/mythril
     (the AP gate survives rank);
   * `loot_rank_mimic` — mimic drop is steel-heavy and never wood/bronze
     beyond the low-percentage tail (assert the material band);
   * `loot_rank_boss` — Lord: steel/mythril only;
   * `loot_rank_carl_no_drop` — rank 0 grants no card and no gold.
   Re-pin the existing loot scenarios whose expected ids move:
   `loot_ap_steel`, `loot_ap_iron_cap`, `loot_ap_bronze*`,
   `loot_drop_slime`, `loot_drop_victory`, `card_ap_loot_gate*`,
   `loot_rider_strip*`, `killing_blow_loot_intact`, `loot_reveal_*`.
5. `make memmap`, `make test-sentinels` (§52.19), then the touched
   scenarios; full `make validate` only at the end of the work.

### Phase 2 — stat bands

1. `tools/enemy_audit.py` + `make enemy-ranks` implementing §5.2's checks
   (reads the JSONs and the deck tables; prints expected/actual and a
   non-zero exit on a violation).  Wire it into `validate`.
2. Apply the content nudges from §5.2 (decks, knight HP/gold, solo
   knights, Carl rank 0).  Each is a `src/game/` content change, not an
   engine change (`AGENTS.md` §55.1).
3. Re-run the battle-heavy sentinels and the walkthrough: HP changes alter
   battle lengths, and `verify-walkthrough` asserts party HP after the
   Carl spar and the field fights.

### Phase 3 — later (explicitly not now)

* Rank-gated **archetype** pools (e.g. rings only from rank ≥ 3, more bows
  from rank ≥ 4).  Phase 1 keeps the fixed 25% archetype mix; changing it
  is a second, independent balance lever and would need its own scenario
  matrix.
* Rank in the UI (name plate colour, a "VETERAN" tag).  Recommend not
  shipping the vocabulary to players yet.
* Area/luck modifiers and procedural rank drift (a rank-2 enemy that
  spawns as rank 3 in a later area).  The ladder makes this easy later:
  a rank is just an index.

### Constraints checklist (must hold)

* **No fixed-bank growth.** The wrapper already exists; all new logic is
  bank 9.  `make memmap` must stay green (`AGENTS.md` §52.18/§55.5).
* **No WRAM growth.** The rank table lives in ROM (bank 9); the battle→rank
  byte is read from ROM.  A WRAM layout shift is the documented cause of
  the `battle_boot` ghost-input regression (`AGENTS.md` §52.24) — do not
  add a global for this.
* **No multiply/divide, no function pointers** in the roll (`§52.1`,
  `§52.18`); the walk, `max`, and cap compare are compare-only.
* **No `di`/`ei`** in the banked body (`§52.11.3`); the drop body is pure
  WRAM + same-bank reads today and stays that way.
* **Determinism:** same seed + same state ⇒ same id.  Keep the draw
  count; new scenarios pin seeds explicitly.
* **Memory:** bank 4 +14 B (enemy rows), bank 9 ≈ +78 B (11×5 ladder,
  12 B index maps, 11 B battle→rank).  Release bank 9 has ~5.4 KB free;
  bank 4 is the fixture bank in debug and must be checked.
* **Engine purity:** rank is game content.  The engine (`battle.c`) still
  only passes the `BattleId` through `game_loot_drop`; no engine file
  learns about ranks, and no engine file branches on a rank.

---

## 7. Alternatives considered

1. **`row = max(AP row, rank row)` without the cap.**  Simplest possible
   rule, but a rank-4 enemy met at AP 2 would roll steel and mythril,
   breaking the AP unlock contract (`docs/loot.md` §34.5: "steel and
   mythril never drop below AP 5").  Rejected.
2. **Pure rank shape (no ladder, no AP row).**  `material = min(rank roll,
   AP cap)` only.  It gives the cleanest identity (a slime is always a
   wood/bronze source) but makes AP *punitive*: at AP 5 a slime would drop
   54.7% wood versus 35.2% today, i.e. the player's progression makes
   trash loot worse than before.  Rejected — but it is a legitimate
   design if we ever want "AP = ceiling only, grind high ranks".
3. **Rank as extra AP rows.**  Treat rank as a bonus to the AP row index
   (`row = ap_row + rank`).  Trivial, but it cannot be *centered* on a
   material (the shape is always "monotone toward the top"), which is the
   user's stated goal.  Rejected.
4. **Weighted blending of the AP table and the rank table.**  Mathematically
   nicest, but it needs an 8-bit multiply (pulls `__mulsuchar` into the
   fixed bank, `§52.18`) and a renormalizing divide.  Rejected for ROM
   cost; the ladder achieves the same monotonicity with compares.
5. **Explicit card rarity (common/rare/epic) instead of material.**
   Matches the earlier `docs/loot.md` §6 vocabulary, but would need a
   second axis in the id space and a duplicated definition of quality.
   Material already *is* the quality ladder; rank composes with it instead
   of duplicating it.

---

## 8. Decisions needed before Phase 1

1. **AP vs rank precedence** — accept the recommended "the better ladder
   wins, AP still caps" (AP 2 can never see iron even from a knight)?  The
   alternative is "rank wins, AP only unlocks" (§7.2).
2. **Rank 0 = no loot** for Carl's spar and other lesson fights?  (Today a
   won spar rolls loot and can consume the first-victory bronze shield.)
3. **Mimic gold stays 0** with the AP story grant, or does rank 5 earn
   gold too?
4. **The Lord's flat 4** — keep the two-`SH2` puzzle as a documented R6
   exception, or raise its swing to 6 and re-tune the shield count?
5. **Solo castle knights** — raise their HP to the R4 solo band (~45–50),
   or drop the `solo` flag and let them fight as a trio?
6. **Do ranks appear in the player-facing UI** (battle name plate, loot
   reveal) or stay debug/tooling vocabulary?

Answers here can also arrive as follow-up content edits; nothing in Phases
0–1 depends on 3–6 except the content audit's pass/fail list.

---

## Appendix A — exact cut tables and reproduction notes

Rank cut rows (as they would appear in `src/game/loot_drop_banked.c`):

```text
static const uint8_t s_ladder_cut[11][5] = {
    { 255,   0,   0,   0,   0 },   /* idx 0  AP 1        */
    { 204, 255,   0,   0,   0 },   /* idx 1  AP 2        */
    { 166, 255,   0,   0,   0 },   /* idx 2  AP 3        */
    { 139, 216, 247, 253, 255 },   /* idx 3  R1 FODDER   */
    { 128, 204, 255,   0,   0 },   /* idx 4  AP 4        */
    {  88, 185, 242, 253, 255 },   /* idx 5  R2 VERMIN   */
    {  89, 166, 230, 250, 255 },   /* idx 6  AP 5+       */
    {  50, 139, 216, 247, 255 },   /* idx 7  R3 GRUNT    */
    {  24,  75, 178, 242, 255 },   /* idx 8  R4 VETERAN  */
    {   4,  24,  88, 216, 255 },   /* idx 9  R5 ELITE    */
    {   0,  11,  63, 178, 255 }    /* idx 10 R6 BOSS     */
};

static const uint8_t s_ap_ladder[5]   = { 0, 1, 2, 4, 6 };   /* AP 1..5+ */
static const uint8_t s_rank_ladder[7] = { 0, 3, 5, 7, 8, 9, 10 }; /* R0..R6 */
static const uint8_t s_rank_gate[7]   = { 64, 64, 80, 96, 112, 128, 128 };
```

Sanity checks a reader (or a future test) can redo by hand:

* each row's percentages sum to 100% (±0.4 for 1/256 quantization);
* `s_ap_ladder` / `s_rank_ladder` indices are strictly increasing in
  expected material, which is what makes `max()` a quality comparison;
* a row with `255` in a slot can never advance past that tier (the walk
  compares `mat_roll > cut`, and `mat_roll ≤ 255`);
* `P(I F SW) = P(sword) · P(iron | row) · (gate/256) · 0.5 = 0.125 ·
  P(iron | row) · gate/256`;
* the AP cap keeps `material ≤ cap(ap)` for every rank (the §4.5 AP 1–3
  columns are all zero for every rank, which is that property in table
  form).
