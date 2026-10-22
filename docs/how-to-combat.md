# How to Fight — Combat Tutorial

This is the player's guide to battles. Numbers below match the game
(`docs/technical-manual.md` §6/§8 is the authoritative spec); the
in-game TUTORIAL slides (title menu) are the pocket summary of this page.

## The goal

Reduce every enemy's HP to 0 before yours reaches 0. Most fights are
against a **trio** of monsters (clones of one type); bosses fight alone.
You always strike first each round, then block.

## First spar: Carl

The field holds a single slime, **CARL**, in a grove of trees, old
stumps and grass tufts. Walk into him and a dialogue box opens first:
it is dangerous to go alone, and he will teach you how to fight -- his
full field guide (targeting, swords, shields, bows, `HE` rings that hit
for 0 but heal and build poker hands, riders included,
repeated on every later visit). Close the lesson and the spar starts by
itself — no second bump needed. Pressing A while facing him teaches
and spars exactly the same. He never leaves: bump him any time to
hear the guide again.

![Carl waits on the field](../screenshots/08-carl-field.png)

East of him sits a treasure chest with your first POISON_DAGGER (the
starter deck carries none); the Merchant sells more. Bumping the chest
also plays a reward speech and grants **+1 action point**, same as beating
Carl — whichever comes first wins, the second is a harmless no-op, so the
order never matters.

He is a friendly practice dummy whose every word is green, so you know
it is him speaking.

