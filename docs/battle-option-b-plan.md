# Option B Implementation Plan: Full BattleScreenDef Wiring

## Current State (c967b87)
- ✅ Option A complete: Editor preview matches ROM geometry, true enemy positions, reserved HUD note, legacy hint
- ✅ 181/181 harness tests pass
- ✅ Memory layout: Fixed bank at 32384/32768 bytes (99%), Bank 3 at 12681/16384 bytes (77%)
- ⚠️ Fixed bank overflow risk at 99% capacity

## Goal: Wire BattleScreenDef (bank 4) → Battle renderer (bank 3) → Game layer (bank 5)

---

## Architecture Overview

```
Game Layer (bank 5)          Engine (bank 3)              ROM Data (bank 4)
─────────────────────────    ───────────────────────      ───────────────────────
battle_start()               ui_update_battle_banked()   BattleScreenDef[]
  └─ game_battle_hud_load()      └─ g_battle_hud cache       g_battle_screens[]
      (bank 5)                    (bank 3)                    (bank 4)
```

---

## Phase 1: Move battle.c to Banked Section (Bank 3)

### 1.1 Create banked battle entry wrapper
**File:** `src/battle/battle_banked.c` (new, bank 3)
```c
#pragma bank 3
#pragma disable_warning 110
#include "battle.h"
#include "banked.h"

void battle_start_banked(void) {
    Battle *b = (Battle *)g_bk_ptr_a;
    const char *enemy_name = (const char *)g_bk_ptr_b;
    uint8_t player_hp = g_bk_byte_a;
    uint8_t player_max_hp = g_bk_byte_b;
    uint8_t enemy_hp = g_bk_byte_c;
    uint8_t enemy_max_hp = g_bk_byte_d;
    const DeckState *ds = (const DeckState *)g_bk_ptr_c;
    uint8_t battle_id = g_bk_byte_e;
    battle_start(b, enemy_name, player_hp, player_max_hp, 
                 enemy_hp, enemy_max_hp, ds, battle_id);
}
```

### 1.2 Refactor battle.c
- Move `battle_start()` implementation to `battle_banked.c`
- Keep thin wrapper in fixed bank:
```c
// src/battle/battle.c (fixed bank)
void battle_start(Battle *b, const char *enemy_name, ...) {
    g_bk_call_bank = 3;
    g_bk_call_target = (uint16_t)&battle_start_banked;
    // stage all args into g_bk_* globals
    banked_call_run();
}
```

### 1.3 Move dependent functions to bank 3
Move these to bank 3 to reduce fixed-bank pressure:
- `battle_update()` 
- `battle_card_select()`
- `battle_card_undo()`
- `battle_defend_resolve()`
- `battle_execute_combo()`

---

## Phase 2: Refactor Fixed Bank (Resolve Overflow)

### 2.1 Move more modules to banked sections
**Target: Reduce fixed bank from 32KB → <30KB**

Candidates for bank 3/4/5:
| Module | Current | Target | Lines | Risk |
|--------|---------|--------|-------|------|
| ui_battle_content.c | bank 3 | bank 4 | 732 | Low |
| battle.c | fixed | bank 3 | 725 | Medium |
| battle_init_content.c | bank 2 | bank 3 | 129 | Low |
| loot_drop_banked.c | bank 3 | bank 3 | ~100 | Low |
| game_render_reset_banked.c | bank 3 | bank 3 | ~100 | Low |

**Action:** Update Makefile bank assignments, rebuild, verify `make memmap` passes.

---

## Phase 3: Wire BattleScreenDef to Renderer

### 3.1 Extend BattleHudCache
```c
// battle_data.h
typedef struct BattleHudCache {
    uint8_t pos[3][2];
    uint8_t tick_o;
    uint8_t tick_b;
    uint8_t rows[28];
    // NEW: wire these from BattleScreenDef
    uint8_t timer_o;
    uint8_t timer_b;
    uint8_t energy_per_turn;
    uint8_t hand_size;
    uint8_t deck_row_start;
    uint8_t combo_row_start;
    uint8_t card_desc_row;
    uint8_t banner_row;
    uint8_t enemy_hp_row;
    uint8_t enemy_sprite_row;
    uint8_t enemy_cursor_row;
    uint8_t enemy_col_start;
    uint8_t enemy_col_step;
    uint8_t hero_label_row;
    uint8_t hero_label_col;
    uint8_t hero_hp_row;
    uint8_t hero_hp_col;
    uint8_t deck_row;
    uint8_t deck_col;
    uint8_t ap_row;
    uint8_t ap_col;
    uint8_t combo_row;
    uint8_t cards_row;
    uint8_t card_cursor_row;
    uint8_t card_desc_row;
    uint8_t timer_row;
    uint8_t timer_col;
    uint8_t timer_width;
} BattleHudCache;
```

