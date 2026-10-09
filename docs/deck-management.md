# Deck Management — CARDS Menu & Starter Deck

Authoritative spec for the deck-management feature: the starter deck grant,
the unified CARDS menu, and the input contract around it. Semantics of the
underlying collection/deck engine live in `docs/deck.md`; this document owns
the *player-facing* management layer.

---

## 1. Starter deck

A new game grants a real, owned 12-card deck (collection **and** deck state in
`GameState.cards`, written by `game_new_game()` via the silent mutators
`deck_collection_add` / `deck_add_card` — no boot telemetry).  The contents
and order are data, not code: `screens/hero.json` `starter_deck` (ordered
draw-pile CardIds), compiled by `battle_compile.py` into
`src/game/hero_content.c` (`g_hero_starter_deck_ids`, bank 2) and editable
in the level editor's Hero view.  The default set is:

```text
IRON_SWORD    ×4   → SW3   (max_copies 4)
WOODEN_SHIELD ×3   → SH2   (max_copies 3)
FIRE_SWORD    ×3   → SW4   (unlimited uses; BURN rider)
POISON_DAGGER ×2   → DA1   (max_copies 3)
```

The deck is inserted in deal order SW SW SH SH SW first, then the extras, so
the opening battle hand is `SW3 SW3 SH2 SH2 SW4` and the draw pile starts at
7. The original five-card grant was enlarged so a fresh file can actually
cycle its deck (draw → discard → reshuffle) without shopping first.

Consequences:

- Battles always draw from the player's real deck. The fallback deck
  (`deck_init_default_banked` in `src/battle/deck_init.c`) remains only as
  a safety net for empty/legacy state and unpacks the same generated
  `g_hero_starter_deck` table through the card catalog, so it always
  mirrors the granted set byte-for-byte (including the first-five deal
  order).
- Healing is **not** free at the start. The only heal source is the shop's
  IRON RING card — **RG5** (power 5, `uses_per_battle` 3, price 20g).
  Buy it, then add it to the deck from the CARDS menu.
- Shop rows render the ITEM code derived from the card definition
  (`ui_card_code_str`: weapon-type code + power digit, e.g. `RG5`, `SW3` --
  a HEAL card is a ring, so the shop shows `RG` rather than its battle
  code `HE`) plus its `price` — no display strings are duplicated in the
  shop layer.
- The old 20-card starter table is gone; the deck grows toward the 20-card
  cap through play.

## 2. The CARDS menu

START opens the quick screen. It has two tabs: **CARDS** and **QUEST**
(LEFT/RIGHT switches). The former separate DECK tab is merged into CARDS:
every owned, deckable card is one list entry showing its deck membership.

### Layout

Each card occupies a **pair of rows**:

```text
  * FILTER/SORT *            ← top row (see §4)
> I SW                    4      ← name row: cursor + lore name + membership
  Sword: physical               ← description row (combat description)
  W SH                    3
  Shield: block dmg
  ...
```

- **Lore name**: `def->name`, the player-facing card name (e.g. `I SW`,
  `W SH`, `F SW`, `I RG`), shown in the CARDS tab and the detail page. The
  combat hand keeps the short `<battle_type><power>` code (`SW3`, `HE5`, ...)
  since that is where the abbreviation fits the HUD.
- **Description**: `card_get_description(battle_type)` — the same string the
  battle HUD shows.
- **Membership column** (rightmost): the decked-copy count as a digit
  (`0` = none, `1`..`n` = copies in the deck), so partial stacks (starter
  4x I SW / 3x W SH / 3x F SW, ring up to 3) are visible at a glance.
- SPECIAL cards (e.g. AMULET) are not deckable and are hidden from the list.
- ~6 pairs visible; scrolling moves by pair.

### Actions

| Input | Context | Effect |
|---|---|---|
| START | overworld / menu | open / close the menu |
| LEFT / RIGHT | menu | switch tab |
| UP / DOWN | card rows | move cursor by entry (UP from first card reaches the top row) |
| A | card row | add one copy; clear all copies when fully decked (§3) |
| SELECT | CARDS row | detail submenu (§5) |
| SELECT | QUEST row | placeholder quest detail text |
| B | main list, not on top row | jump to first card row (**step 1**) |
| B | on top FILTER/SORT row | close the menu (**step 2**) |
| B | any submenu | back out one level |

The two-step B is deliberate: B never closes the menu directly from the card
list, so an accidental press cannot lose your place.

## 3. Deck membership: count-up-then-clear (A)

A routes through the real mechanics — no UI-side shortcuts:

