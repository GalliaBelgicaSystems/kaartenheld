# RPG Foundation Architecture

## Purpose

This repository is evolving into a **lightweight, reusable Game Boy RPG foundation**.

It is not intended to become a general-purpose game engine comparable to Godot, Unity, or similar tools.

Instead, the goal is much narrower:

> Build the reusable infrastructure that repeatedly appears when creating a small RPG for the Game Boy, while allowing each individual game to define its own content, rules, presentation, and identity.

The foundation should emerge organically from the needs of the games being built with it.

The guiding principle is:

> **Build the game first. Generalize only where the game reveals a reusable problem.**

This repository is therefore both:

1. a working RPG prototype/reference implementation; and
2. a potential starting point for future Game Boy RPG projects.

---

# 1. What Is an RPG Foundation?

There is no universally accepted definition of an RPG.

Different RPGs may have:

* turn-based combat;
* real-time combat;
* no combat;
* party-based progression;
* a single protagonist;
* equipment systems;
* card systems;
* skill trees;
* crafting;
* quests;
* branching dialogue;
* procedural worlds;
* highly scripted stories.

The foundation should therefore **not attempt to encode a definition of the RPG genre**.

Instead, it should provide a collection of reusable primitives that are common across many RPGs.

The current working model is:

```text
                    RPG
                     │
       ┌─────────────┼─────────────┐
       │             │             │
     WORLD       GAME STATE      STORY
       │             │             │
     Scenes          Party        Dialogue
     Actors          Stats        Events
     Maps            Items        Flags
     Movement        Progression  Variables
     Collision       Inventory
       │             │
       └─────────────┼─────────────┘
                     │
                Persistence
                     │
                 Save / Load
```

Surrounding all of this are:

```text
Presentation
    Screens
    UI
    Audio
    Input

Development Infrastructure
    Debug protocol
    Telemetry
    Scenario harness
    Emulator automation
    Assertions
```

---

# 2. Foundation vs. Game

The most important architectural distinction is between **foundation** and **game-specific content**.

The foundation should provide mechanisms.

The game should provide meaning.

For example:

```text
FOUNDATION                  GAME
────────────────────────────────────────
Actor                       Mayor
Actor                       Guard
Actor                       Slime

Scene                       Town
Scene                       Forest

Dialogue                    Mayor greeting
Dialogue                    Guard warning

Item                        Potion
Item                        Phoenix Sword

Stats                       Strength = 12
Progression                 Slime Hunter rank

Battle                       Baten Kaitos-style battle
Event                        Mayor introduction
```

The foundation should not know that a Mayor exists.

The game should define the Mayor using foundation primitives.

---

# 3. Core Foundation Systems

The current target foundation consists of the following systems.

## 3.1 World

The world system represents the places where gameplay occurs.

It includes:

* scenes;
* maps;
* scene IDs;
* actor placement;
* movement;
* collision;
* transitions between scenes.

A game should be able to define many scenes without adding scene-specific logic to the engine.

For example:

```text
SCENE_OVERWORLD_01
SCENE_OVERWORLD_02
SCENE_TOWN
SCENE_FOREST
SCENE_CASTLE
```

The world system should treat these as data.

---

## 3.2 World Actors

All character-like entities in the world should use a common actor abstraction.

Examples:

```text
Hero
Mayor
Guard
Slime
Merchant
Boss
Villager
```

The foundation should not maintain separate architectural systems for "NPCs" and "enemies" unless a concrete requirement makes that necessary.

A World Actor can have properties such as:

```text
ID
position
facing
visual
flags
interaction
dialogue
battle
runtime state
```

Hostility is one actor property.

Therefore:

```text
Mayor
    hostile = false
    interaction = dialogue

Guard
    hostile = false
    interaction = dialogue

Slime
    hostile = true
    interaction = combat
```

The overworld does not need to know that the Slime is an "enemy."

It only needs to know that it has encountered a World Actor whose engagement behavior results in combat.

This also establishes a natural future foundation for aggro systems.

---

# 4. Interaction

Actors should have a generic interaction model.

The initial interaction types may include:

```text
NONE
DIALOGUE
COMBAT
```

Future games may add:

```text
SHOP
ITEM
EVENT
SCENE_TRANSITION
```

The important architectural principle is that movement/collision code should not contain actor-specific behavior.

Instead:

