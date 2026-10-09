# Game Boy Baten Kaitos–Inspired Combat Prototype

## 1. Objective

Build a very simple, highly readable Game Boy combat prototype inspired by the card-based combat of the first **Baten Kaitos**.

The prototype should prioritize:

* fast card selection
* number-based combos
* simple attack and defense
* responsive Game Boy controls
* extremely simple graphics
* a visible turn timer
* minimal menus
* reusable tile-based UI

Do **not** attempt to reproduce the original game's graphics.

For the first prototype, cards are represented entirely by **two-letter abbreviations plus a number**.

Examples:

```text
SW3
SH5
BO2
SW4
HE7
```

The goal is to prove that the card-combat loop is fun and understandable before investing in proper card artwork or complex effects.

---

# 1.5 The Attack/Defend Split

The battle is built on **two opposed phases**, each with a distinct card role. This
is the core of the design, so read this before any of the per-card details.

```text
ATTACK PHASE  (your turn)          DEFEND PHASE (enemy telegraphs a hit)
   build an offensive combo            build a defensive block
       ↓                                   ↓
 SW / BO / FI = damage               SH = block value
 HE           = heal                 SW / BO / FI / HE = inert
 SH           = combo fodder (0 dmg)
```

Every card has an **active role** determined by the current phase:

| Card    | ATTACK phase                       | DEFEND phase              |
| ------- | ---------------------------------- | ------------------------- |
| `SW`    | Physical damage (adds value)       | inert                     |
| `BO`    | Ranged damage (adds value)         | inert                     |
| `FI`    | Elemental damage (adds value)      | inert                     |
| `HE`    | Heal (adds value to player HP)     | inert                     |
| `SH`    | combo fodder (0 damage)            | block value               |

### The shield tension (why this design exists)

A shield (`SH`) is genuinely good at **both** jobs — in an attack combo it
extends the straight and raises the multiplier, and in the defend phase it
becomes block value. But a shield used is a shield **gone** from your hand and
deck.

So every time your hand shows a shield you answer the question:

> Do I burn this shield **now** to make a bigger attack combo, or do I save it to
> survive the enemy's next hit?

Spending one in a combo that doesn't even land is a total loss.

### Fodder vs inert (the asymmetry)

* In the **attack** phase a shield is **fodder**: it contributes **0 damage**,
  but its number **still counts toward straight detection and the combo
  multiplier**. `SW1 SH2 SW3` is a valid 3-card straight whose damage base is
  `1 + 3` (the shield's `2` does not add damage, but it earns the ×150%
  multiplier).
* In the **defend** phase a non-shield (`SW/BO/FI/HE`) is fully **inert**: it
  contributes **0 block and 0 combo**. Only shields build a defensive block.

### Wasted cards

Selecting a card palette that produces no effect still **consumes** the cards
from your hand and deck. A lone `SH2` in the attack phase (no straight, base 0)
does nothing but eats the shield — exactly the mistake the tension wants the
player to avoid.

---

## Implementation status (where this prototype stands)

The Game Boy build implements the full card combat system and phase rules. `DONE`
markers throughout the doc point to the exact source. A quick summary:

**Implemented today**
* Five-card hand with LEFT/RIGHT cursor, A-select, B-undo, SELECT-execute.
* Phase-aware combo evaluation (ascending straights, flush/same-type multiplier, 100/150/175/200).
* Attack resolution — lead card type decides action (`HE` heals, offensive types damage); shields are combo fodder (0 damage base, participate in straight/flush multipliers).
* Defend resolution — shields block incoming enemy damage with straight/flush bonuses; non-shields are inert (0 block, 0 combo).
* Card consumption — played cards are always discarded and replaced from the deck.
* Reset deck (Fisher-Yates shuffle), draw/discard/draw-to-replace.
* 20-second decision timer that auto-executes on expiry.
* Explicit battle state machine plus a tutorial-slime battle.
* Telemetry for every important transition; regression scenarios under `make test-harness`.

---

# 2. Card Representation

