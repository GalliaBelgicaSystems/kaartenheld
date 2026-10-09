import React, { useEffect, useState } from 'react';
import {
  CardSkin, CardTypeSkin, CARD_ICON_NAMES, CARD_COLOR_NAMES, CARD_COLOR_HEX,
  CARD_ICON_URL, CARD_FRAME_URLS, fetchCardSkin, saveCardSkin,
} from './io/cardSkin';
import {
  BattleHud, BattleScreen, BATTLE_SCREEN_IDS,
  HUD_ICON_NAMES, BAR_TILE_NAMES,
  fetchBattleHud, saveBattleHud, fetchBattleScreen, saveBattleScreen,
} from './io/battleHud';
import {
  EnemyTypeListItem, CombatArtSet,
  fetchEnemyTypeList, fetchCombatArtSet, COMBAT_TILE_URL,
} from './io/combatArt';

/** Battle view: configures the entire battle-time view from the editor.
 *
 *  Tabs:
 *   - HUD    (screens/battle_hud.json): hero-HP / AP / deck icon + CGB
 *     palette, and the turn-timer bar (segment tiles, color, row, width).
 *   - LAYOUT (screens/battle/<id>.json): per-battle-screen hud_layout rows
 *     and columns for every HUD element + enemy slot positions.
 *   - CARDS  (screens/cards_skin.json): per-type/element hand-card skin.
 *
 *  Everything renders into a live 20x18 preview replicating the ROM battle
 *  renderer.  Gameplay data (damage, cost, effects) is NOT edited here.
 *  After saving: run `make screens` (+ `make gfx` only when curated tile
 *  PNGs changed). */

type Tab = 'hud' | 'layout' | 'cards';

const TYPE_KEYS: Array<keyof CardSkin['types']> = ['sword', 'shield', 'bow', 'heal', 'dagger'];
const ELEM_KEYS: Array<keyof CardSkin['elements']> = ['fire', 'ice', 'poison'];
const TYPE_LABELS: Record<string, string> = {
  sword: 'Sword', shield: 'Shield', bow: 'Bow', heal: 'Ring / Heal', dagger: 'Dagger',
};
const ELEM_LABELS: Record<string, string> = {
  fire: 'Fire (BURN)', ice: 'Ice (FREEZE)', poison: 'Poison',
};

const PREVIEW_HAND: Array<{ type: keyof CardSkin['types']; value: number; elem: keyof CardSkin['elements'] | null }> = [
  { type: 'sword', value: 3, elem: 'fire' },
  { type: 'shield', value: 2, elem: null },
  { type: 'heal', value: 2, elem: null },
  { type: 'dagger', value: 1, elem: 'poison' },
  { type: 'bow', value: 4, elem: null },
];

/* ── Shared small controls ─────────────────────────────────────────── */

const iconChip = (name: string, size = 18) => {
  const url = CARD_ICON_URL[name];
  return url
    ? <img src={url} alt={name} width={size} height={size} style={{ imageRendering: 'pixelated', verticalAlign: 'middle' }} />
    : <span style={{ fontSize: 9, fontWeight: 'bold', color: '#5a4a2a', border: '1px solid #b09a6a', padding: '0 3px', background: '#fff' }}>{name.slice(0, 2).toUpperCase()}</span>;
};

const tintStyle = (c: string) => ({ background: CARD_COLOR_HEX[c] || '#fff', opacity: 0.35 });

const SkinRow: React.FC<{ label: string; value: CardTypeSkin; icons: string[]; onChange: (v: CardTypeSkin) => void }> = ({ label, value, icons, onChange }) => (
  <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 4 }}>
    <span style={{ width: 130, fontSize: 13 }}>{label}</span>
    <select value={value.icon} onChange={(e) => onChange({ ...value, icon: e.target.value })} style={{ fontSize: 12 }}>
      {icons.map((n) => <option key={n} value={n}>{n}</option>)}
    </select>
    <select value={value.color} onChange={(e) => onChange({ ...value, color: e.target.value })} style={{ fontSize: 12 }}>
      {CARD_COLOR_NAMES.map((n) => <option key={n} value={n}>{n}</option>)}
    </select>
    <span style={{ width: 14, height: 14, display: 'inline-block', border: '1px solid #999', background: CARD_COLOR_HEX[value.color] || '#fff' }} />
  </div>
);