```text
Player movement
      ↓
Actor collision
      ↓
Actor engagement
      ↓
Interaction type
      ├── Dialogue
      ├── Combat
      ├── Shop
      └── Event
```

This keeps gameplay systems independent.

---

# 5. Dialogue

Dialogue is a reusable RPG primitive.

The foundation should provide mechanisms for:

* identifying dialogue;
* starting dialogue;
* advancing dialogue;
* ending dialogue;
* branching where required;
* connecting dialogue to game state.

The game provides:

```text
MAYOR_GREETING
GUARD_WARNING
SLIME_INTRODUCTION
```

The dialogue system should not contain story-specific assumptions.

---

# 6. Scripted Events and Scenes

Scripted events are one of the most important foundation systems.

Many RPG interactions can be described as sequences of actions:

```text
Hero enters town
    ↓
NPC approaches
    ↓
NPC faces hero
    ↓
Dialogue starts
    ↓
Dialogue ends
    ↓
Game flag changes
    ↓
Battle starts
    ↓
Battle ends
    ↓
NPC disappears
```

The foundation should eventually provide a generic event/action mechanism capable of expressing these sequences.

Conceptually:

```text
Event
 ├── Move actor
 ├── Face actor
 ├── Start dialogue
 ├── Wait
 ├── Set flag
 ├── Set variable
 ├── Give item
 ├── Start battle
 ├── Change scene
 └── End event
```

A specific game should define the event:

```text
EVENT_MAYOR_INTRO
```

rather than implementing a bespoke C function for the Mayor.

This is particularly valuable for automated testing and LLM-driven development.

---

# 7. Persistent Game State

Persistent state is a fundamental RPG concept.

The foundation should eventually provide a coherent model for:

```text
World state
Party state
Character state
Inventory
Progression
Flags
Variables
Quest state
```

For example:

```text
GAME STATE
│
├── World
│   ├── current scene
│   ├── actor states
│   └── world flags
│
├── Party
│   ├── characters
│   ├── levels
│   ├── XP
│   ├── stats
│   └── equipment
│
├── Inventory
│   ├── items
│   └── quantities
│
└── Progression
    ├── quests
    ├── flags
    └── variables
```

This state should be independent from the current screen.

A screen is a presentation of state.

The state itself should survive transitions between screens.

---

# 8. Items and Inventory

Item management is one of the strongest common RPG primitives.

The foundation should provide generic concepts such as:

```text
ItemId
ItemDefinition
Inventory
quantity
```

Potentially:

```text
Equipment
Consumables
Key Items
Materials
Weapons
Armor
```

However, the foundation should not assume a particular inventory UI or item taxonomy unless repeated games demonstrate that those assumptions are genuinely reusable.

For example:

```text
Foundation:
    ITEM
    INVENTORY
    QUANTITY
    EQUIPMENT

Game:
    POTION
    PHOENIX_SWORD
    FIRE_SWORD
    MAYOR_KEY
```

---

# 9. Stats and Progression

RPGs commonly have some form of progression.

This may apply to:

* playable characters;
* enemies;
* NPCs;
* weapons;
* equipment;
* skills;
* items;
* reputation;
* other game-defined entities.

The foundation should therefore avoid assuming that progression only means:

```text
character XP → character level
```

Instead, progression should be treated as a reusable concept.

Examples:

```text
Character XP
Character Level
Weapon Level
Skill Level
Reputation
Relationship Value
```

The specific progression rules belong to the game.

For example:

```text
Foundation:
    progression value
    progression threshold
    progression update

Game:
    level = floor(xp / 100)
```

The foundation provides mechanisms.

The game provides formulas and rules.

---

# 10. Combat

Combat is likely to be part of the foundation, but **the foundation should not dictate a particular combat system**.

The foundation should provide the ability to:

```text
start battle
identify combatants
return battle result
award rewards
return to world
```

A particular game may implement:

```text
Baten Kaitos-style card combat
```

while another game might implement:

```text
traditional turn-based combat
```

or:

```text
real-time encounters
```

Therefore:

```text
Foundation:
    battle lifecycle
    combatant abstraction
    battle entry/exit
    result handling

Game:
    cards
    decks
    elemental rules
    turn order
    damage formulas
```

The current card RPG is therefore the first implementation that exercises this abstraction rather than necessarily defining the final foundation architecture.

---

# 11. Save and Load

Save/load is a core foundation capability.