Every card has:

```text
TYPE + NUMBER
```

Examples:

```text
SW3
SH5
BO2
SW4
HE7
```

The two-letter code communicates the card type.

The number is the card's numerical value and is used for combo construction.

### Initial card types

| Code | Meaning | ATTACK role   | DEFEND role |
| ---- | ------- | ------------- | ----------- |
| `SW` | Sword   | Physical attack | inert     |
| `SH` | Shield  | combo fodder (0 dmg) | block value |
| `BO` | Bow     | Ranged attack  | inert       |
| `FI` | Fire    | Elemental attack | inert    |
| `HE` | Heal    | Restore HP     | inert       |

A card's **type determines its active role in the current phase** (see §1.5).
Attack cards (`SW/BO/FI`) deal damage only in the attack phase; `HE` heals only
in the attack phase; `SH` is the only card that does anything in the defend
phase.

Keep the initial prototype to these five card types.

More can be added later.

---

# 3. No Card Graphics Initially

Do not create individual card sprites.

The card:

```text
SW3
```

should literally be rendered as text/tiles.

Likewise:

```text
SH5
BO2
SW4
HE7
```

The Game Boy already has everything needed to display letters and numbers.

This dramatically reduces the graphical requirements and makes it possible to concentrate on the combat system.

Later, `SW3` could become:

```text
[small sword icon] 3
```

without changing the underlying card system.

---

# 4. Card Data

Internally, do not store cards as strings.

Use compact IDs.

For example:

```text
CARD_SWORD = 0
CARD_SHIELD = 1
CARD_BOW = 2
CARD_FIRE = 3
CARD_HEAL = 4
```

A card instance contains approximately:

```text
type
number
power
effect
```

For example:

```text
type   = CARD_SWORD
number = 3
power  = 3
effect = DAMAGE
```

The renderer converts this into:

```text
SW3
```

This separation is important because the graphics can later be replaced without rewriting the combat engine.

---

# 5. Game Boy UI Philosophy

The Game Boy screen is only 160×144 pixels.

Do not try to reproduce a modern card-game interface.

The UI should be mostly:

* background tiles
* text
* numbers
* a small cursor
* player/enemy sprites
* extremely simple effects

Cards should be rendered into the **background tilemap**, not as hardware sprites.

Reserve sprites for:

* player
* enemy
* cursor if necessary
* simple attack effects

This keeps the prototype within the Game Boy's sprite and memory limitations.

---

# 6. Battle Screen

The basic layout should be approximately:

```text
┌──────────────────────────────┐
│ SLIME              HP 30/30  │
│                              │
│          [SLIME]             │
│                              │
│ HERO               HP 40/40  │
│             DECK: 10         │
│                              │
│ COMBO: SW2 BO3 SW4           │
│                              │
│ SW2  SH5  BO3  SW4  HE7      │
│              ^               │
│ Sword attack. Deals damage.  │
│██████████████████████████████│
└──────────────────────────────┘
```

The screen has four important areas:

1. **Enemy information**
2. **Current combo**
3. **Player's hand**
4. **Card description + turn timer**

The timer always occupies the bottom line.

---

# 7. Hand

The player initially has a five-card hand.

Example:

```text
SW2  SH5  BO3  SW4  HE7
```

The cards are arranged horizontally.

The cursor moves between them.

Example:

```text
SW2  SH5  BO3  SW4  HE7
 ^
```

Move right:

```text
SW2  SH5  BO3  SW4  HE7
      ^
```

Move right again:

```text
SW2  SH5  BO3  SW4  HE7
           ^
```

The cursor should be extremely obvious.

A simple arrow underneath the selected card is sufficient.

> **DONE:** The five-card hand renders horizontally on the battle screen, and the
> cursor moves with LEFT/RIGHT. Implemented in `src/ui/ui.c`
> (`ui_draw_battle_hand`, hand row with cursor arrow) and `src/battle/battle.c`
> (`battle_cursor_move`).

---

# 8. Card Description

There is **no card information menu**.

