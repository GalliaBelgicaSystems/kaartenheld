# verify-walkthrough — Real-Content Semantic Verifier

Status: **implemented** — `make verify-walkthrough`, required on push (CI).
759 checks green (incl. the content sweep over 14 levels); PNG output
byte-stable across runs; negative tests (offset drift, route failure)
verified to fail loudly.

## 1. Purpose

`tools/capture_walkthrough.py` upgrades from a screenshot-aid into the
**real-content regression layer** of the test architecture.  The two-tier
scenario suite (AGENTS.md §42.1) freezes fixture content for the debug ROM:
it can never exercise the working content in `levels/`.  The walkthrough
runs the **release ROM** (real content) headlessly under PyBoy and asserts
**semantic state** — not pixels — closing that blind spot.

It is **gated on push** (CI), like `verify-oam`.  The PNG artifacts it
saves stay non-gating (AGENTS.md §56.4 updated accordingly).

## 2. Verified foundation

| Fact | Evidence |
|---|---|
| Release ROM `.sym` exposes readable WRAM symbols | `_g_game`@0xC94C, `g_battle`@0xD0D5, `g_audio_current_track`@0xDA19, `g_rng_state`@0xDA37 (0x44D85-range entries are ROM initializers — ignored) |
| Struct layouts | `Game`: screen(0), prev_screen(1), **state@2**; `GameState`: scene(4B: id,x,y,facing), party(13B), cards(CardState), flags(8B), variables(32B), currency(8B), world(49B), progression(49B); `Battle`: player Combatant(14B: hp,max_hp,name[12]), enemies[3]@+14, phase/turn/result, battle_over |
| Boot anchors | scene pattern `[0,17,7,3]` (SCENE_FIELD + spawn + DIRECTION_RIGHT) + party `[1,10,10]` + gold=20 + hp=10/10; anchor failure = loud error naming offset-mirror drift |
| Enums | SceneId 0..N real (registry-assigned) / TEST fixed at 240+; MusicTrack NONE0 OVERWORLD1 BATTLE2 VICTORY3 TITLE4 TOWN5 DUNGEON6 BOSS7 MIMIC8 DESOLATE9 FOREST10; flags bit-packed (N → byte (N-1)/8 bit (N-1)%8): ARRIVED_TOWN=1, MET_MAYOR=2; CURRENCY_ID_GOLD=1 → amount[0]; HERO_START gold=20 hp=10 |
| BFS inputs | `derive_collision()` + `load_tilesets()` importable from `tools/level_compiler/` (walkable flags in `tools/level_editor/tilesets/*.json`); `levels/*.json` carries terrain grid, exits (portals with target_scene/target_x/y), objects with AI types (AI_PATROL_VERT = ±3 tiles from spawn, AI_PATROL_CIRCLE/CROSS = around spawn) |
| Save/load roundtrip | Cart = MBC5+RAM+BATTERY (header 0x1B, 8KB RAM); PyBoy 2.7.0 supports `PyBoy(rom, ram_file=f)` and `pb.stop(save=True, ram_file=f)` — cross-session SRAM transfer works |
| Shop | shopkeeper (shop_id=1) sells CARD_WOOD_RING; A buys; `g_game.shop_message` readable; gold/collection WRAM-readable |
| Battle | `g_battle.player.hp`, `enemies[i].hp`, result/phase bytes readable; victory loot hook credits slime `gold_reward` from `levels/field.json` |
| Title CONTINUE | menu index 1 → SCREEN_SAVE_LOAD (mode=LOAD) → A on slot 1 restores scene/pos |
| Title splash | boot → SCREEN_SPLASH (screen id 12, "A GAME BY / GALLIA BELGICA / SYSTEMS") auto-advances to SCREEN_TITLE after 150 frames or is skipped with A/START |
| CI | `.github/workflows/ci.yml` test job: clean+lint+test+memmap+test-harness(JOBS=4)+verify-oam → append `make verify-walkthrough` |

## 3. Architecture

```
tools/capture_walkthrough.py        ← thin entry (Makefile path unchanged)
tools/walkthrough/
├── __init__.py
├── state_reader.py    ← .sym resolver + offset mirror + boot anchors
├── route.py           ← BFS planner (imports derive_collision/load_tilesets)
├── session.py         ← Session class (boot, walk, press_until, cross_exit, bg_text)
└── walks.py           ← declarative walk definitions (steps + asserts)
```

### Phase 1 — StateReader