The save system should serialize the persistent game state rather than individual screens.

Conceptually:

```text
Save
 └── GameState
      ├── WorldState
      ├── PartyState
      ├── InventoryState
      ├── ProgressionState
      ├── Flags
      └── Variables
```

Loading should reconstruct the game state and allow the game to resume from the appropriate screen/scene.

The save system should not need to know about:

```text
Mayor
Slime
Fire Bow
Town
```

unless those are represented as generic state data.

---

# 12. Game Flags and Variables

Flags and variables are fundamental to scripted RPGs.

Examples:

```text
FLAG_MET_MAYOR
FLAG_DEFEATED_SLIME_BOSS
FLAG_TOWN_GATE_OPEN
```

Variables may represent:

```text
PLAYER_GOLD
MAYOR_REPUTATION
QUEST_PROGRESS
WORLD_COUNTER
```

Events and dialogue should be able to read and modify these values.

This enables:

```text
if FLAG_MET_MAYOR
    use alternate dialogue
```

or:

```text
if QUEST_PROGRESS >= 3
    open town gate
```

Flags and variables should be part of persistent game state.

---

# 13. Screens and Presentation

The foundation should provide reusable screen/state infrastructure.

Examples:

```text
TITLE
OVERWORLD
DIALOGUE
BATTLE
MENU
INVENTORY
GAME_OVER
SAVE
```

A screen should consume game state rather than own the authoritative state.

For example:

```text
GameState
    ↓
Overworld Screen
```

rather than:

```text
Overworld Screen
    └── owns player state
```

This distinction is important for:

* save/load;
* debugging;
* scenario initialization;
* screen transitions;
* automated testing.

---

# 14. Audio

Audio infrastructure belongs in the foundation.

This includes:

* music playback;
* track switching;
* sound effects;
* VBlank-safe timing;
* screen/state-driven music changes.

The actual compositions belong to the game.

For example:

```text
Foundation:
    audio_play_music()

Game:
    MUSIC_TOWN
    MUSIC_BATTLE
    MUSIC_GAME_OVER
```

---

# 15. Development Harness

The development harness is a first-class part of the foundation.

It is not merely a collection of temporary developer tools.

The goal is to make every game state reproducible.

An agent should be able to say:

```text
Start:
    scene = TOWN
    player = (8, 6)
    state = FLAG_MET_MAYOR=false

Actions:
    UP
    A
```

and reproduce a scenario without manually playing through the game.

This is especially important because the repository is designed to be developed by LLM-based coding agents.

---

# 16. LLM-Readable Debugging

The debug system should expose semantic game state rather than requiring visual interpretation.

A useful snapshot might look like:

```text
SCREEN: OVERWORLD
SCENE: TOWN

PLAYER:
  POSITION: 8,6
  FACING: NORTH

ACTORS:
  MAYOR
    POSITION: 8,5
    HOSTILE: NO
    INTERACTION: DIALOGUE

  GUARD
    POSITION: 12,4
    HOSTILE: NO
    INTERACTION: DIALOGUE

STATE:
  FLAG_MET_MAYOR: FALSE
  GOLD: 125
```

This allows an LLM to reason about the game directly.

Visual screenshots remain useful, but they should not be the only source of truth.

---

# 17. Scenario Testing

Every important gameplay system should be testable from an arbitrary initial state.

Examples:

```text
Start beside Mayor
    → interact
    → assert dialogue

Start beside Slime
    → move into Slime
    → assert battle

Start at Town Gate
    → trigger event
    → assert scene transition

Start after defeating boss
    → load state
    → assert alternate dialogue
```

This means the harness should be capable of injecting:

```text
scene
player position
party state
inventory
flags
variables
actor state
```

where appropriate.

---

# 18. What Does NOT Belong in the Foundation

The foundation should resist premature generalization.

The following should remain game-specific until there is evidence that they belong in the reusable layer:

* specific card mechanics;
* Baten Kaitos combat rules;
* specific damage formulas;
* specific XP formulas;
* character classes;
* particular quest structures;
* particular skill trees;
* specific item names;
* specific story events;
* specific dialogue;
* specific enemy designs;
* specific UI presentation;
* specific world lore.

For example:

```text
FOUNDATION
    Item
    Inventory
    Battle
    Character
    Progression

GAME
    Fire Bow
    Deck
    Card Combo
    Slime
    Mayor
    Phoenix Sword
```

---