### 3.2 Update `game_battle_hud_load()` (src/game/content.c)
Copy ALL fields from BattleScreenDef to g_battle_hud:
```c
void game_battle_hud_load(uint8_t battle_type) {
    // ... existing scan logic ...
    banked_copy(4, (void *)&g_battle_hud,
                (const void *)((uint16_t)(p[0] | ((uint16_t)p[1] << 8)) + 6), 42); // was 36, now 42 bytes
}
```

### 3.3 Update ui_battle_content.c to read from cache
Replace all hardcoded row/col constants with `g_battle_hud.rows[HUD_*]` and `g_battle_hud.pos[k][0]`.

Key mappings:
| Hardcoded | New Source |
|-----------|------------|
| Row 0 (banner) | `rows[HUD_BANNER_ROW]` |
| Row 1 (HP) | `rows[HUD_ENEMY_HP]` |
| Row 2 (names) | **NEW field** or keep row 2 |
| Row 3-4 (art) | `rows[HUD_ENEMY_SPRITE]` |
| Row 5 (cursor) | `rows[HUD_ENEMY_CURSOR]` |
| Col 0/7/14 | `pos[k][0]` from cache |
| Row 6 (hero) | `rows[HUD_HERO_LROW]` |
| Row 7 (deck/AP) | `rows[HUD_DECK_ROW]` |
| Row 9 (combo) | `rows[HUD_COMBO_ROW]` |
| Row 10+4 (cards) | `rows[HUD_CARDS_ROW]` |
| Row 14 (cursor) | `rows[HUD_CARD_CUR_ROW]` |
| Row 15 (desc) | `rows[HUD_CARD_DSC_ROW]` |
| Row 16/17 (timer) | `rows[HUD_TIMER_ROW]` |
| Timer width | `rows[HUD_TIMER_W]` |

---

## Phase 4: Battle Type → Screen Selection

### 4.1 Update game_battle_hud_load() logic
```c
void game_battle_hud_load(uint8_t battle_type) {
    const char *want = (battle_type == BATTLE_NONE) ? "boss" : "default";
    // ... scan g_battle_screens[] for match ...
    // fallback to "default" if not found
}
```

### 4.2 Map battle_id → screen id
| battle_id | JSON id | Description |
|-----------|---------|-------------|
| BATTLE_DEFAULT (0) | "default" | Standard 3-enemy |
| BATTLE_BOSS (1) | "boss" | Single boss |
| BATTLE_AMBUSH (2) | "ambush" | 3 enemies spread |
| BATTLE_DUO (3) | "duo" | 2 enemies |
| BATTLE_NONE (4) | "boss" | Final boss |

---

## Phase 5: Add Scenarios + Validation

### 5.1 New test scenarios
| Scenario | Tests |
|----------|-------|
| `battle_layout_default` | HUD rows match ROM geometry |
| `battle_layout_boss` | Boss screen uses boss HUD |
| `battle_layout_ambush` | 3 enemies at positions 0,7,14 |
| `battle_layout_duo` | 2 enemies at 0,14 |
| `battle_layout_legacy` | Legacy format still loads |
| `battle_timer_config` | Timer ticks from JSON |
| `battle_enemy_positions` | Enemy x from JSON, not hardcoded |

### 5.2 Validation gate
```bash
make test-harness JOBS=12
make test
make verify-oam
make memmap
make lint
```

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Fixed bank overflow | High | Block | Move more to banked first |
| Bank 3 overflow | Medium | Block | Move ui_battle_content.c to bank 4 |
| Banked call perf | Low | Minor | 64-byte WRAM trampoline tested |
| Bank 3 address window | Medium | Medium | All accesses via 0x4000\|offset |
| Regression in scenarios | Medium | High | Full harness before merge |

---

## Implementation Order

1. **Commit current state** (c967b87) ✅
2. **Branch:** `fix/battle-screen-wiring`
3. **Week 1:** Phase 1 (move battle.c → bank 3)
4. **Week 2:** Phase 2 (fixed bank refactor)
5. **Week 3:** Phase 3 (wire BattleScreenDef)
5. **Week 4:** Phase 4 (battle type mapping)
5. **Week 5:** Phase 5 (scenarios + full validation)
6. **PR & merge** after full green CI

---

## Build Commands for Development
```bash
# Quick build check
make debug

# Full validation
make clean && make debug && make test-harness JOBS=12 && make test && make verify-oam && make memmap && make lint
```

---

## Rollback Plan
```bash
git checkout c967b87  # back to Option A only
```

---

## Decision Points for You

1. **Bank assignment for ui_battle_content.c:** Bank 4 (recommended) or keep in 3?
2. **Enemy position source:** JSON x-coords (0,7,14) or keep engine-generated (3,8,13)?
3. **Legacy battle.json support:** Keep read-only hint or drop entirely?
4. **Timer config:** Wire `timer_overworld_ticks` / `timer_battle_ticks` or keep #defines?
5. **Boss screen:** Use boss.json HUD or keep hardcoded?

**Ready to start Phase 1 when you say go.**