const NumField: React.FC<{ label: string; value: number; min: number; max: number; onChange: (v: number) => void; width?: number }> =
  ({ label, value, min, max, onChange, width = 46 }) => (
    <label style={{ fontSize: 12, display: 'flex', alignItems: 'center', gap: 4 }}>
      {label}
      <input type="number" min={min} max={max} value={value} style={{ width }}
        onChange={(e) => {
          const v = parseInt(e.target.value);
          if (!isNaN(v)) onChange(Math.max(min, Math.min(max, v)));
        }} />
    </label>
  );

/* ── Card preview (ROM renderer replica) ───────────────────────────── */

const PreviewCard: React.FC<{ skin: CardSkin; card: typeof PREVIEW_HAND[0]; scale: number }> = ({ skin, card, scale }) => {
  const h = Math.max(3, Math.min(5, skin.box.h || 4));
  const typeSkin = skin.types[card.type];
  // The ROM shows element status purely as the card's box tint
  // (battle_card_box_color: element color overrides the type's
  // material color) -- no floating rider icon.
  const tint = card.elem ? skin.elements[card.elem].color : typeSkin.color;
  const frame = (idx: number) => (
    <div style={{ position: 'relative', width: scale, height: scale }}>
      <img src={CARD_FRAME_URLS[idx]} alt="" width={scale} height={scale} style={{ imageRendering: 'pixelated', display: 'block' }} />
      <div style={{ position: 'absolute', inset: 0, ...tintStyle(tint) }} />
    </div>
  );
  const rows: React.ReactNode[] = [];
  rows.push(<div key="t" style={{ display: 'flex' }}>{frame(0)}{frame(1)}{frame(2)}</div>);
  for (let r = 1; r < h - 1; r++) {
    const content = r === 1
      ? iconChip(typeSkin.icon, scale - 6)
      : (r === h - 2 ? <span style={{ fontSize: scale - 9, fontWeight: 'bold', color: '#5a4a2a' }}>{card.value}</span> : null);
    rows.push(
      <div key={r} style={{ display: 'flex' }}>
        {frame(3)}
        <div style={{ position: 'relative', width: scale, height: scale }}>
          {frame(4)}
          {content && <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>{content}</div>}
        </div>
        {frame(5)}
      </div>,
    );
  }
  rows.push(<div key="b" style={{ display: 'flex' }}>{frame(6)}{frame(7)}{frame(8)}</div>);
  return (
    <div style={{ position: 'relative', width: scale * 3 }}>
      {rows}
    </div>
  );
};

/* ── Full-screen battle preview (20x18 grid) ───────────────────────── */

const CELL = 11;
const GRID_W = 20;
const GRID_H = 18;

/* The 6-column name slot the ROM centers enemy art on
 * (battle_enemy_art_x in ui_battle_content.c: art_x = x + (6-w)/2). */
const ENEMY_NAME_SLOT_W = 6;