* Parse `build/kaartenheld.sym`; resolve WRAM symbols (addresses
  0xC000-0xDFFF only).
* Python offset mirror of `Game`/`GameState`/`Battle`/`DeckState`,
  documented per-field with header references.
* Exposes: `scene_id`, `player_x/y`, `flag(id)`, `gold`, `hero_hp`,
  `deck_count`, `collection_count(card_id)`,
  `battle.{player_hp, enemy_hp[i], result, phase}`, `music_track`,
  `shop_message`, `save_slot_message`.
* **Boot anchors**: at session start assert scene==SCENE_FIELD,
  position==spawn (from `levels/field.json`), gold==20, hp==10/10,
  ARRIVED_TOWN unset.  Post-action anchors re-verify (ARRIVED_TOWN set
  after town entry; gold==20−cost after purchase).  Anchor failure →
  hard error: "struct offset mirror drift — update the mirror from
  src/core/game.h / src/rpg/state.h".

### Phase 2 — Session class

* One class replaces the 5x duplicated closures
  (`pos/pos2/pos3/pos5`, `walk/walk2/...`, `press/press2/...`).
* Per-walk isolation: try/except + wall-clock cap (240 s default); a
  stuck walk aborts itself, later walks still run; final summary lists
  per-walk PASS/FAIL with expected/actual details (AGENTS.md §46 style).

### Phase 3 — BFS RoutePlanner

* Reuses `derive_collision` + `load_tilesets` (no duplicated logic).
* Multi-map graph: walkable cells per scene; portal edges from `exits`
  (enter exit tile → wipe-detection `cross_exit`).
* Patrol avoidance: inflate AI boxes (VERT ±3, CROSS/CIRCLE ±1) as
  blocked; hostile cells allowed when the target IS the encounter.
* `session.walk_to(scene, x, y)` executes the BFS path segment by
  segment with the existing commit/retry mechanics; arrival checks are
  semantic (`reader.scene_id == X`), position only secondary.

### Phase 4 — Expanded milestones (expected values read from `levels/*.json`)

1. **Shop purchase** (Walk A): buy WOOD_RING → assert
   gold==20−cost, `shop_message==BOUGHT`, collection gained the card.
2. **Mayor flag**: interact mayor → assert STORY_FLAG MET_MAYOR set.
3. **Battle victory** (Walk B): loop select+execute (bounded;
   variance-safe: enemy HP strictly decreased per hit) until
   `result==VICTORY` → assert hero gold == 20+slime.gold_reward,
   overworld restored.  Replaces the flee exit (same overworld-redraw
   + VRAM-restore coverage); `11-battle-run` → `11-battle-victory`.
4. **Save/load roundtrip** (new session pair S→L): save at wizard →
   `pb.stop(ram_file=)` → new `PyBoy(rom, ram_file=)` → title START →
   DOWN → CONTINUE → A → LOAD slot 1 → assert restored scene/pos/gold
   (post-purchase gold proves the loaded state is the saved one).
5. **Scene asserts** on Forest/Pass/Castle arrivals (`reader.scene_id`);
   Walk E now routes through the expanded castle chain (entry → hall →
   castle) to the solo Mimic.
6. **Quick-screen deck toggle**: deck a card → assert `deck_count`
   changed.
7. **Content sweep** (`walk_sweep`): every level in `levels/` is visited
   on every run — fresh session per level, BFS route from the spawn,
   scene-id + music asserts, `sweep-<name>.png`.  New levels are swept
   automatically; unreachable levels fail loudly.

### Phase 5 — Stability

* `12-wizard-save`: gate the capture on a stable state (transient TTL
  message cleared) — removes the known-unstable frame (§56.2).
* All 24 committed PNG names preserved; byte-stability re-verified by
  two consecutive runs.
* `check()` prints expected vs actual on every failure.

### Phase 6 — Wiring + docs

* `make verify-walkthrough` (depends on `make release`; exit code = gate).
* `make screenshots` unchanged (same script, artifact refresh).
* `ci.yml`: append `&& make verify-walkthrough`.
* AGENTS.md: §41 Step 6 adds verify-walkthrough; §56.4 — PNGs stay
  non-gating, semantic checks gate; §56.2/§56.3 updated for new
  milestones.

### Phase 7 — Validation

1. Full run green, exit 0; two consecutive runs byte-identical PNGs.
2. **Negative tests** (§52.16 discipline): corrupt one offset-mirror
   constant → boot anchor must fail loudly; break a route waypoint →
   clear walk failure; restore both.