The description of the currently highlighted card is **always visible** immediately above the timer.

For example:

```text
SW2  SH5  BO3  SW4  HE7
 ^
Sword attack. Deals physical damage.
████████████████████████████████
```

Move to Shield:

```text
SW2  SH5  BO3  SW4  HE7
      ^
Blocks incoming damage.
████████████████████████████████
```

Move to Bow:

```text
SW2  SH5  BO3  SW4  HE7
           ^
Ranged physical attack.
████████████████████████████████
```

Descriptions should be **one very short sentence**.

Do not display lengthy card explanations.

---

# 9. Turn Timer

The entire bottom row is a visual turn timer.

It starts full:

```text
████████████████████████████████
```

and decreases **from right to left**.

For example:

```text
████████████████████████████████
```

then:

```text
██████████████████████████
```

then:

```text
██████████████
```

then:

```text
██████
```

then:

```text
██
```

then:

```text
```

The player should be able to understand the remaining decision time without reading a number.

### Timer rules

* Timer starts when player input becomes available.
* Timer runs only during decision-making.
* Timer pauses during attack/defense animations.
* Timer pauses while inventory is open.
* Timer should not run during battle transitions.
* The bar should flash when it becomes critically low.
* Do not depend on color because the target is the original Game Boy.
* Use blinking or animation for the low-time warning.

---

# 10. Timer Expiration

When the timer reaches zero:

### If cards have been selected

Automatically execute the current combo.

Example:

```text
COMBO: SW2 BO3 SW4
```

Timer reaches zero.

The game automatically performs:

```text
SW2 BO3 SW4
```

This prevents the player from simply losing their turn because they failed to press a button.

### If no cards have been selected

End the action without an attack.

Optionally play a short warning sound.

This can be changed later if testing shows that automatically selecting the first available card feels better.

---

# 11. Button Mapping

The control scheme should respect the existing prototype.

### D-Pad

**LEFT**

Move to previous card.

**RIGHT**

Move to next card.

**UP / DOWN**

Used for context-dependent navigation, primarily target selection when multiple targets exist.

For a single-enemy battle, UP/DOWN can simply do nothing.

### A

**Select card.**

A adds the currently highlighted card to the current combo.

After selecting the card, automatically advance the cursor to the next card.

### B

**Undo / cancel.**

Remove the most recently selected card.

If no cards have been selected, cancel the current action.

### SELECT

**Execute the current action.**

This replaces START as the combat confirmation button.

There is no secondary confirmation screen.

### START

**Open inventory.**

START retains its existing prototype behavior.

Opening inventory pauses combat and pauses the timer.

When the inventory closes, return to exactly the same battle state.

---

# 12. Basic Interaction

The primary interaction should be:

```text
D-PAD
  ↓
Highlight card
  ↓
Read description
  ↓
A
  ↓
Add card
  ↓
Cursor advances
  ↓
A
  ↓
Add another card
  ↓
...
  ↓
SELECT
  ↓
Execute combo
```

Undo:

```text
B
↓
Remove last selected card
```

Inventory:

```text
START
↓
Inventory
```

This keeps the control scheme extremely simple.

> **DONE:** Card selection (`A`/`battle_card_select`, cursor auto-advances to the
> next unselected card), undo (`B`/`battle_card_undo`; with no cards selected in
> the attack phase it flees via `BATTLE_RESULT_FLED`), and execute (SELECT) are all
> wired in `src/battle/battle.c`, driven through the input layer and the battle
> screen (`src/screens/battle_screen.c`).

---

# 13. Combo Display

Selected cards appear in a dedicated area above the hand.

For example:

```text
COMBO: SW2 BO3 SW4
```

The hand remains below:

```text
SW2  SH5  BO3  SW4  HE7
```

Selected cards should have some simple visual indication in the hand, such as:

* inverted tiles
* an underline
* a small marker
* blinking

No special graphics are required.

---

# 14. Selecting Cards

Suppose the hand is:

```text
SW2  SH5  BO3  SW4  HE7
 ^
```