# 19. The Generalization Rule

The most important rule for this repository is:

> **Do not generalize because something might be reusable. Generalize because the game has demonstrated that the abstraction is useful.**

For example:

### First occurrence

```text
Mayor dialogue
```

Implement the dialogue needed by the game.

### Second occurrence

```text
Guard dialogue
```

Extract the reusable dialogue mechanism.

Likewise:

### First enemy

```text
Slime
```

Implement the necessary actor/battle integration.

### Second enemy

```text
Bat
```

If no generic changes are required, the architecture is working.

This keeps the foundation practical instead of theoretical.

---

# 20. Foundation and Game Repository Boundary

Eventually this repository should become a reusable starting point for future RPG projects.

The desired relationship is:

```text
                 RPG FOUNDATION
                       │
             ┌─────────┼─────────┐
             │         │         │
             ▼         ▼         ▼
          RPG #1     RPG #2     RPG #3
```

Each game should contain:

```text
Game-specific content
Game-specific rules
Game-specific story
```

while inheriting or starting from:

```text
Foundation architecture
Build environment
Debug protocol
Testing harness
Reusable systems
```

The repository should not be split prematurely.

The foundation boundary should become explicit only after the architecture has been exercised by a sufficiently complete vertical slice.

---

# 21. When to Freeze the Foundation

A useful milestone is a stable **Foundation v0.1**.

Before creating a reusable template, the repository should have demonstrated stable implementations of:

* [ ] reproducible Nix environment;
* [ ] Game Boy build/release pipeline;
* [ ] screen/state architecture;
* [ ] scene/map system;
* [ ] scene IDs;
* [ ] World Actors;
* [ ] collision;
* [ ] dialogue;
* [ ] scripted events;
* [ ] game flags;
* [ ] game variables;
* [ ] party state;
* [ ] stats;
* [ ] items;
* [ ] inventory;
* [ ] progression;
* [ ] battle lifecycle;
* [ ] save/load;
* [ ] audio;
* [ ] telemetry;
* [ ] debug protocol;
* [ ] deterministic scenario initialization;
* [ ] automated scenario testing;
* [ ] LLM-readable state snapshots.

At that point, tag a release such as:

```text
v0.1.0
```

and consider turning the repository into a GitHub template.

---

# 22. Template Strategy

The initial strategy should favor simplicity.

A future game can be created from the foundation template:

```text
gb-rpg-foundation
        │
        │ Use as template
        ▼
    my-new-rpg
```

The new game then evolves independently.

A formal shared library or dependency mechanism should only be introduced if multiple games demonstrate a real need for synchronized foundation updates.

For small Game Boy projects, copying a stable foundation is likely preferable to introducing unnecessary dependency complexity.

---

# 23. Architecture Philosophy

This project is not trying to become a general-purpose engine.

It is a **purpose-built RPG foundation**.

The distinction is important.

The project should not attempt to solve:

```text
"How can every kind of game be made?"
```

It should solve:

```text
"How can I efficiently build small RPGs on the Game Boy
without repeatedly reinventing the same infrastructure?"
```

That narrower question produces a much more useful architecture.

---

# 24. The Ultimate Test

The foundation should eventually pass two tests.

## Test 1: New Content

Adding a new NPC should not require changes to the actor engine.

Adding a new enemy should not require changes to collision.

Adding a new dialogue should not require changes to the dialogue system.

Adding a new scene should not require changes to the world engine.

Adding a new event should not require a new bespoke execution system.

## Test 2: New Game

A second RPG should be able to reuse the foundation while replacing:

```text
characters
maps
items
dialogue
enemies
story
events
combat rules
progression rules
```

without rewriting:

```text
screen architecture
scene infrastructure
actor infrastructure
input
audio infrastructure
debugging
telemetry
scenario harness
build tooling
```

If that is possible, the foundation has succeeded.

---

# 25. Final Principle

The repository should remain grounded in the game that created it.

The architecture should evolve like this:

```text
GAME REQUIREMENT
      ↓
IMPLEMENTATION
      ↓
REPEATED PATTERN
      ↓
REUSABLE ABSTRACTION
      ↓
FOUNDATION
```

Not:

```text
THEORETICAL ENGINE
      ↓
COMPLEX FRAMEWORK
      ↓
TRY TO FIT THE GAME INTO IT
```

The game remains the source of truth.

The foundation exists to make the next game easier.