3. Full gates: `make test-harness`, `make test`, `memmap`, `lint`,
   `verify-oam`, `make screenshots`.
4. Host-side only — no ROM changes; `make release` is the only build.

## 4. Risks / mitigations

* **PyBoy ram_file roundtrip**: spike verified end-to-end (save at wizard →
  `pb.stop(ram_file=)` → fresh boot with `ram_file=` → CONTINUE → LOAD →
  restored scene/pos/gold asserted).  In use by walk-l.
* **Offset mirror maintenance**: anchors catch drift at boot, never
  mid-assert; every mirror constant cites its header.  Negative test:
  corrupting a constant fails the anchor loudly.
* **Battle loop**: hand-aware selection (battle `hand[]`/`combo_count`
  mirrored from WRAM), A presses verified against `combo_count` with
  reset frames (§52.10 edge discipline), bounded rounds + defeat
  detection.

## 4.1 Battle mechanics learned (verified by the probes)

* Every hostile engages as a TRIO (struck actor + 2 clones) unless solo
  (`src/screens/overworld_screen.c`) — the field slime fight is 3x10 HP.
* `A` toggles the card at the cursor; the cursor does not reliably
  auto-advance, and some A presses are eaten by edge timing — selection
  must be verified against `combo_count` and retried (§52.10).
* The starter hand is mostly swords/shields; combos of 3 swords deal
  ~10 damage — trio dies in a handful of hand-aware rounds.
* `+429` in the Battle struct (mapped as `energy`) does not behave like
  a plain energy budget under scripted play; selection therefore keys
  off `combo_count`, not energy.

## 4.2 Triage: uniform world-mirror -1s

* `world_hostile_count` returns -1 when NEITHER const-char* stride
  candidate makes the World tail byte match the expected tileset kind.
  Boot anchors passing first proves `g_game`/`.sym` and the
  GameState/World-head layout are fine, so a uniform -1 is NOT a
  ROM/`.sym` mismatch. The suspects are: `levels/` edited after the
  ROM was built (the expected kind is computed from the working-tree
  level JSON, with a silent default for unknown tileset names), or
  World-tail struct drift (reader mirror vs `src/world/world.h`).
* The failure message shows the observed tail bytes next to the
  expected kind, which distinguishes these causes on the spot.
* The script fails fast when `build/kaartenheld.sym` is older than
  `build/kaartenheld.noi` (both pure link-time products — the .gb is
  routinely newer than both because rgbfix stamps it after make_sym.py
  and again under `make test`, so the .gb mtime cannot be used), and
  prints whether the ROM pair plus `levels/` match HEAD before running
  (dirty state = testing the working tree, which is expected under
  `make verify-walkthrough`; it only attributes).
* Recipe: `git diff --quiet HEAD -- build/kaartenheld.gb
  build/kaartenheld.sym levels/` (clean?), then `python3
  tools/capture_walkthrough.py` directly against the committed ROM —
  no `make` in between, so no rebuild can race the read.

## 5. Decisions (user-confirmed)

* Gating: required on push (like verify-oam).
* Scope: full plan (all phases).
* Routing: full BFS pathfinding.
* Walk B exit becomes victory (flee path dropped; same exit-path
  coverage, plus loot/gold assertion value).

## 6. Screenshot lifecycle (milestones + review shots)

* The committed top-level set is explicit: `CLASSIC_MILESTONES` in
  `tools/capture_walkthrough.py` plus `sweep-<name>` per planner scene.
  A plain `make screenshots` run only overwrites those.
* `--clean` (used by `make verify-walkthrough`, i.e. CI) additionally
  removes any top-level `screenshots/*.png` outside the set, so
  renamed/retired milestones cannot linger and confuse reviewers.
* `screenshots/review/` holds committed ad-hoc captures a walk cannot
  stage deterministically.  `review/manifest.json` is the source of
  truth (`file`, `description`, `taken_from` commit); `--clean` deletes
  any review PNG missing from it.  If the manifest is missing or
  unreadable, review/ is left untouched with a warning — never nuke
  blindly.  `tools/capture_bow_shot.py` (bow hand card, staged through
  PyBoy memory like the harness's `set_hand_card`) is the first entry:
  `bow-card-uses2.png` / `bow-card-depleted.png`.
* Regenerating a shot after a visual change overwrites it in place
  (same filename, fresh `taken_from`); a renamed shot plus a manifest
  update lets `--clean` retire the orphan.