Press A.

The game displays:

```text
COMBO: SW2

SW2  SH5  BO3  SW4  HE7
      ^
```

The cursor automatically advances.

Press A again:

```text
COMBO: SW2 SH5
```

Continue until the desired sequence has been created.

---

# 15. Undo

Suppose:

```text
COMBO: SW2 BO3 SW4
```

Press B.

The result becomes:

```text
COMBO: SW2 BO3
```

The cursor should return to the removed card.

This allows the player to correct mistakes quickly without restarting the turn.

---

# 16. Number-Based Combos

The central prototype mechanic is the number sequence.

The first version should implement only **ascending straights**.

Example:

```text
SW2 → SW3 → SW4
```

produces:

```text
2 → 3 → 4

STRAIGHT!
```

A non-sequence:

```text
SW2 → BO4 → FI5
```

does not produce a straight because `3` is missing.

The card types do not have to match.

This is important.

A combo can be:

```text
SW2 → BO3 → SW4
```

and still be a valid straight.

The numbers are what matter.

However, if the types match, then the combo gets a multiplier bonus:

```text
SW2 → SW3 → SW4
```

### Shields extend length, not damage

Recall the phase rule (§1.5): in the attack phase a shield adds **0 damage** but
its number still participates in straight detection and the combo multiplier.

```text
SW1 → SH2 → SW3
```

is a valid 3-card straight:

```text
1 → 2 → 3
STRAIGHT!
```

Its damage base is only `1 + 3` (the `SH2` contributes no damage), but because
it is a 3-card straight the ×150% multiplier applies. The shield earned the
straight you otherwise would not have had — at the price of one value point and
one shield card.

In the defend phase the mirror is stricter: non-shields are **fully inert**
(§1.5/§23), contributing neither block value **nor** combo length.

---

# 17. Combo Length

Initially support:

### 2 cards

Normal combination.

### 3 cards

Straight bonus.

### 4 cards

Larger straight bonus.

### 5 cards

Maximum initial combo.

Example:

```text
2 → 3
```

Normal.

```text
2 → 3 → 4
```

3-card straight.

```text
2 → 3 → 4 → 5
```

4-card straight.

```text
1 → 2 → 3 → 4 → 5
```

5-card straight.

The exact multipliers should remain simple.

---

# 18. Simple Damage / Block Formula

Do not use floating-point arithmetic.

Use integer percentages.

For example:

```text
Base value = sum of the active cards' values
```

In the attack phase, "active" cards are the attack cards (`SW/BO/FI`) plus heals
(`HE`) when they resolve as healing. Shields contribute **0** to the base.

Then apply a combo multiplier.

Examples (attack phase):

```text
SW2 + BO3 + SW4            base = 9
3-card straight = ×150%    final damage = 13
```

```text
SW1 + SH2 + SW3            base = 4   (shield adds 0)
3-card straight = ×150%    final damage = 6
```

```text
SH2 alone                  base = 0
no straight                final damage = 0  (card consumed, no effect)
```

Use integer arithmetic:

```text
value = base * multiplier / 100
```

Possible initial multipliers:

```text
Normal combo:    100
3-card straight: 150
4-card straight: 175
5-card straight: 200
```

These numbers can be tuned later.

---

# 19. Card Types

## SW — Sword

Physical attack.

Example:

```text
SW3
```

Description:

```text
Sword attack. Deals physical damage.
```

Damage contribution:

```text
3
```

---

## SH — Shield

**Block value in the defend phase; combo fodder (0 damage) in the attack phase**
(see §1.5).

Example:

```text
SH5
```

Description:

```text
Blocks incoming damage.
```

When used defensively:

```text
Defense = 5
```

When used in an attack combo it adds **0** to the damage base, but its number
still counts toward straight detection and the combo multiplier — so it can
bridge or extend an attack straight without dealing damage itself.

Multiple Shield cards can be combined into one block in the defend phase.

