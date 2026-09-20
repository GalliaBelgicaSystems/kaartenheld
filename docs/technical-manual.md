# KAARTENHELD — Technical Manual (as implemented)

> Looking for the friendly player's guide instead? See [`manual.md`](manual.md).

A complete, machine-checked reference to **KAARTENHELD — BATTLE DEMO**, the
Game Boy card-combat RPG.

This manual describes the game exactly as implemented.  Where a design
document and the code disagree, the code wins (for example the BURN/POISON
tick values in §8.5 come from `src/rpg/status_content.c`).

Everything here is readable without booting the ROM, but you can also walk
it live with the deterministic harness (`make test-harness`,
`make screenshots`) — see §12.

---

## 1. Story

### 1.1 Premise

Three slides play over the intro (`src/screens/title_content.c`,
`intro_screen.c`):

> A troubled land
> calls out for a
> hero of a new
> kind.
>
> Your strength is
> not in steel,
> but in the cards
> you carry.
>
> Only the Lord of
> Slimes stands
> between all that
> lives and the end.

The land calls for a hero — but not one of sword and steel alone.  Your
strength is in the **cards** you carry.  Monsters have begun to menace the
settlements, and the Lord of Slimes — a slime of terrible size — stands
between everything living and the end.

You play the **Hero**.  Your task, given indirectly by the Mayor of the
town, is to clear the monster threat, claim the reward, and finally reach
**Castle** where the Lord of Slimes waits.  Defeating it lifts the blight
and the game reaches its ending.

### 1.2 The villain

The **Castle** holds a bat, a spider and a solo **Mimic**; beyond it, the
**Throne Room** is where the **Lord of Slimes** (50 HP) waits.  He appears
**only after** the Monster Hunt quest is complete (his spawn is gated on the
quest variable, see §4.2), so "every slime you kill leads to a bigger slime".

---

## 2. Getting started

### 2.1 Title screen

Boot lands on the **studio splash** — the GALLIA BELGICA SYSTEMS bitmap
logo (`assets/gallia_belgica_systems.png`, red/blue bracket) — which
auto-advances to the title screen after ~2.5 seconds or is skipped
immediately with `A`/`START`.  The title screen then shows the
**KAARTENHELD** logo (a red bitmap logo, `assets/title-red.png`), its
**BATTLE DEMO** subtitle, and `PRESS START`.

The on-screen title block is:

```
    [  KAARTENHELD  ]     (red bitmap logo, assets/title-red.png)
    BATTLE DEMO           (text)
```

Pressing START opens a menu with four entries (`title_screen.c`,
`title_content.c`):

| Entry | Effect |
|---|---|
| `NEW GAME` | Starts a fresh game: intro slides → overworld. |
| `CONTINUE` | Opens the **LOAD GAME** screen (saves, see §10). |
| `SOUND: ON/OFF` | Toggles the soundtrack. |
| `TUTORIAL` | Plays the 7-slide tutorial (see §2.4). |

### 2.2 Intro

`NEW GAME` shows the three story slides above.  `A` or `START` advances;
after the third slide the game drops into a fresh overworld.

### 2.3 Controls

Overworld:

| Button | Action |
|---|---|
| D-Pad | Move (hold to keep walking). |
| `A` | Interact with the actor you face (talk / shop / save / battle). |
| `START` | Open the quick screen (cards & quests, §7). |
| `SELECT` | Open the **LOAD GAME** screen. |

Dialogue: `A`/`START` advances to the next line; the dialogue closes after
the last line.

Battle and menus have their own controls, covered in §6 and §7.

### 2.4 Tutorial

`TUTORIAL` on the title menu plays seven slides (`tutorial_content.c`).
`LEFT`/`RIGHT` navigates, `B` exits.  The slides cover:

1. **TUTORIAL BASICS** — use LEFT/RIGHT to navigate, B to exit.
2. **CARD TYPES** — SW sword (damage), SH shield (block), BO bow (damage).
3. **CARD TYPES 2** — HE heal (restore), DA dagger (dmg), F SW fire + BURN.
4. **COMBOS** — played cards rank like poker hands, boosting effects.
5. **ENERGY & COMBAT** — cards cost ENERGY; start with 6/turn.
6. **DEFEND & STATUS** — only SH blocks dmg; SW/BO/DA block 0; POISON is a
   damage-over-time.
7. **SHIELD CARD** — offense: 0 dmg, but still counts for combos.

---

## 3. The world

### 3.1 Maps and travel

The world is a connected network of fourteen maps (`scenes_content.c`,
`src/world/world.h`):