- A adds **one** copy via `deck_add_card()`. Emits `CARD_ADDED_TO_DECK`.
- Once the card is fully decked (the add is rejected for per-card reasons —
  no owned copies left beyond `max_copies`), A **clears every decked copy**
  of it via repeated `deck_remove_card()`. Emits `CARD_REMOVED_FROM_DECK`
  per copy.
- Rejections show a transient message line instead of a silent no-op:
  - `DECK FULL` — hard limit of **20** cards (`MAX_DECK_CARDS`).
  - `DECK MIN 5` — the clear is rejected all-or-nothing when the cards
    left after removing every copy of this card would drop the deck below
    **5** (`DECK_MIN_CARDS`, one full opening hand). Nothing is removed in
    that case. On the 12-card starter a single-card clear can no longer
    breach the floor (the largest stack is 4 swords, leaving 8), so the
    boundary is reached by clearing stacks in sequence — e.g. after the
    swords and shields are cleared, the fire-sword clear at deck 5 is
    rejected.
    `deck_remove_card()` enforces the same floor as an engine backstop for
    any other caller.

(The earlier per-press boolean toggle could never rebuild a multi-copy stack
— pressing A on a 2-copy row silently dropped it to 1 with no path back —
so the semantics were redesigned around the visible count digit.)

## 4. Filter/sort top row

The first list entry is `* FILTER/SORT *`. Reach it with UP from the first
card (or B's step-1 jump followed by UP). A opens a small picker (hint line
`LR CYCLE  A:OK B:NO`) where LEFT/RIGHT cycle the existing filter
(ALL/ATK/DEF/HEL/STS/UTL) and sort (OFF/TYPE/PWR+/PWR-/CST+/CST-)
values; A validates and applies them to the list; B backs out unchanged.

## 5. Detail submenu (SELECT)

SELECT on a highlighted entry opens a full-property view; B returns.

CARDS:

```text
NAME   WD RING
TYPE   HEAL
PWR    5        COST   1
USES   3/battle (- = unlimited)
MAX CP 3        PRICE  20
OWNED  x1       IN DECK x0
```

QUEST: a one-line placeholder (`no details yet`); per-quest detail is future
work.

## 6. Reshuffle semantics (combat)

Already implemented by the deck engine (`deck_draw`, `src/battle/deck.c`);
restated here because it constrains the menu's promises:

- Playing a card discards it and leaves the slot empty; the turn-start draw
  refills open slots from the draw pile (deck.md: no mid-turn instant
  redraw). Hands may sit below five cards whenever the pile is dry.
- When the draw pile is exhausted while a hand slot is still open and the
  discard pile can feed it, the discard pile is shuffled back in — but that
  reshuffle consumes the player's action for the cycle (re-deal, announce,
  enemy still attacks).
- `uses_remaining` travels inside the `Card` copy through discard/draw/
  reshuffle, so **limited cards stay depleted across reshuffles** while
  unlimited cards cycle freely. A depleted HE5 returns to the hand but is
  unplayable (skipped by cursor movement and auto-select).

## 7. Telemetry contract

No new events. The menu reuses:

- `CARD_ADDED_TO_DECK` / `CARD_REMOVED_FROM_DECK` (add / clear)
- `CARD_PURCHASED` / `CARD_PURCHASE_FAILED` (shop)
- `RENDER_SCREEN` (menu redraws)

State assertions use the extended snapshot's collection/deck sections; byte
offsets are a transport contract (AGENTS.md §53.7), scenarios must use the
semantic assertion names.

## 8. Scenarios

| Scenario | Proves |
|---|---|
| `cards_menu_toggle` | A on a maxed card clears every decked copy, adds rebuild the count, events emitted (13-card replaced deck) |
| `fallback_deck_starter` | emptying the persistent deck routes battles onto the packed starter-deck fallback (opening hand renders SW3 SW3 SH2 SH2 SW4 from the 12-card table) |
| `deck_min_floor` | removals walk 10 → 8 → grow to 11 → exactly 5; a further removal at the floor is rejected |
| `deck_min_ui_message` | real gameplay reaches the floor boundary: sword clear allowed, shield clear rejected with the transient DECK MIN 5 message |
| `reshuffle_turn` | one play per phase drains the pile in ~6 phases; the dry-pile reshuffle turn fires once and consumes the action |
| `reshuffle_preserves_limits` | HE5 (ring) depletes after 3 plays and stays dead across reshuffle; SW keeps cycling |
| `herb_purchase_flow` | buy herb → add to deck → battle heal 5 |

Existing scenarios bound to the old tab/filter/sort keybindings are updated
or retired alongside this change.