![Carl's lesson](../screenshots/08-carl-tutor.png)

Walk into him to spar: a solo fight against his 10 HP. His practice
deck deals **0 damage**, so you cannot lose — learn the round below at
your own pace (the battle timer still runs, but nothing can hurt you).
There is no target arrow over Carl: with a single foe there is nothing
to aim at. On your turn the top reads `CARL: PICK CARDS`, then
`DO POKER HANDS.` below it. The screen coaches the buttons too:
`L/R` moves the cursor, `A` chooses the card, `SELECT` confirms the
hand — same wording every phase.

![Pick cards, then SELECT](../screenshots/09-battle.png)

On his turn he coaches the block (`SHIELD` cards stop his swing,
`SELECT` defends).

![Block with shields, then SELECT](../screenshots/10-battle-defend.png)

Beat him and he gives a graduation speech: real fights begin, AP
limits how many cards you can play (your pool starts at **1 AP** per
phase and grows by story rewards, up to **5**). Bumping into him
afterwards repeats the whole field guide from his lesson (swords,
shields, bows, rings that hit for 0 but heal and build poker hands,
riders) — but never the extra AP.

![Good luck, and +1 AP](../screenshots/11-victory-speech.png)

## One round, step by step

```
1. PLAY YOUR ATTACK  -- pick up to 5 cards, press SELECT
2. Watch it land     -- damage or healing resolves
3. ENEMY TELEGRAPH: the foe shows its card,
   its value is the damage coming at you
4. DEFEND            -- play shields to block that damage
5. Resolve, refill, next round
```

## Controls

| Button | Action |
|---|---|
| `LEFT` / `RIGHT` | Move the hand cursor |
| `UP` / `DOWN` | Change target (matters vs trios; the `^` caret marks it) |
| `A` | Select the hovered card into your combo |
| `B` | Undo last selection. With **nothing selected in the attack phase**, `B` **flees** |
| `SELECT` | **Execute** the current selection |
| `START` | Quick screen (pauses battle, resumes where you left off) |

Selected cards show digits 1-5 in the order you picked them. Order
matters only for one thing: the **first (leading) card** decides what an
attack *does* (see below).

## Hand, energy, timer

- **Hand**: 5 cards, drawn from your deck. Played cards are discarded
  and replaced at the round boundary.
- **Energy**: **2 per phase** at first — attack *and* defend each get a
  fresh pool. Beating Carl's practice spar raises it to **3 per phase**
  (opening the field cache does the same, idempotently).
  Every card shows a cost; selecting reserves it, resolving pays it.
  A card you can't afford can't be selected (`NO ENERGY!`).
- **Timer**: **20 seconds** per decision, shown as the bottom bar. Expiry
  auto-executes whatever you selected (or auto-picks the hovered card if
  you picked nothing and something is playable). It never runs during
  animations.

## Attacking: cards + combos

Each card has a **value** (its power) and a **symbol** (its type):

| Card | Attack does | Defend does |
|---|---|---|
| `SW` Sword | damage (only if in the poker hand) | nothing (0) |
| `BO` Bow | damage (only if in the poker hand) | nothing (0) |
| `DA` Dagger | damage (may poison, only if in the hand) | nothing (0) |
| `SH` Shield | **0 damage** — but counts for combos | **blocks** its value (only if in the hand) |
| `HE` Heal (rings) | converts the hand to healing (see below) | 0 block — but wildcards the hand + gates over-block heal |

Every card shows its energy cost as a pip in its top-left corner (one pip per AP; free cards show none).

Your selection ranks **like a poker hand**. Better hands multiply the
whole play:

| Hand | Needs | Multiplier |
|---|---|---|
| high card (`HCARD`) | anything / 1 card, scores highest only | 100% |
| PAIR | 2 same value | 120% |
| TWO PAIR | 2 pairs | 150% |
| THREE KIND | 3 same value | 180% |
| STRAIGHT | all 5 sequential | 210% |
| FLUSH | all 5 same symbol | 240% |
| FULL HOUSE | trips + pair | 260% |
| FOUR KIND | 4 same value | 280% |
| STR FLUSH | 5 sequential, same symbol | 350% |
| FIVE KIND | all 5 same value | 400% |

Same-symbol hands get **+25%** on top. Final effect, always rounded
down: `base × multiplier / 100`, where **base counts only the cards
forming the poker hand** — extras deal/block 0. A lone high card scores
just itself (`SW2 + BO3` with no pair hits for 3, not 5); a
`SW1 + SW1 + SW2` pair hits for (1 + 1) × 120% = 2, the `SW2` adding
nothing.

The **leading card** decides the action:

- **Sword/bow/dagger lead** → damage. Base = sum of the
  *participating* non-shield, non-ring values. Shields in the mix add 0
  damage but still shape the hand — `SW1 SH1 DA1` is three-of-a-kind
  (×180%) on a base of just `1 + 1 = 2`, dealing 3.
- **Ring (`HE`) lead (or any ring in the mix)** → the whole hand
  converts to healing: no damage at all. You gain the scaled
  *participating* value with the ring counting as its joker value — a
  `SW1` + ring PAIR heals (1 + 1) × 120% = **2 HP**. A lone ring heals
  its face value. There is no way to mix damage and healing in one
  play: ring in, damage out.
- **Shield lead** → still deals the participating non-shield damage sum
  (shields are "fodder that counts"). Only a ring turns an attack into
  a heal.

Worked example: `SW2 + SW2` is a PAIR — base 4 × 120% = **4 damage**.

The COMBO row above your hand previews the tier live as you select.
Playing junk that makes nothing still **eats the cards** — a lone `SH1`
on attack does 0 and is gone. That trade (burn a shield for a bigger
attack now vs save it to block next) is the core decision of the game.

## Defending

After the telegraph (`ENEMY ATTACK!`, e.g. damage 3), only **shields**
(and rings) do anything. Swords, bows, daggers, played here are fully
inert — 0 block, 0 combo — though the game still lets you select them
(and still eats them, so don't).

```
net = incoming - (participating shields, scaled)
net > 0  -> you take that much
net <= 0 -> no damage; over-block heals you ONLY with a ring in the mix
```

Shields outside the poker hand block 0: `SH2 + SH2 + SH5` (pair of
twos, five kicker) blocks (2 + 2) × 120% = 4, the `SH5` adding nothing.

Worked example: incoming 3, you play `SH2` → take **1**.

## Rings and healing

Rings (`HE`) are the game's healing vector — the merchant sells an
iron ring once his shop opens, and loot can contain more:

- In **attack**, a ring is a **joker**: any value 1-10, best tier kept
  (ties go to the higher value). It converts the whole hand to healing
  — no damage is dealt. The heal is the scaled participating value with
  the ring counting as its joker value.
  Example: `SW5 + RING2` reads as a PAIR of fives — (5 + 5) × 120% =
  **12 HP healed**, 0 damage.
- In **defend**, a ring is a **wildcard** for the hand (enabling pairs,
  kinds, straights) and gates the over-block heal — but it adds **0
  block** itself. Example: `SH3 + SH3 + RING` reads as THREE KIND and
  blocks (3 + 3) × 180% = **10**, the ring adding nothing to the sum.
- **Max one ring per selection** — a second attempt flashes `ONE RING!`.

## Status effects

| Status | Does | From |
|---|---|---|
| POISON | 1 HP/round **and greys out 2 of your hand cards** for 2 rounds | poison daggers, bat/spider swings |
| BURN | 1 HP/round | fire swords |
| FREEZE | you skip your whole action (attack *and* the incoming block); enemies skip their swing | ice swords |

Greyed cards can't be selected (cursor skips them). Re-poisoning
refreshes the duration, not which cards are grey. Failed applications
show as resisted.

## Enemies and their decks

Each enemy draws **one card per attack turn**; the card's value is the
damage you must block (announced up front, so defending is never a
guess). Rough guide: slimes hit 2-3, bats up to 4 (one swing poisons),
kobolds 2-3, spiders up to 3 (one webs/poisons), the mimic 2-4,
trios hit 2-3 per round, the deckless boss swings a flat 3. Exhausted
enemy decks cycle.

## Running out of cards

Cards have **uses per battle** (unlimited ones never run out); a spent
card stays in your deck but can't be played (`OUT OF USES!`). When the
draw pile can't refill your hand, the game spends that round
**reshuffling** the discards back in — the enemy still attacks that
round, so don't get caught empty-handed.

## Winning, losing, running

- **Victory**: all enemies down. Fanfare, a loot card reveal (`YOU
  FOUND:` — exactly one card per victory), gold (slimes 5, bats 8,
  kobolds 8, spiders 15, boss 50), HP carries into the overworld, the
  monster stays dead.
- **Defeat**: HP 0 → Game Over screen.
- **Flee**: `B` with an empty selection in the attack phase. No reward,
  the enemy survives. (No fleeing mid-defend — weather the hit first.)