> **DONE:** Implemented in `src/battle/combo.c` and `src/battle/battle.c`.
> In the attack phase, shields are combo fodder (0 damage base, participate in
> straight and flush multipliers). In the defend phase, shields provide block
> value and build defensive combos, while non-shields are inert (0 block, 0 combo).
> Regression-covered by `tools/scenarios/tests/card_shield_defend.json`,
> `tools/scenarios/tests/card_shield_attack_fodder.json`, and
> `tools/scenarios/tests/card_defend_inert_nonshield.json`.

---

## BO — Bow

Ranged physical attack.

Example:

```text
BO4
```

Description:

```text
Ranged physical attack.
```

Initially, Bow can behave almost identically to Sword.

Its distinction can be expanded later.

---

## Fire — sword element

There is no Fire *card type*. Fire (and later Ice) is an **element carried on
a weapon**: the Fire Sword renders as `SW4` and has the BURN on-hit rider
(`STATUS_BURN`, see `docs/loot.md §34`). Example:

```text
F SW   (battle code SW4)
```

Description:

```text
Sword: physical (with a chance to burn the target)
```

Elemental weaknesses can be added later; the fire effect walks a sword until
then.

---

## HE — Heal

Restores HP.

Example:

```text
HE5
```

Description:

```text
Restores 5 HP.
```

For the first version, simply restore the number shown on the card.

---

# 20. Attack Resolution

When SELECT is pressed:

1. Stop player input.
2. Stop the timer.
3. Evaluate the selected cards.
4. Determine whether a straight exists.
5. Calculate base damage/effect.
6. Apply combo bonus.
7. Display the result.
8. Play a short animation.
9. Apply the result to the enemy.
10. Check for victory.
11. Begin enemy turn.

Example:

```text
COMBO: SW2 BO3 SW4

2 → 3 → 4

STRAIGHT!

DAMAGE: 13
```

Then:

```text
ENEMY HP
30 → 17
```

> **DONE:** The attack phase resolves a combo against the enemy this way —
> the lead card type decides the action (`HE` heals the player, all offensive types
> damage the enemy), computed via `battle_eval_current_combo` in
> `src/battle/battle.c::battle_execute_combo`. Shields in an attack combo act as
> combo fodder: they add 0 damage to the base while contributing to straight
> and flush multipliers. Combed damage/heal is applied, the played cards are
> discarded and replaced from the deck, and victory/defeat is checked.

---

# 21. Simple Attack Animation

Do not require sophisticated graphics.

For the first version:

```text
COMBO!
```

then:

```text
ATTACK!
```

then:

```text
-13
```

Then update HP.

If a placeholder projectile is desired, a simple sprite moving across the screen is sufficient.

The combat system must work perfectly without the animation.

---

# 22. Enemy Turn

Use a single enemy initially.

Example:

```text
SLIME
HP 30/30
```

The enemy can have extremely simple AI.

For example:

```text
Normal attack
Normal attack
Heavy attack
Normal attack
```

or a small weighted random table.

The enemy does not need to use the card system yet, but will in the future.

> **DONE (updated):** Enemies now draw from per-enemy card decks
> (`src/battle/enemy_deck_content.c`, registered game content). After the
> player's attack resolves, the battle enters an enemy telegraph phase that
> plays the enemy deck's next card (`EVENT_ENEMY_CARD_PLAYED`) and sets the
> incoming damage to that card's value, then hands the player the defend phase
> (`src/battle/battle.c::battle_update`). When the draw index wraps the deck,
> it cycles back to the top. A fallback flat attack (value 3) remains only for
> enemies with no registered deck.

---

# 23. Defense

When the enemy telegraphs an attack:

```text
SLIME ATTACK!
DAMAGE: 15
```

Switch the player into the **defend phase**.

The hand is shown; all five cards are still present and cursor-selectable:

```text
SW2  SH5  BO3  SH4  HE7
      ^
```

Only **Shield** cards build the block. The non-shields (`SW2 BO3 HE7`) are
fully **inert** here — they add 0 to the block and 0 to any defensive combo
(§1.5). Selection is not prevented; they simply do nothing if played.