```
FIELD (32×18) ──east── TOWN (20×18)
FIELD         ──north─ FOREST (20×18)
FIELD         ──south─ SOUTH FIELD (20×18)
FOREST        ──east── FOREST GLADE ──east── DEEP WOODS ──north── SUNKEN GROVE
                                              DEEP WOODS ──east── OVERGROWN SHRINE
SOUTH FIELD   ──east── HOWLING RIDGE
SOUTH FIELD   ──south─ MOUNTAIN PASS ──north── CASTLE ENTRY
                        CASTLE ENTRY  ──north── GREAT HALL
                        GREAT HALL    ──north── CASTLE (mimic)
                        CASTLE        ──north── THRONE ROOM (Lord of Slimes)
```

`>` / `<` tiles on the ground are gates.  Walk onto a gate to cross.  Each
map has its own looping theme: FIELD / FOREST / FOREST rooms use the forest
theme, SOUTH FIELD / MOUNTAIN PASS / HOWLING RIDGE the desolate theme,
TOWN its own theme, and every castle room the dungeon theme.

### 3.2 Terrain legend

The world is rendered as ASCII tiles (`ui.c`):

| Glyph | Meaning |
|---|---|
| `.` | Floor — walkable. |
| `#` | Wall / tree / solid — blocked. |
| `>` | Gate (exit) — walkable, crossing changes map. |
| `B` | Building — blocked (town and castle interiors' outer walls). |
| `1` `2` / `3` `4` | 2×2 stump pairs in the forest — blocked. |

### 3.3 Places

**FIELD** — your starting area (spawn at (4,4), facing down).  A signpost
near the top (2,4) teaches the game:

> East: Town. North: Forest (danger!).
> ATK: A then SELECT. Shields add to combos (no dmg).
> DEF: shields block. Combos are poker!

Hostiles patrol the whole width: a slime (14,8), a spider (23,9), a bat
(8,12) and a kobold (26,14).

**TOWN** — the hub.  Contains (actors at `src/game/actors_content.c`):

| Actor | Position | Role |
|---|---|---|
| Mayor | (10,5) | Gives the Monster Hunt quest (§4.2). |
| Guard | (10,8) | Flavour dialogue; greets you warmly after you meet the Mayor. |
| Shopkeeper | (9,3) | Runs the shop (sells the Ring, §9). |
| Lost Merchant | (11,3) | Runs the Lost Amulet quest, then a bigger shop (§4.3, §9). |
| Wizard | (6,10) | Lets you save the game to one of three slots (§10). |

Three fire braziers (4,5), (4,12), (14,5) and two dogs (8,14), (11,8) are
placed as blocking scenery.

**FOREST** — trees, stumps, a slime (12 HP), a bat (8 HP), a kobold
(12 HP) and a tougher slime (20 HP), plus the Lost Amulet chest at
(16,10).  An east gate opens onto the forest branch.

**FOREST BRANCH** — MOSSY GLADE (slime, bat, kobold, tough slime; a
hermit's advice), DEEP WOODS (kobold, spider, tough slime, bat; a
signpost), SUNKEN GROVE (a solo Mimic, spider, bat and tough slime), and
the OVERGROWN SHRINE (elite slime, spider, kobold, bat; a shrine
signpost).

**SOUTH FIELD / HOWLING RIDGE** — desolate ground.  The south field
holds a slime, a bat and a kobold; the ridge east of it is thick with a
kobold, a bat, a tougher slime and a spider, plus a scout's advice.

**MOUNTAIN PASS** — a narrow walled corridor with a slime (16 HP), a bat
(8 HP) and a tougher slime (20 HP).

**CASTLE CHAIN** — CASTLE ENTRY (bat, spider, kobold, tough slime), the
GREAT HALL (kobold, spider, bat, tough slime; a knight's advice), CASTLE
(a bat, a spider, a kobold and the solo Mimic), and the THRONE ROOM,
where the Lord of Slimes waits (with a bat and a spider) once the Monster
Hunt quest is complete.

### 3.4 Encounters

Hostiles roam and pat​rol (slimes do a cross pattern, bats a circle
`patrol_banked.c`).  Two ways to start a battle:

* Walk **into** a hostile's tile.
* Have a patrolling hostile walk **into** you.

You will also be hit if you try to bump-past a hostile who blocks the
path.  Every normal encounter engages as a **trio**: the struck monster
plus two clones with the same HP.  Enemies flagged **solo** — the Mimic
and the Lord of Slimes — stand alone.  See §9.2 for enemy statistics and
§6.6 for their decks.

---

## 4. Quests

Quests surface on the quick screen's `QUEST` tab (§7.2).  Both quests are
fully data-driven in the event table (`src/game/events_content.c`).

### 4.1 Quest status model

Each quest has a status **variable**: `0` = not started, `1` = active,
`2` = complete.  Quest list (`src/game/quests_content.c`):

| Quest | Status var | Objective | Reward note |
|---|---|---|---|
| MONSTER HUNT | `QUEST_MONSTER_HUNT` | defeat 3 monsters | "SWORD" |
| LOST AMULET | `MERCHANT_QUEST` | find & return the amulet | "MERCHANT" |

### 4.2 Monster Hunt — the main quest

1. **Start.** Talk to the **Mayor** for the first time:

   > I am the Mayor.
   > Slimes menace the forest.
   > Please help us!

2. **Objective.** Defeat **3 monsters in total** — any hostile counts
   (field/forest slimes, bats, even the boss).  Kills made *before* the
   quest starts still count toward the total.  While the quest is active
   the Mayor says "Still monsters about. Defeat them all!"
3. **Complete.** Talk to the Mayor again once 3 are defeated.  He gives
   you the **Iron Sword** card and warns that something dreadful waits at
   the Castle:

   > You did it! Take this Sword!
   > I have felt a disturbance in the ether: I believe that something
   > dreadful waits for you at the Castle.

4. **Finished.** Talking to the Mayor after completion is a short "go
   forth, hero!" line ("The Sword suits you. Go forth, hero!").

Completing this quest is what unlocks the spawn of the **Lord of Slimes**
in the Castle.

### 4.3 Lost Amulet — the side quest

1. **Start.** Talk to the **Lost Merchant** in Town (the first visit):

   > A thief stole my
   > family heirloom!
   > Find it in the Forest!

2. **Objective.** Find the **Lost Amulet** in the Forest (16,10).
   Interact with it ("You found the Lost Amulet!").  It is added to your
   card collection (as a non-deckable `SPECIAL` card).  Returning to the
   spot afterwards says "Nothing here now."
3. **Complete.** Return to the Merchant once you have the amulet.  He
   thanks you, removes the amulet from your collection, pays you
   **15 gold**, and opens his real shop (which sells the Iron Sword and
   the Mythril Bow):

   > You found it! Thank you, hero!
   > My shop is open now.

---

## 5. Cards

### 5.1 The card language

Every tool in battle is a **card**.  Cards have several attributes
(`src/rpg/cards.h`, `src/game/cards_content.c`):

| Field | Meaning |
|---|---|
| **Name / identity** | e.g. `I SW`, `W SH`, `M P DA`.  The descriptive name shown in menus. |
| **Battle code** | Weapon symbol + power.  The **shop** labels a card `SW3`,
`SH2`, `RG5`; the **battle hand** uses its own code set — `SW3`, `SH2`,
`BO2`, **`HE5`** for a heal ring, `DA1`. |
| **Type** | Broad category: ATK / DEF / HEL / STS / UTL / SPL (see below). |
| **Power** | Base magnitude of the effect. |
| **Cost** | Energy price to play in battle (most cards cost 1). |
| **Uses** | Limited uses per battle (rings: 3). `-` = unlimited. |
| **Max copies** | How many copies may be decked at once. |
| **Rider** | On-hit status rider (fire → BURN, poison → POISON) with a base chance, scaled by your combo. |
| **Price** | Buy cost in gold (and sell value, §9.3). |

### 5.2 Battle card types

| Symbol | Name | Battle role | Description |
|---|---|---|---|
| `SW` | Sword | Attack | Physical damage. `F SW` = fire rider (BURN). |
| `SH` | Shield | Defense | Blocks damage in the defend phase; **0 damage** in attack but still counts toward combos. |
| `BO` | Bow | Attack | Ranged damage. |
| `RG` / `HE` | Ring | Heal | Heals its power; in attack a ring deals 0 but acts as a **joker** (§8.2); in defense it blocks like a shield (§6.5).  Shop label `RG5`, battle hand label `HE5`. |
| `DA` | Dagger | Attack | 1 damage plus a poison rider. |

Broad categories: `ATK` attack, `DEF` defense, `HEL` heal, `STS` status,
`UTL` utility, `SPL` special (quest items that cannot be decked).

### 5.3 The card catalog

All catalogue cards (`src/game/cards_content.c`):

| Card | Name | Code | Type | Pwr | Cost | Uses | Max | Rider | Price |
|---|---|---|---|---|---|---|---|---|---|
| Iron Sword | `I SW` | `SW3` | ATK | 3 | 1 | – | 4 | — | 10g |
| Wooden Shield | `W SH` | `SH2` | DEF | 2 | 1 | – | 3 | — | — |
| Ring (shop) | `I RG` | `RG5` | HEL | 5 | 1 | 3/battle | 3 | — | 20g |
| Fire Sword | `F SW` | `SW4` | ATK | 4 | 1 | – | 3 | BURN 128 | — |
| Poison Dagger | `P DA` | `DA1` | ATK | 1 | 1 | – | 3 | POISON 128 | — |
| Amulet | `AMULET` | — | SPL | 0 | 0 | – | 1 | — | — |
| Mythril Bow | `M BO` | `BO9` | ATK | 9 | 2 | 2/battle | 1 | — | 30g |

Chances are in 1/255 units: `128` ≈ 50%.  They scale with your combo
multiplier (§8.4).

### 5.4 The collection and the battle deck

Your **collection** is every card you own (up to 12 distinct card types).
Your **battle deck** is a subset (up to 20 copies) that you actually
bring into battle.  Owning a card is not enough — it must be stacked in
the deck to appear in your hand.

Deck rules (`src/rpg/deck.h`):

* `DECK_MIN_CARDS 5` — the deck can never drop below 5 copies.
* `MAX_DECK_CARDS 20` — the deck cap.
* You can only add a copy if you own it, it is deckable (not `SPL`), and
  it is within the card's `max copies`.
* A card's decked count is visualised in the menu as the membership digit.

### 5.5 Starter deck

A new game grants this 12-card deck (`content.c`): **4× Iron Sword
(`SW3`), 3× Wooden Shield (`SH2`), 3× Fire Sword (`SW4`), 2× Poison
Dagger (`DA1`)**.  The opening hand is always `SW SW SH SH SW` (the first
five copies are the two swords, two shields and a fire sword; the extras
only deepen the draw pile).

---

## 6. Battle

### 6.1 Battle screen layout

The battle screen (HUD layout fixed in `AGENTS.md §52.11.2`):

| Row | Content |
|---|---|
| 0 | Turn banner, centered (`PLAYER TURN`, `ENEMY ATTACK!`, `DEFENSE TURN`, `VICTORY!`, `DEFEATED!`, `FLED!`, ...). |
| 2 | Enemy HP line. |
| 3–4 | Enemy art + target caret. Up to 3 enemies. |
| 7 | Hero row: `HERO` label + `HP: n/m`. |
| 8 | Deck counter: `DECK: n` (cards left in the draw pile) + AP. |
| 9 | Transient gameplay messages (`NO ENERGY!`, `OUT OF USES!`, `ONE RING!`). |
| 10 | Live combo row: `COMBO:` + current hand name while you select. |
| 11–14 | Your boxed hand cards (up to 5). |
| 15 | Selection markers: digits 1–5 in selection order + the `^` cursor. |
| 16 | Card description of the hovered card. |
| 17 | Timer bar (window), draining as the turn timer runs. |

An ASCII-converted view of the "YOU FOUND:" loot reveal flips onto rows
10–12 after a victory (banner, synthesized card name, full description).

### 6.2 Hand, energy, timer

* **Hand**: 5 cards drawn from your deck at battle start.
* **Energy**: **6 per full round**.  Most cards cost 1; the Mythril Bow
  costs 2.  Selecting a card reserves its cost; you cannot select a card
  whose cost exceeds the energy you have left (`battle_nav_banked.c`).
* **Timer**: **20 seconds** per player decision phase, shown as a bar
  (1200 frames).  It runs in the attack *and* defend decision phases.
  When it expires, your current selection is auto-executed — a timer-out
  with no selection commits an empty combo (`battle.c`).

### 6.3 Battle phases

Battles run a fixed cycle (`battle.h`):

```
PLAYER_SELECT → PLAYER_ANIM → ENEMY_TELEGRAPH → PLAYER_DEFEND
                                                   → DEFENSE_RESOLVE
                                                   → (status ticks)
                                                   → back to PLAYER_SELECT
```

1. **Play your attack** — pick up to 5 cards from your hand.
2. The hero's attack resolves (with combo multipliers, §8.4).
3. **Enemy telegraph** — the enemy draws a card; its value is the damage
   incoming this round, announced before you defend.
4. **Defend** — play shield cards to block that incoming damage.
5. **Resolve** damage and recover, statuses tick (poison/burn), then the
   next round begins: hand refills, energy and timer reset.

### 6.4 Playing your attack

Battle input:

| Button | Action |
|---|---|
| `LEFT` / `RIGHT` | Move the hand cursor. |
| `UP` / `DOWN` | Change the current target (when several enemies are alive, e.g. a trio). |
| `A` | Select the hovered hand card (locks in at the next marker slot). |
| `B` | Undo the last selection. With **no cards selected in the attack phase**, `B` **flees** the battle. |
| `SELECT` | **Execute** the current selection. |
| `START` | Open the quick screen (pauses battle; it resumes when you close it). |

You may select up to **5** cards.  The order you select them in is the
order they are played (selection digits show the order).

The first (leading) card decides the action:

* A **sword/bow/dagger** lead deals damage.
* A **ring** lead heals you instead.  The heal is the whole hand's scaled
  power (the sword/bow damage becomes healing, §8.4) **plus** the ring's
  own value — so a ring-first selection trades the attack for a heal.
  Put the ring second if you want the swords to hit and the ring to just
  top you up.
* A **shield** lead still deals the non-shield damage sum (shields are
  "fodder" but count toward your hand shape).

### 6.5 The defend phase

After the enemy telegraphs (`ENEMY ATTACK!`), you defend:

* **Only `SH` (shield) cards block.**  Swords/bows/daggers in the defend
  hand contribute `0` block.
* Rings act as **wild shields** worth their power.
* The net is `incoming − (sum of shields + rings)`.
  * **Net > 0** → you take that damage (clamped to your HP).
  * **Net ≤ 0** → no damage.  Over-block *heals* you only while a ring is
    present, capped at max HP.

### 6.6 Enemy turn details

* The enemy draws **one card per attack turn** from its fixed deck;
  the card's value is the damage you must block (see
  `enemy_deck_content.c`):
  * **Slime** — `SW2/SW3` mix with an occasional heal worth 2.
  * **Bat** — bow and sword values up to 4; one bow swing carries a
    poison rider (60/255).
  * **Kobold** — `SW2/SW2/SW3` plus a heal worth 2.
  * **Mimic** — `SW2`–`SW4` plus a heal worth 3.
  * **Spider** — bow/sword up to 3 plus a healer; one webbing bow swing
    carries a poison rider (70/255).
  * **Team slimes** (the trio encounter) hit for 2–3 each round.
* When the enemy deck is exhausted it **reshuffles** and keeps going.
* Freeze / grey-out can make the enemy skip (see §8.5/§8.7).
* The boss (no deck) simply telegraphs a flat 3-damage swing.

### 6.7 Deck exhaustion — the reshuffle turn

When the draw pile cannot cover every open hand slot at the start of
your round — fully dry, or short with cards already discarded — the game
consumes your action that cycle to **reshuffle** the discard pile back
into the draw pile and re-deal; the enemy still attacks that round
(`deck.md Phase 10`).  A short pile never deals a partial refill: the
reshuffle happens first instead of minting phantom `SW2` safety cards for
the shortfall.  The draw pile otherwise never auto-refills from the
discard.

### 6.8 Victory, defeat, flee

* **Victory** — all enemies are defeated.  A one-shot victory fanfare
  plays, the loot card reveal shows what you found (`YOU FOUND:`), and
  you are returned to the overworld with HP carried over.  Gold is
  awarded and the defeated monster is recorded as dead (it will **not
  respawn** on that save file).
* **Defeat** — your HP reaches 0.  Game Over screen (§10).
* **Flee** — `B` with nothing selected in the attack phase.  You escape,
  no reward, the enemy survives where it was.

### 6.9 The loot roll

Every victory drops **exactly one combat card** — rolled precisely at
battle start (isolated RNG), revealed and granted on victory
(`loot.md §34.5`).  The card is added to your collection.  If your
collection is full (12 distinct types), no card is granted.

---

## 7. Menus

### 7.1 Opening menus

* Overworld `START` → **CARDS** quick screen.
* Overworld `SELECT` → **LOAD GAME**.
* Facing an actor and pressing `A` → dialogue, shop, save, or battle,
  depending on the actor.

### 7.2 The quick screen (CARDS / QUESTS)

Two tabs, switched with `LEFT`/`RIGHT`.  A `^` marks the active tab.

**CARDS tab** — the collection/deck manager:

* The top row (`* FILTER/SORT *`) opens a picker with `A`.
* Each card row shows its identity name (e.g. `I SW`) and, at the far
  right, the **decked-copy count** (0..n).
* `A` on a card adds **one copy** to your battle deck; pressing `A` again
  once the card is fully decked **clears all its copies** from the deck
  (all-or-nothing — it refuses if that would drop you below the 5-card
  minimum).  Rejections show a transient message: `DECK FULL`, `DECK MIN
  5`, or `QUEST ITEM` (for special quest items).
* `SELECT` on a card opens the **detail page**: name, type, power, cost,
  uses/battle, max copies, owned, decked, price.
* `B` backs out (two-step on the cards list: first `B` jumps to the top
  row, `B` again closes the menu).  `START` closes from the list.
* `A` on a loot card's **detail page** sells one copy while you have a
  buying merchant engaged (§9.3).

The **FILTER/SORT picker**: `UP`/`DOWN` moves between the FILTER and SORT
rows, `LEFT`/`RIGHT` cycles, `A` confirms, `B` cancels.

* Filter cycles `ALL → ATK → DEF → HEL → STS → UTL → ALL`.
* Sort cycles `OFF → TYPE → PWR+ → CST+ → PWR− → CST−`.

**QUEST tab** — the quest list.  Each entry shows the quest name and a
status line:

* `not started`
* `status var: X/T` while active (e.g. `monsters: 2/3`)
* `complete - SWORD` when done.

`SELECT` on a quest opens a placeholder detail page.

---

## 8. Combos & statuses

### 8.1 Card values and symbols

Each card has a **value** (its power) and a **symbol** (battle type).
In battle the only thing that matters for combinations is the value and
the symbol — the names are cosmetic.

### 8.2 Hand classification

Selected cards are ranked **exactly like poker hands**
(`combo_content.c`).  The evaluator is order-independent:

| Tier | Name | Requirement | Multiplier |
|---|---|---|---|
| 0 | (high card / <2) | — | 100% |
| 1 | PAIR | 2+ same value | 120% |
| 2 | TWO PAIR | 2 pairs | 150% |
| 3 | THREE KIND | 3 same value | 180% |
| 4 | STRAIGHT | all 5 sequential values | 210% |
| 5 | FLUSH | all 5 same symbol | 240% |
| 6 | FULL HOUSE | trips + pair | 260% |
| 7 | FOUR KIND | 4 same value | 280% |
| 8 | STRAIGHT FLUSH | 5 sequential, same symbol | 350% |
| 9 | FIVE KIND | all 5 same value | 400% |

* Straights/flushes/straight-flushes/five-kind require **all five** cards;
  pairs and kinds work with 2+.
* **Rings are JOKER cards** (`combo_content.c` §34.3): in the hand
  evaluation, each ring's value may be any value **1..10** — the evaluator
  tries every legal value and keeps the *best* tier.  A `SW3, SH2, RG5`
  selection lets the ring become a 3 (a pair) so the tier improves.
  Crucially the joker substitutes **values only** — a ring's symbol is
  still `HE/RG`, so it still participates in (and can break) a suited
  bonus.  A lone ring is the best hand-add (wild value), but two rings
  drag your suitedness.
* The whole selected hand contributes to the classification, but only the
  **effective** cards contribute to the base magnitude (see §8.3).

### 8.3 Base power

* **Attack phase**: base power = sum of all non-shield, non-ring values.
  (Shields and rings deal 0 in attack but still count toward the hand
  shape.)
* **Defend phase**: base power = sum of **shield values and rings**
  (rings are wild shields worth their power).

### 8.4 Effect scaling

The resolved effect magnitude is

```
amount = base_power × multiplier / 100
```

where multiplier is the tier percent, plus a **+25% suited bonus** when all
cards that enter the hand evaluation share one symbol — in **attack** that
means every selected card (a shield or ring you slot in can break a suited
bonus); in **defend** it means every shield/ring you play.  Examples: a
`SW3 + SW3` pair = 6 × 120% = 7 (truncated); a `SW3,SW3,SH2,SH2,SH2` full
house (power 6) = 6 × 260% = 15.

The **suited bonus** is the reason mixing too many card types dilutes your
damage — a homogeneous hand punches well above its weight.

### 8.5 Status effects

Statuses (`src/rpg/status_content.c`):

| Status | Tick (dmg/round) | Max stacks | Duration | Effect |
|---|---|---|---|---|
| POISON | 1 | 5 | 3 rounds | Deals flat 1 HP/round. Also **greys out** the victim's cards (§8.7). |
| BURN | 1 | 3 | 3 rounds | Deals flat 1 HP/round. |
| FREEZE | 0 | 1 | 3 rounds | Victim **skips its whole action** (no attack for the player, no swing for the enemy) each round it lasts. |

The tick is **flat per status** regardless of stack count; extra
applications refresh the duration and deepen the stack counter (visible in
telemetry).  Statuses tick once per round at the return to `PLAYER_SELECT`;
deaths from poison/burn resolve like combat deaths.

**Applying statuses.**  Riders come from cards:

* Player→enemy: the status rider on your **leading card**, with a base
  chance scaled by your combined multiplier (`chance × mult / 100`,
  capped ~100%), rolled vs the deterministic RNG.  `F SW` → BURN 128,
  `P DA` → POISON 128, loot fire swords → BURN 128, loot ice swords →
  FREEZE 96, loot poison daggers → POISON 128.
* Enemy→player: the bat's poisoned swing carries a ~60/255 poison chance
  that also greys your hand.

If the roll fails the target **resists** (telemetry `STATUS_RESISTED`).

### 8.6 Freeze

A frozen player can neither attack (whole offense skipped) nor defend
(the incoming swing lands unblocked).  A frozen enemy skips its swing.
Freeze never stacks and lasts 3 rounds.

### 8.7 Poison grey-out

Poison's special penalty: while a combatant is poisoned,
`POISON_GREY_CARDS` **2** random cards in its pool become **unplayable**
for `POISON_GREY_TURNS` **2** rounds (`status.h`):

* **Player**: 2 cards from your 5-card hand grey out — you cannot select
  them.
* **Enemy**: 2 positions in its **shared deck** grey out — those swings
  are skipped (and the draw position does not advance, so the enemy is
  locked out for the grey duration).

Re-poisoning refreshes the grey without re-rolling which cards are greyed.

---

## 9. Loot, shops & economy

### 9.1 Gold

Your only currency is **gold** (start: 20g, `HERO_START_GOLD` in
`content.c`).  Sources: battle rewards (below), the Amulet quest (+15g),
selling loot.  Uses: shop purchases.

### 9.2 Battle rewards

Defeating a monster grants gold **once per battle** (a trio pays the
plain reward once, not per clone).  HP varies by placement; gold depends
on the enemy type:

| Enemy | HP | Gold |
|---|---|---|
| Slime | 10–16 | 5 |
| Elite slime | 20 | 10 |
| Bat | 8–12 | 8 |
| Kobold | 12–18 | 8 |
| Spider | 16–18 | 15 |
| Mimic (solo) | 30 | 0 |
| Lord of Slimes (solo boss) | 50 | 50 |

### 9.3 Shops

Two shops exist (`src/game/shops_content.c`):

* **Shopkeeper** (Town, (9,3)) — shop 1 — sells the **Ring** (`I RG`,
  `RG5`) for **20g** (a heal card; the game's only reliable healing,
  outside loot rings).
* **Lost Merchant** (Town, (11,3)) — shop 2 — sells the **Iron Sword**
  for **10g** and the **Mythril Bow** for **30g**.  Reachable only after
  the Lost Amulet quest (§4.3).

Shopping: `A` buys the hovered item if you have enough gold and room in
your card collection (max distinct types = 12).  Feedback lines: `Bought!`,
`Too many!`, `Not enough!`.  `B` or `START` leaves.  Purchased cards join
your **collection**; deck them from the CARDS menu to use them.

### 9.4 Loot cards and selling

Victory loot cards are **procedurally generated** with a derived identity
name `[material] [effect] weapon` (`loot_banked.c`, `loot.md §34`) — e.g.
`W SW` (wood sword), `M P DA` (mythril poison dagger), `W F SW` (wood fire
sword).

* Materials raise power: **W**ood +0, **B**ronze +1, **I**ron +2,
  **M**ythril +3 (weighted wood-heavy).
* Effects are legal per weapon: daggers may carry **P**oison, swords may
  carry **F**ire (BURN) or **I**ce (FREEZE); bows/shields/rings stay
  plain.  Illegal pairs carry no rider.
* Weapon bases: sword 3, shield 2, bow 2, ring 2 (heals), dagger 1.

Loot cards are unlimited-use (no per-battle uses) and have no copy limit
(0 = unlimited duplicates).

**Selling.** While a **buying** merchant is engaged (`g->shop_id` set by
the shop actor; only the Lost Merchant buys), you can sell **loot** cards
from the CARDS tab: open a loot card's detail page (`SELECT`) and press
`A` to sell one copy (no `SELL` label is shown — the sale is silent; the
`CARD_SOLD` telemetry event is the authoritative feedback).  Sell value =
the card's price field, synthesized as `base power + material tier×2 (+2
for a poison dagger)`.  You cannot sell a copy that would strand a decked
card without ownership backing (rejected with the `DECK FULL` message).

---

## 10. Saving, game over & the ending

### 10.1 Saving

* **Wizard** in Town (6,10) saves the game: pick one of **three slots**
  (`SAVE GAME` screen: `SLOT 1/2/3`, saved vs empty).  `A` saves, `B`
  back.
* **Overworld `SELECT`** and the **title `CONTINUE`** open **LOAD GAME**:
  pick a saved slot and `A` to load.
* Saves are battery-backed SRAM; the slot stores the whole persistent
  `GameState` with a magic/version/checksum (`save.md`).  Empty or a
  version-mismatched slot cannot be loaded.

### 10.2 Game over

Defeat in battle → `GAME OVER … CONTINUE? YES / NO`:

* **YES** → goes to the LOAD GAME screen so you continue from a save.
* **NO** → `THANKS FOR PLAYING!` (a dead-end screen).

### 10.3 The ending

Defeating the **Lord of Slimes** sets the ending flag; after the battle
you go to the **ENDING** screen instead of back to the overworld:

```
      THE END