const BattlePreview: React.FC<{
  skin: CardSkin;
  hud: BattleHud;
  screen: BattleScreen;
  artSet: CombatArtSet | null;
}> = ({ skin, hud, screen, artSet }) => {
  const L = screen.hud_layout;
  /* grid holds per-cell render nodes: string (char) or JSX (icons/tiles) */
  const grid: Array<Array<React.ReactNode>> = Array.from({ length: GRID_H },
    () => Array.from({ length: GRID_W }, () => null as React.ReactNode));
  const put = (x: number, y: number, s: string) => {
    if (y < 0 || y >= GRID_H) return;
    for (let i = 0; i < s.length; i++) {
      if (x + i >= 0 && x + i < GRID_W) grid[y][x + i] = s[i];
    }
  };
  const putNode = (x: number, y: number, node: React.ReactNode) => {
    if (y >= 0 && y < GRID_H && x >= 0 && x < GRID_W) grid[y][x] = node;
  };

  put(4, L.turn_banner_row, 'TARGET SLIME');
  for (let e = 0; e < screen.max_enemies; e++) {
    const p = screen.enemy_positions[e] || { x: 0, y: 0 };
    put(p.x, L.enemy_hp_row, '10/10');
    /* Real combat art (frame0), centered on the name slot exactly like
     * the ROM stamper; falls back to a placeholder blob for text-fallback
     * types (art 0xFF). */
    if (artSet && artSet.frame0 && artSet.frame0.length > 0) {
      const ax = p.x + Math.floor((ENEMY_NAME_SLOT_W - artSet.width) / 2);
      for (let r = 0; r < artSet.height; r++) {
        for (let c = 0; c < artSet.width; c++) {
          const cell = artSet.frame0[r * artSet.width + c];
          if (cell) {
            putNode(ax + c, L.enemy_sprite_row + r,
              <img src={COMBAT_TILE_URL(cell)} alt="" width={CELL} height={CELL}
                style={{ imageRendering: 'pixelated', width: CELL - 1, height: CELL - 1, marginTop: 1, marginLeft: 1 }} />);
          }
        }
      }
    } else {
      putNode(p.x, L.enemy_sprite_row,
        <div style={{ width: 3 * CELL - 2, height: 2 * CELL - 2, background: '#67c23a', border: '1px solid #3f7d22', borderRadius: 3 }} />);
    }
    if (e === 1) put(p.x, L.enemy_cursor_row, '  ^');
  }
  put(L.hero_label_col, L.hero_label_row, 'HERO');
  putNode(L.hero_hp_col - 2, L.hero_hp_row, iconChip(hud.hp.icon, CELL - 3));
  put(L.hero_hp_col - 1, L.hero_hp_row, `HP:10/10`);
  putNode(L.deck_col - 1, L.deck_row, iconChip(hud.deck.icon, CELL - 3));
  put(L.deck_col, L.deck_row, `DECK: 7`);
  putNode(L.ap_col - 2, L.ap_row, iconChip(hud.ap.icon, CELL - 3));
  put(L.ap_col - 1, L.ap_row, `AP: 6/6`);
  put(0, L.combo_row, 'COMBO: PAIR');
  put(0, L.card_desc_row, 'Sword: physical');
  put(0, L.card_cursor_row, '1   ^');
  for (let i = 0; i < (L.timer_width || 20); i++) {
    putNode(i, hud.bar.row,
      <div style={{ width: CELL - 1, height: CELL - 2, marginTop: 1, background: i < 14 ? CARD_COLOR_HEX[hud.bar.color] : '#fff', border: '1px solid #8a7a5a' }} />);
  }

  return (
    <div style={{ position: 'relative', width: GRID_W * CELL, height: GRID_H * CELL, background: '#fdfbf5', border: '2px solid #333', fontFamily: 'monospace', fontSize: 10, lineHeight: `${CELL}px`, overflow: 'hidden' }}>
      {grid.map((row, y) => (
        <div key={y} style={{ display: 'flex', height: CELL }}>
          {row.map((c, x) => (
            <div key={x} style={{ width: CELL, height: CELL, display: 'flex', alignItems: 'center', justifyContent: 'center', overflow: 'visible' }}>
              {c === null ? '\u00a0' : c}
            </div>
          ))}
        </div>
      ))}
      {/* boxed hand cards overlay their rows (rows cards_row-3 .. cards_row) */}
      <div style={{ position: 'absolute', top: (Math.max(3, Math.min(5, skin.box.h || 4)) === 4 ? L.cards_row - 3 : L.cards_row - 2) * CELL - 0, left: 0, display: 'flex' }}>
        {PREVIEW_HAND.map((card, i) => (
          <div key={i} style={{ marginRight: CELL }}>
            <PreviewCard skin={skin} card={card} scale={CELL} />
          </div>
        ))}
      </div>
    </div>
  );
};

/* ── Layout tab ────────────────────────────────────────────────────── */

const LAYOUT_FIELDS: Array<{ key: keyof BattleScreen['hud_layout']; label: string; min: number; max: number }> = [
  { key: 'turn_banner_row', label: 'Banner row', min: 0, max: 17 },
  { key: 'enemy_hp_row', label: 'Enemy HP row', min: 0, max: 17 },
  { key: 'enemy_sprite_row', label: 'Enemy art row', min: 0, max: 17 },
  { key: 'enemy_cursor_row', label: 'Enemy caret row', min: 0, max: 17 },
  { key: 'hero_label_row', label: 'HERO row', min: 0, max: 17 },
  { key: 'hero_label_col', label: 'HERO col', min: 0, max: 16 },
  { key: 'hero_hp_row', label: 'HP row', min: 0, max: 17 },
  { key: 'hero_hp_col', label: 'HP col (icon 2 left of HP:)', min: 2, max: 15 },
  { key: 'deck_row', label: 'DECK row', min: 0, max: 17 },
  { key: 'deck_col', label: 'DECK col (icon 1 left)', min: 1, max: 14 },
  { key: 'ap_row', label: 'AP row', min: 0, max: 17 },
  { key: 'ap_col', label: 'AP col (icon 2 left of AP:)', min: 2, max: 15 },
  { key: 'combo_row', label: 'COMBO row', min: 0, max: 17 },
  { key: 'cards_row', label: 'Cards row (box bottom)', min: 3, max: 17 },
  { key: 'card_cursor_row', label: 'Markers row', min: 0, max: 17 },
  { key: 'card_desc_row', label: 'Description row', min: 0, max: 17 },
  { key: 'timer_width', label: 'Banner/desc width', min: 1, max: 20 },
];