The player selects Shield cards:

```text
COMBO: SH4 SH5
```

SELECT executes the defense.

Calculate:

```text
Block   = 4 + 5  (+ straight/flush multiplier if any)
Enemy damage = 15
Damage taken = damage - block
```

Display:

```text
BLOCK: 9

TAKEN: 6
```

Then continue the battle.

> **DONE:** The defend phase blocks incoming enemy damage equal to the
> combo-evaluated shield value (straight/flush bonuses apply), applied once to
> the *next* enemy attack only (`src/battle/battle.c::battle_execute_combo`
> `PLAYER_DEFEND`). Non-shields are inert (0 block, 0 combo). Regression-covered
> by `tools/scenarios/tests/card_shield_defend.json` and
> `tools/scenarios/tests/card_defend_inert_nonshield.json`.

---

# 24. Healing

If the player selects:

```text
HE5
```

and presses SELECT:

```text
+5 HP
```

The player's HP updates immediately.

For the initial prototype, Heal does not need complicated combo rules.

> **DONE:** Healing works in the current build — if the combo's lead card is `HE`,
> the player's HP is restored (capped at max) instead of dealing damage
> (`src/battle/battle.c::battle_execute_combo`, `PLAYER_SELECT` branch), and emits
> a `HEALED` telemetry event.

---

# 25. Multiple Targets

Do not implement complicated target selection initially.

With one enemy:

**Target automatically.**

When multiple enemies are eventually added:

* LEFT/RIGHT changes target.
* A can confirm the target if necessary.
* A small arrow identifies the selected enemy.

Example:

```text
        ↓
     [SLIME]

     [BAT]
```

This can be added after the one-enemy prototype works.

---

# 26. Inventory

START should continue opening the existing inventory.

While inventory is open:

* combat is paused
* timer is paused
* current hand remains unchanged
* current combo remains unchanged
* cursor position remains unchanged

Example:

Before inventory:

```text
COMBO: SW2 BO3

SW2 SH5 BO3 SW4 HE7
           ^
```

Open inventory with START.

Close inventory.

Return to:

```text
COMBO: SW2 BO3

SW2 SH5 BO3 SW4 HE7
           ^
```

Nothing should reset.

---

# 27. Sound Design

Because the graphics are extremely simple, sound is important.

Use short Game Boy sound effects.

### Card selection

Short click.

### Undo

Lower click.

### Combo detected

Short rising tone.

### Straight

Distinct positive sound.

### Attack

Short impact.

### Defense

Block sound.

### Heal

Positive ascending tone.

### Low timer

Periodic warning beep.

### Victory

Simple victory jingle.

These sounds can provide much of the feedback that graphics cannot.

---

# 28. Timer Audio

When the timer becomes low, the game should provide additional feedback.

For example:

```text
████████████████████
```

normal.

Then:

```text
██████
```

timer begins flashing.

Play a short warning beep at intervals.

This allows the player to recognize that they need to commit their action without constantly watching the bar.

Do not make the warning too aggressive initially.

---

# 29. Game State Machine

The combat engine should be implemented as an explicit state machine.

Suggested states:

```text
BATTLE_START
    ↓
PLAYER_DRAW
    ↓
PLAYER_SELECT
    ↓
PLAYER_COMBO
    ↓
PLAYER_RESOLVE
    ↓
CHECK_BATTLE_END
    ↓
ENEMY_TURN
    ↓
ENEMY_ATTACK
    ↓
PLAYER_DEFENSE
    ↓
DEFENSE_RESOLVE
    ↓
CHECK_BATTLE_END
    ↓
NEXT_TURN
```

Additional temporary states:

```text
ATTACK_ANIMATION
DAMAGE_DISPLAY
HEAL_DISPLAY
COMBO_DISPLAY
VICTORY
DEFEAT
INVENTORY
```