--------------------
The Hero cleared
the land of slimes!
Peace has returned!
Thanks for playing!
[A] RESTART
```

`A` (or `START`) restarts a fresh game — straight back into the overworld,
skipping the title screen and the intro slides.

---

## 11. Audio

Music tracks (`src/audio/audio.h`): overworld, battle, victory (one-shot
fanfare), title, town, dungeon (castle), boss.  The boss theme plays only
for the Lord of Slimes.  UI SFX: a short cursor blip and a confirm tone.
`SOUND: ON/OFF` on the title menu toggles the soundtrack.

Music runs on the hardware **timer interrupt** at a fixed 256 Hz, so
tempos are stable across menus, dialogue and map transitions (AGENTS.md
§35).

---

## 12. Developer appendix — the debug harness

The game is a first-class **LLM/agent development target**.  Everything
above is observable through a deterministic, machine-readable harness:

| Tool | Purpose |
|---|---|
| `make debug` | Build the debug ROM (telemetry, scenarios, RNG control, assertions). |
| `make test-harness` / `make test-scenario SCENARIO=<name>` | Run all / one scenario against the ROM (`tools/scenarios/tests/*.json`), returning PASS/FAIL. |
| `make test` | Release ROM validation (link, header, checksum). |
| `make verify-oam` | mGBA execution checks for VBlank-timed OAM bugs. |
| `make verify-walkthrough` | Drive the release ROM (real `levels/` content) and assert canonical state. |
| `make verify-scroll` / `verify-music` / `verify-patrol` / `verify-endurance` | Extra PyBoy regression checks. |
| `make screenshots` | Deterministic headless walkthrough producing `screenshots/*.png`. |
| `make gifs` | Regenerate the README demo GIFs (`screenshots/*.gif`). |
| `make memmap` | Memory-budget reporter (fails on fixed-bank overflow). |
| `make lint` | `-Wf-Wall` compile-to-asm lint (no warnings allowed). |

The debug protocol (`docs/DEBUG_PROTOCOL.md`) exposes `PRESS/WAIT/STEP`,
`INSPECT`, `SNAPSHOT`, `EVENTS`, `SET_RNG`, `SET_FLAG`, `TELEPORT`,
`ASSERT`, and scenario loading.  Key telemetry events: `PLAYER_MOVED`,
`COLLISION`, `ENCOUNTER_STARTED`, `BATTLE_STARTED`, `COMBO_RESOLVED`,
`EFFECT_RESOLVED`, `STATUS_APPLIED`, `STATUS_RESISTED`, `CARDS_GREYED`,
`TURN_SKIPPED`, `DAMAGE_DEALT/RECEIVED`, `ENTITY_DEFEATED`,
`LOOT_CARD_ADDED`, `CURRENCY_ADDED`, `GAME_STATE_CHANGED`, `SCREEN_CHANGED`,
`MAP_CHANGED`, `MUSIC_CHANGED`, `SCRIPT_TRIGGERED`.

Reproduce any trophy scenario ("can I beat the boss at 20 HP?") with a
scripted scenario page (`docs/dev-harness.md`, `docs/LLM_AGENT_GUIDE.md`).

---

## 13. Reference: quick numbers

* Hero: HP 10, gold 20, deck 12, opening hand `SW SW SH SH SW`.
* Deck: min 5, max 20 copies; collection cap 12 distinct types.
* Battle: hand 5, energy 6/round, timer 20s per decision.
* Enemies: trios for normal monsters; the Mimic and the boss are solo.
  HP/gold per §9.2.
* Statuses: POISON/BURN tick 1 × 3 rounds (5/3 max stacks), FREEZE 3
  rounds skip; poison greys 2 cards for 2 rounds.
* Combos: 10 poker tiers, 100–400%, +25% if suited.

> Source of truth for every number above: the code tree (`src/`).  Where
> a doc in `docs/` disagrees (e.g. older BURN "tick 2" notes in
> `combo-system.md`), the code tables in `status_content.c` win.