export const BattleManager: React.FC = () => {
  const [tab, setTab] = useState<Tab>('hud');
  const [skin, setSkin] = useState<CardSkin | null>(null);
  const [hud, setHud] = useState<BattleHud | null>(null);
  const [screenId, setScreenId] = useState<string>('default');
  const [screen, setScreen] = useState<BattleScreen | null>(null);
  const [dirty, setDirty] = useState({ hud: false, layout: false, cards: false });
  const [status, setStatus] = useState('');
  const [enemyTypes, setEnemyTypes] = useState<EnemyTypeListItem[]>([]);
  const [artSets, setArtSets] = useState<Record<string, CombatArtSet>>({});

  useEffect(() => {
    fetchCardSkin().then((s) => setSkin(s)).catch((e) => setStatus(`cards load failed: ${e.message}`));
    fetchBattleHud().then((h) => setHud(h)).catch((e) => setStatus(`hud load failed: ${e.message}`));
    fetchBattleScreen('default').then((s) => setScreen(s)).catch((e) => setStatus(`layout load failed: ${e.message}`));
    fetchEnemyTypeList().then(setEnemyTypes).catch(() => undefined);
  }, []);

  useEffect(() => {
    fetchBattleScreen(screenId).then((s) => setScreen(s)).catch((e) => setStatus(`layout load failed: ${e.message}`));
  }, [screenId]);

  /* Preview enemy: same category rule as the ROM's screen filter — prefer
   * the first sorted type of the highest-priority allowed category, so
   * 'default' previews the slime trio and 'boss' previews the 3x3 lord. */
  const previewType = (() => {
    const sorted = [...enemyTypes].sort((a, b) => a.id.localeCompare(b.id));
    const allowed = screen?.allowed_categories || [];
    for (const cat of ['boss', 'elite', 'minion']) {
      if (!allowed.includes(cat)) continue;
      const hit = sorted.find((t) => t.category === cat);
      if (hit) return hit;
    }
    return sorted[0] || null;
  })();

  useEffect(() => {
    const art = previewType?.art;
    if (!art || artSets[art]) return;
    fetchCombatArtSet(art)
      .then((s) => setArtSets((prev) => ({ ...prev, [art]: s })))
      .catch(() => undefined);
  }, [previewType, artSets]);

  const mutateSkin = (fn: (s: CardSkin) => void) => {
    setSkin((prev) => {
      if (!prev) return prev;
      const next = JSON.parse(JSON.stringify(prev)) as CardSkin;
      fn(next);
      return next;
    });
    setDirty((d) => ({ ...d, cards: true }));
  };

  const mutateHud = (fn: (h: BattleHud) => void) => {
    setHud((prev) => {
      if (!prev) return prev;
      const next = JSON.parse(JSON.stringify(prev)) as BattleHud;
      fn(next);
      return next;
    });
    setDirty((d) => ({ ...d, hud: true }));
  };

  const mutateLayout = (fn: (s: BattleScreen) => void) => {
    setScreen((prev) => {
      if (!prev) return prev;
      const next = JSON.parse(JSON.stringify(prev)) as BattleScreen;
      fn(next);
      return next;
    });
    setDirty((d) => ({ ...d, layout: true }));
  };

  const save = async (which: Tab) => {
    try {
      if (which === 'cards' && skin) {
        await saveCardSkin(skin);
        setStatus('saved screens/cards_skin.json — run make screens to recompile');
      } else if (which === 'hud' && hud) {
        await saveBattleHud(hud);
        setStatus('saved screens/battle_hud.json — run make screens to recompile');
      } else if (which === 'layout' && screen) {
        await saveBattleScreen(screenId, screen);
        setStatus(`saved screens/battle/${screenId}.json — run make screens to recompile`);
      }
      setDirty((d) => ({ ...d, [which]: false }));
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  if (!skin || !hud || !screen) return <div style={{ padding: 16 }}>{status || 'loading…'}</div>;

  const tabBtn = (id: Tab, label: string) => (
    <button key={id} onClick={() => setTab(id)}
      style={{ fontWeight: tab === id ? 'bold' : 'normal', border: '1px solid #999', borderBottom: tab === id ? '2px solid #333' : '1px solid #999', background: tab === id ? '#fff' : '#eee', padding: '3px 14px', cursor: 'pointer' }}>
      {label}{dirty[id] ? ' *' : ''}
    </button>
  );

  return (
    <div style={{ display: 'flex', gap: 24, padding: 16, height: '100%', overflow: 'auto', background: '#f4efe4', color: '#222' }}>
      <div style={{ minWidth: 420 }}>
        <h3 style={{ margin: '0 0 8px' }}>Battle view</h3>
        <div style={{ marginBottom: 8 }}>
          {tabBtn('hud', 'HUD')}{tabBtn('layout', 'Layout')}{tabBtn('cards', 'Cards')}
        </div>

        {tab === 'hud' && (
          <div>
            <div style={{ fontSize: 12, color: '#555', marginBottom: 8 }}>
              Icon tiles are the fixed VRAM icon set (ui.h 104-116); colors are
              the CGB BG palettes. The bar draws one segment per column, filled
              while the turn timer has time left.
            </div>
            <h4 style={{ margin: '4px 0 4px' }}>HUD icons</h4>
            <SkinRow label="Hero HP icon" value={hud.hp} icons={HUD_ICON_NAMES} onChange={(v) => mutateHud((h) => { h.hp = v; })} />
            <SkinRow label="AP (energy) icon" value={hud.ap} icons={HUD_ICON_NAMES} onChange={(v) => mutateHud((h) => { h.ap = v; })} />
            <SkinRow label="Deck icon" value={hud.deck} icons={HUD_ICON_NAMES} onChange={(v) => mutateHud((h) => { h.deck = v; })} />
            <h4 style={{ margin: '10px 0 4px' }}>Turn-timer bar</h4>
            <SkinRow label="Bar color" value={{ icon: hud.bar.filled, color: hud.bar.color }} icons={BAR_TILE_NAMES}
              onChange={(v) => mutateHud((h) => { h.bar.filled = v.icon; h.bar.color = v.color; })} />
            <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap', marginTop: 4 }}>
              <NumField label="Filled tile" value={BAR_TILE_NAMES.indexOf(hud.bar.filled) >= 0 ? BAR_TILE_NAMES.indexOf(hud.bar.filled) : 0} min={0} max={BAR_TILE_NAMES.length - 1} width={200}
                onChange={(i) => mutateHud((h) => { h.bar.filled = BAR_TILE_NAMES[i]; })} />
              <NumField label="Empty tile" value={BAR_TILE_NAMES.indexOf(hud.bar.empty) >= 0 ? BAR_TILE_NAMES.indexOf(hud.bar.empty) : 1} min={0} max={BAR_TILE_NAMES.length - 1} width={200}
                onChange={(i) => mutateHud((h) => { h.bar.empty = BAR_TILE_NAMES[i]; })} />
            </div>
            <div style={{ display: 'flex', gap: 10, marginTop: 4 }}>
              <NumField label="Bar row" value={hud.bar.row} min={0} max={17} onChange={(v) => mutateHud((h) => { h.bar.row = v; })} />
              <NumField label="Bar width" value={hud.bar.width} min={1} max={20} onChange={(v) => mutateHud((h) => { h.bar.width = v; })} />
            </div>
            <div style={{ marginTop: 10 }}>
              <button onClick={() => save('hud')} disabled={!dirty.hud}>Save HUD skin{dirty.hud ? ' *' : ''}</button>
            </div>
          </div>
        )}

        {tab === 'layout' && (
          <div>
            <div style={{ fontSize: 12, color: '#555', marginBottom: 8 }}>
              Per battle screen (screens/battle/{screenId}.json). The timer
              bar's row/width live in the HUD tab; enemy art columns follow the
              slot positions below.
            </div>
            <div style={{ marginBottom: 8 }}>
              Screen:{' '}
              <select value={screenId} onChange={(e) => setScreenId(e.target.value)} style={{ fontSize: 13 }}>
                {BATTLE_SCREEN_IDS.map((id) => <option key={id} value={id}>{id}</option>)}
              </select>
              <span style={{ fontSize: 12, color: '#555', marginLeft: 8 }}>{screen.label}</span>
            </div>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, max-content)', gap: '2px 18px' }}>
              {LAYOUT_FIELDS.map((f) => (
                <NumField key={f.key} label={f.label} value={screen.hud_layout[f.key] as number} min={f.min} max={f.max}
                  onChange={(v) => mutateLayout((s) => { (s.hud_layout[f.key] as number) = v; })} />
              ))}
            </div>
            <h4 style={{ margin: '10px 0 4px' }}>Enemy slots</h4>
            {[0, 1, 2].map((i) => (
              <div key={i} style={{ display: 'flex', gap: 10, marginBottom: 2, opacity: i < screen.max_enemies ? 1 : 0.4 }}>
                <span style={{ width: 70, fontSize: 12 }}>Slot {i + 1}</span>
                <NumField label="x" value={(screen.enemy_positions[i] || { x: 0 }).x} min={0} max={19} width={40}
                  onChange={(v) => mutateLayout((s) => { if (s.enemy_positions[i]) s.enemy_positions[i].x = v; })} />
                <NumField label="y" value={(screen.enemy_positions[i] || { y: 0 }).y} min={0} max={17} width={40}
                  onChange={(v) => mutateLayout((s) => { if (s.enemy_positions[i]) s.enemy_positions[i].y = v; })} />
              </div>
            ))}
            <div style={{ marginTop: 10 }}>
              <button onClick={() => save('layout')} disabled={!dirty.layout}>Save layout{dirty.layout ? ' *' : ''}</button>
            </div>
          </div>
        )}

        {tab === 'cards' && (
          <div>
            <div style={{ fontSize: 13, marginBottom: 6 }}>
              Box height:{' '}
              <button onClick={() => mutateSkin((s) => { s.box.h = Math.max(3, (s.box.h || 4) - 1); })}>-</button>{' '}
              {skin.box.h}{' '}
              <button onClick={() => mutateSkin((s) => { s.box.h = Math.min(5, (s.box.h || 4) + 1); })}>+</button>{' '}
              (3-5; width fixed at 3, hand stride 4)
            </div>
            <h4 style={{ margin: '8px 0 4px' }}>Card types (weapon icon + box color)</h4>
            {TYPE_KEYS.map((k) => (
              <SkinRow key={k} label={TYPE_LABELS[k]} value={skin.types[k]} icons={CARD_ICON_NAMES}
                onChange={(v) => mutateSkin((s) => { s.types[k] = v; })} />
            ))}
            <h4 style={{ margin: '8px 0 4px' }}>Element status (tint color + reveal icon)</h4>
            <div style={{ fontSize: 12, color: '#555', margin: '0 0 6px' }}>
              A card carrying a status is TINTED with the element color (no
              floating rider icon).  The icon tile appears only in the
              loot-reveal icon pair.
            </div>
            {ELEM_KEYS.map((k) => (
              <SkinRow key={k} label={ELEM_LABELS[k]} value={skin.elements[k]} icons={CARD_ICON_NAMES}
                onChange={(v) => mutateSkin((s) => { s.elements[k] = v; })} />
            ))}
            <div style={{ marginTop: 10 }}>
              <button onClick={() => save('cards')} disabled={!dirty.cards}>Save card skin{dirty.cards ? ' *' : ''}</button>
            </div>
          </div>
        )}

        {status && <div style={{ marginTop: 8, fontSize: 13 }}>{status}</div>}
      </div>

      <div>
        <h3 style={{ margin: '0 0 4px' }}>Live preview (20×18)</h3>
        <div style={{ fontSize: 12, color: '#555', marginBottom: 8 }}>
          Replicates the ROM battle renderer from the edited HUD skin, layout
          and card skin. Ring/dagger/amulet icons preview as text chips.
        </div>
        <BattlePreview skin={skin} hud={hud} screen={screen}
          artSet={previewType?.art ? (artSets[previewType.art] || null) : null} />
      </div>
    </div>
  );
};