> **DONE:** The engine runs an explicit state machine in `src/battle/battle.c`
> (`battle_start` → `PLAYER_SELECT` → `PLAYER_ANIM` → `ENEMY_TELEGRAPH` →
> `PLAYER_DEFEND` → `DEFENSE_RESOLVE`, cycling back to `PLAYER_SELECT`), with
> `RESULT`/`battle_over` exit and telemetry for start/won/lost/fled/entity-defeated.

---

# 30. Timer State

The timer should be tied specifically to the player decision state.

For example:

```text
PLAYER_COMBO
    ↓
timer running
    ↓
SELECT
    ↓
timer stopped
    ↓
PLAYER_RESOLVE
```

Or:

```text
PLAYER_COMBO
    ↓
timer reaches zero
    ↓
automatically resolve
```

Never let the timer run during:

* animations
* dialogue
* inventory
* damage display
* victory/defeat
* transitions

> **DONE:** The decision timer runs only in `PLAYER_SELECT`/`PLAYER_DEFEND`
> and decrements once per frame; it targets 20s (`BATTLE_TIMER_MAX_FRAMES 1200`)
> with a 60-division bar. On expiry, `battle_update` auto-executes the current
> combo (and if none was selected, auto-selects the card under the cursor). It is
> suspended during animation/delay phases (`battle.c::battle_update`).

---

# 31. Deck

Initially use a small fixed deck.

Example:

```text
SW2
SW3
SW4
SW5

SH2
SH4
SH5

BO2
BO3
BO5

SW3
SW4

HE2
HE5
```

Start with a deterministic deck order for debugging.

Once the system works, implement shuffling.

The player should draw replacement cards after cards are played according to the eventual deck rules.

> **DONE:** The deck system is implemented: a Fisher-Yates shuffle with uniform
> reject-and-retry, draw + discard, and draw-to-replace after each resolved combo
> (`battle_resolve_hand_discard` draws one replacement per played card). Cards are
> represented as compact type+value structs, not strings. Battles always draw from
> the player's persistent deck; the fallback deck (`deck_init_default_banked`
> in `src/battle/deck_init.c`, reading the generated `g_hero_starter_deck`
> table from `screens/hero.json` via `battle_compile.py`, unpacked through the
> WRAM banked-call trampoline) mirrors the granted starter deck and only serves
> empty/legacy state. Removals are floored at `DECK_MIN_CARDS` (5, one full hand)
> by an engine backstop in `deck_remove_card`. Deck composition and management
> rules live in `docs/deck.md` and `docs/deck-management.md`.

---

# 32. Randomness

Do not introduce lots of randomness in the first version.

The first prototype should be deterministic enough to test combos.

Randomization can later control:

* deck shuffle
* enemy actions
* damage variation
* special effects

The core card-combination system should remain predictable.

---

# 33. Memory and Hardware Considerations

The prototype should avoid:

* large card sprites
* individual sprite objects for every card
* floating-point calculations
* dynamic memory allocation
* large strings stored repeatedly
* complicated particle systems
* unnecessary animation frames

Prefer:

* card IDs
* small fixed arrays
* integer arithmetic
* lookup tables
* tilemap rendering
* reusable icons/tiles
* fixed-size battle structures

The hand can simply be an array of five card IDs.

The current combo can be another small fixed array.

---

# 34. Recommended Initial Data Structures

Conceptually:

```text
Card
    type
    number
    power
```

Player:

```text
Player
    hp
    max_hp
    deck[]
    hand[5]
    combo[]
    combo_length
    cursor_position
```

Battle:

```text
Battle
    state
    enemy_hp
    enemy_max_hp
    timer
    selected_target
```

There is no need for anything more complicated in the first prototype.

---

# 35. Rendering Strategy

Use the Game Boy background tilemap for:

* HP text
* enemy text
* combo text
* card text
* descriptions
* timer bar
* cursor

Use sprites only for:

* player character
* enemy
* optional attack effect

This makes the combat UI cheap to render and easy to update.

---

# 36. Timer Rendering

The timer can be represented by a row of repeated tiles.

For example, a 20-tile bar:

```text
████████████████████
```

Each timer tick removes one or more tiles from the **right side**.

This is visually simple and cheap.

Conceptually:

```text
timer_tiles = 20
```

As time decreases:

```text
20 → 19 → 18 → ... → 1 → 0
```

Only redraw the affected section rather than rebuilding the entire screen.

---

# 37. First Prototype Battle

The first playable battle should contain:

### Player

```text
HP: 40
```

### Enemy

```text
SLIME
HP: 30
```

### Hand

```text
SW2 SH5 BO3 SW4 HE7
```

### Turn

Player gets a fixed decision period.

The timer starts:

```text
████████████████████████████████
```

Player selects:

```text
SW2 → BO3 → SW4
```

The screen shows:

```text
COMBO: SW2 BO3 SW4
```

SELECT executes.

The game detects:

```text
2 → 3 → 4
STRAIGHT!
```

Calculates damage.

Displays:

```text
DAMAGE: 13
```

Enemy HP becomes:

```text
17/30
```

Enemy attacks.

Player receives a defense opportunity.

Player selects Shield cards.

SELECT resolves the defense.

The loop continues until:

```text
VICTORY
```

or:

```text
DEFEAT
```

> **Design note for this battle:** with the attack/defend split (§1.5), the shield
> `SH5` in the hand is the pivot. Use it as combo fodder now (its `5` could extend
> an attack straight at 0 damage) and lose the block for the enemy's attack — or
> save it to eat most of the incoming hit. That trade is the game.

---

# 38. Development Order

Implement the system in this exact order.

### Phase 1 — Card Rendering

Make the Game Boy display:

```text
SW2 SH5 BO3 SW4 HE7
```

with a movable cursor.

### Phase 2 — Input

Implement:

```text
LEFT / RIGHT
A
B
SELECT
START
```

and verify:

* A selects
* B undoes
* SELECT executes
* START opens inventory

### Phase 3 — Combo

Implement:

```text
2 → 3 → 4
```

detection.

### Phase 4 — Timer

Add the full-width bottom bar.

Make it shrink from right to left.

### Phase 5 — Attack

Add damage calculation and enemy HP.

### Phase 6 — Defense

Add Shield cards and enemy attacks.

### Phase 7 — Heal

Add HE cards.

### Phase 8 — Enemy AI

Add simple enemy behavior.

### Phase 9 — Deck

Add card drawing and shuffling.

### Phase 10 — Presentation

Only after the combat works:

* improve sprites
* add attack animations
* add more card types
* add elemental interactions
* add better sound
* add more sophisticated combos

---

# 39. Final Prototype UI

The target experience should look roughly like this:

```text
┌──────────────────────────────┐
│ SLIME              HP 17/30  │
│                              │
│          [SLIME]             │
│                              │
│ HERO               HP 40/40  │
│             DECK: 10         │
│                              │
│ COMBO: SW2 BO3 SW4           │
│                              │
│ SW2  SH5  BO3  SW4  HE7      │
│              ^               │
│ Sword attack. Deals damage.  │
│██████████████████████████████│
└──────────────────────────────┘
```

The player's mental process should be:

> "I'm highlighting SW4. It says what it does at the bottom. I've already selected SW2 and BO3. I have 2-3-4, so that's a straight. I press SELECT."

That is the entire core loop.

The prototype does **not** need beautiful cards, elaborate menus, or sophisticated animations.

It needs to make this interaction feel **fast, obvious, and satisfying**.

## Core rules to preserve

**A = select card**

**B = undo**

**SELECT = execute**

**START = inventory**

**D-Pad = navigate**

**Bottom line = always-visible card description**

**Very bottom line = right-to-left turn timer**

**Cards = two letters + number**

**Combos = primarily based on card numbers**

**Attack phase = attack cards deal damage, shields are combo fodder (0 damage)**

**Defend phase = shields block, everything else is inert**

**Used cards are always consumed, even if the play had no effect**

**Graphics = tile/text based wherever possible**

That should be the foundation of the first playable Game Boy implementation.
