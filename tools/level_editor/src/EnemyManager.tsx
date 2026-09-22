import React, { useEffect, useState } from 'react';
import {
  EnemyTypeListItem, CombatArtListItem,
  fetchEnemyTypeList, fetchEnemyType, saveEnemyType,
  fetchCombatArtList,
} from './io/combatArt';
import { BUILTIN_TILESETS } from './model/Tileset';
import { BATTLE_IDS, AI_IDS } from './model/Objects';
import { RampSelect } from './RampSelect';

/** Enemies view (art-only): one record per enemy type.  The level view
 *  owns placement; this view owns looks.  Per type it edits the shared
 *  overworld sprite (transparent, every world) and shows the battle-art
 *  assignment (edited in the Combat Art Studio).  Selections apply
 *  everywhere the type appears: the compilers resolve placed enemies to
 *  their type row by the ENTITY_ID_X naming convention. */

interface OwDraft {
  width: number;
  height: number;
  cells: string[];
  /** OBJ ramp name (files normalize here on edit); a legacy bare OBJ
   *  slot int still loads and displays via the slotmap. */
  palette: string | number;
}

export const EnemyManager: React.FC<{
  onOpenComposer: () => void;
  initialId?: string;
  /** Jump into the Palettes view with a ramp editor pre-opened. */
  onEditRamp?: (rampName: string) => void;
  /** A save changed an overworld palette: level views should refresh. */
  onChanged?: () => void;
}> = ({ onOpenComposer, initialId, onEditRamp, onChanged }) => {
  const [enemies, setEnemies] = useState<EnemyTypeListItem[]>([]);
  const [sets, setSets] = useState<CombatArtListItem[]>([]);
  const [activeId, setActiveId] = useState<string>('');
  const [full, setFull] = useState<any>(null);
  const [ow, setOw] = useState<OwDraft | null>(null);
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState('');
  const [enemyRows, setEnemyRows] = useState<Record<string, { art: string; frames: number; category: string; dirty: boolean }>>({});

  const owTiles = (BUILTIN_TILESETS.enemies?.tiles || []).filter((t) => t.category === 'enemy');
  const tileUrl = (id: string) => {
    const t = owTiles.find((x) => x.id === id);
    return t ? t.image_url : `/tiles/enemies/${id}.png`;
  };

  useEffect(() => {
    fetchEnemyTypeList().then((items) => {
      setEnemies(items);
      if (items.length > 0) setActiveId((prev) => prev || initialId || items[0].id);
    }).catch((e) => setStatus(`load failed: ${e.message}`));
    fetchCombatArtList().then(setSets).catch(() => undefined);
  }, []);

  useEffect(() => {
    if (!activeId) { setFull(null); return; }
    fetchEnemyType(activeId).then((data) => {
      setFull(data);
      const o = data.overworld || null;
      setOw(o ? {
        width: o.width || 1,
        height: o.height || 1,
        cells: (o.cells || []).slice(),
        palette: o.palette ?? 0,
      } : null);
      setDirty(false);
      setStatus('');
    }).catch((e) => setStatus(`load failed: ${e.message}`));
  }, [activeId]);

  const active = enemies.find((e) => e.id === activeId);
  const battleSet = sets.find((s) => s.id === ((full && full.sprite && full.sprite.art) || ''));

  const save = async () => {
    if (!full) return;
    try {
      const data = JSON.parse(JSON.stringify(full));
      if (ow && ow.cells.length > 0) {
        data.overworld = { width: ow.width, height: ow.height, cells: ow.cells.slice(), palette: ow.palette };
      } else {
        delete data.overworld;
      }
      const prevPal = full && full.overworld ? full.overworld.palette : undefined;
      await saveEnemyType(activeId, data);
      setFull(data);
      setDirty(false);
      setStatus(`saved screens/enemy_types/${activeId}.json — run make screens + make gfx to recompile`);
      if (onChanged && ow && ow.palette !== prevPal) onChanged();
      fetchEnemyTypeList().then(setEnemies).catch(() => undefined);
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  const saveEnemyRow = async (id: string) => {
    const row = enemyRows[id];
    if (!row) return;
    try {
      const full = await fetchEnemyType(id);
      if (row.art) full.sprite = { art: row.art, frames: row.frames };
      else full.sprite = null;
      full.category = row.category;
      await saveEnemyType(id, full);
      setEnemyRows((prev) => ({ ...prev, [id]: { ...row, dirty: false } }));
      setStatus(`saved screens/enemy_types/${id}.json — run make screens to recompile`);
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  const setCell = (idx: number, id: string) => {
    setOw((prev) => {
      const cells = ((prev && prev.cells) || []).slice();
      cells[idx] = id;
      return { ...(prev || { width: 1, height: 1, palette: 0 }), cells };
    });
    setDirty(true);
  };

  /* Change sprite grid dimensions.  Cells are a flat frame-major list of
   * width*height*frames tiles; resizing keeps existing cells and pads new
   * cells with the first overworld tile (or blanks when shrinking). */
  const setDims = (w: number, h: number) => {
    w = Math.max(1, Math.min(2, w));
    h = Math.max(1, Math.min(2, h));
    setOw((prev) => {
      const p = prev || { width: 1, height: 1, cells: [] as string[], palette: 0 };
      const ow = p.width * p.height;
      const nw = w * h;
      const out: string[] = [];
      for (let i = 0; i < nw; i++) {
        out.push(i < p.cells.length ? p.cells[i] : (owTiles[0] && owTiles[0].id) || '');
      }
      return { width: w, height: h, cells: out, palette: p.palette };
    });
    setDirty(true);
  };

  /* Number of animation frames = cells.length / (w*h).  Frames are
   * stacked: each frame is its own w*h block. */
  const frameCount = ow ? Math.max(1, Math.ceil((ow.cells.length || 0) / (ow.width * ow.height))) : 1;
  const setFrames = (n: number) => {
    n = Math.max(1, Math.min(2, n));
    setOw((prev) => {
      const p = prev || { width: 1, height: 1, cells: [] as string[], palette: 0 };
      const per = p.width * p.height;
      const target = per * n;
      const cells = (p.cells || []).slice(0, target);
      while (cells.length < target) cells.push((cells[0]) || (owTiles[0] && owTiles[0].id) || '');
      return { width: p.width, height: p.height, cells, palette: p.palette };
    });
    setDirty(true);
  };

  return (
    <div style={{ display: 'flex', gap: 16, padding: 16, height: '100%', overflow: 'auto', background: '#f4efe4', color: '#222' }}>
      <div style={{ minWidth: 190 }}>
        <div style={{ fontWeight: 'bold', marginBottom: 4 }}>Enemies (by type)</div>
        {enemies.map((e) => (
          <div key={e.id}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
              <button style={{ width: '100%', fontWeight: e.id === activeId ? 'bold' : 'normal', textAlign: 'left', background: 'none', border: 'none', color: 'inherit', cursor: 'pointer', padding: 2 }} onClick={() => setActiveId(e.id)}>
                {e.label}
              </button>
              <select
                value={enemyRows[e.id]?.category ?? e.category}
                onChange={(ev) => {
                  const category = ev.target.value as 'minion' | 'elite' | 'boss';
                  setEnemyRows((prev) => ({ ...prev, [e.id]: { art: prev[e.id]?.art ?? e.art ?? '', frames: prev[e.id]?.frames ?? 1, category, dirty: true } }));
                }}
                style={{ fontSize: 10, padding: '1px 4px', background: '#fff', border: '1px solid #ccc', borderRadius: 3 }}
              >
                <option value="minion">Minion</option>
                <option value="elite">Elite</option>
                <option value="boss">Boss</option>
              </select>
            </div>
          </div>
        ))}
        <div style={{ fontSize: 12, color: '#555', marginTop: 8 }}>
          New types are raw JSON for now: add <code>{'screens/enemy_types/<id>.json'}</code> and it appears here.
        </div>
      </div>

      {full && active && (
        <div style={{ display: 'flex', gap: 24 }}>
          <div>
            <h3 style={{ margin: '0 0 4px' }}>{active.label} — overworld sprite</h3>
            <div style={{ fontSize: 12, color: '#555', marginBottom: 8 }}>
              One transparent-background sprite shared by every world. Applies everywhere a {active.id} is placed.
            </div>
            {(ow ? ow.cells : []).length > 0 && ow && (
              <div style={{ marginBottom: 6 }}>
                {Array.from({ length: frameCount }).map((_, f) => (
                  <div key={f} style={{ marginBottom: 8 }}>
                    <div style={{ fontSize: 12, color: '#555', marginBottom: 4 }}>Frame {f + 1} (grid {ow.width}×{ow.height}):</div>
                    <div style={{ display: 'inline-grid', gridTemplateColumns: `repeat(${ow.width}, auto)`, gap: 2 }}>
                      {Array.from({ length: ow.width * ow.height }).map((_, i) => {
                        const idx = f * ow.width * ow.height + i;
                        const cell = ow.cells[idx];
                        return (
                          <div key={i} style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2, border: '1px solid #bbb', padding: 2, background: '#fff' }}>
                            <img src={tileUrl(cell)} alt={cell} width={32} height={32} style={{ imageRendering: 'pixelated', background: '#888' }} />
                            <select value={cell} onChange={(e) => setCell(idx, e.target.value)} style={{ fontSize: 10, width: 90 }}>
                              {owTiles.map((t) => <option key={t.id} value={t.id}>{t.label}</option>)}
                            </select>
                          </div>
                        );
                      })}
                    </div>
                  </div>
                ))}
              </div>
            )}
            <div style={{ marginTop: 6, fontSize: 13, display: 'flex', gap: 10, flexWrap: 'wrap' }}>
              <span>
                Grid: W <button onClick={() => setDims(((ow && ow.width) || 1) - 1, (ow && ow.height) || 1)}>-</button>{' '}
                {(ow && ow.width) || 1}{' '}
                <button onClick={() => setDims(((ow && ow.width) || 1) + 1, (ow && ow.height) || 1)}>+</button>{' '}
                H <button onClick={() => setDims((ow && ow.width) || 1, ((ow && ow.height) || 1) - 1)}>-</button>{' '}
                {(ow && ow.height) || 1}{' '}
                <button onClick={() => setDims((ow && ow.width) || 1, ((ow && ow.height) || 1) + 1)}>+</button>{' '}
                (1-2)
              </span>
              <span>
                Frames: <button onClick={() => setFrames(frameCount - 1)}>-</button>{' '}
                {frameCount}{' '}
                <button onClick={() => setFrames(frameCount + 1)}>+</button>{' '}
                (max 2)
              </span>
            </div>
            <div style={{ marginTop: 6, fontSize: 13 }}>
              <label>Palette (OBJ ramp):{' '}
                <RampSelect
                  value={(ow && ow.palette) ?? 0}
                  filter="obj"
                  disabled={!ow}
                  title={ow ? undefined : 'Add a shared sprite first'}
                  onPick={(ramp) => {
                    setOw((prev) => ({ ...(prev || { width: 1, height: 1, cells: [] as string[] }), palette: ramp }));
                    setDirty(true);
                  }}
                  onEditRamp={onEditRamp}
                />
              </label>
            </div>
            {!ow && (
              <button style={{ marginTop: 8 }} onClick={() => { setOw({ width: 1, height: 1, cells: [owTiles[0] ? owTiles[0].id : ''], palette: 0 }); setDirty(true); }}>
                Add shared sprite
              </button>
            )}
            {ow && (
              <button style={{ marginTop: 8 }} onClick={() => { setOw(null); setDirty(true); }}>
                Remove (legacy kinds)
              </button>
            )}
          </div>

          <div>
            <h3 style={{ margin: '0 0 4px' }}>Battle tiles</h3>
            <div style={{ fontSize: 13 }}>
              {battleSet ? `${battleSet.label} (${battleSet.width}x${battleSet.height})` : 'text fallback (none)'}
            </div>
            <div style={{ fontSize: 12, color: '#555', margin: '4px 0 8px' }}>
              Battle art is assigned in the Combat Art Studio (metatiles supported).
            </div>
            <button onClick={onOpenComposer}>Open Combat Art Studio</button>
          </div>

          <div>
            <h3 style={{ margin: '0 0 4px' }}>Category</h3>
            <div style={{ fontSize: 13 }}>
              <select
                value={active.category}
                onChange={async (ev) => {
                  const category = ev.target.value as 'minion' | 'elite' | 'boss';
                  setEnemyRows((prev) => ({ ...prev, [active.id]: { art: prev[active.id]?.art ?? active.art ?? '', frames: prev[active.id]?.frames ?? 1, category, dirty: true } }));
                  try {
                    const full = await fetchEnemyType(active.id);
                    full.category = category;
                    await saveEnemyType(active.id, full);
                    setEnemyRows((prev) => ({ ...prev, [active.id]: { art: prev[active.id]?.art ?? active.art ?? '', frames: prev[active.id]?.frames ?? 1, category, dirty: false } }));
                    setStatus(`saved screens/enemy_types/${active.id}.json — run make screens to recompile`);
                  } catch (err: any) {
                    setStatus(`save failed: ${err.message}`);
                  }
                }}
              >
                <option value="minion">Minion</option>
                <option value="elite">Elite</option>
                <option value="boss">Boss</option>
              </select>
            </div>
            <div style={{ fontSize: 12, color: '#555', margin: '4px 0 8px' }}>
              Category determines battle screen filtering (screens/battle/*.json allowed_categories).
            </div>

            <h3 style={{ margin: '8px 0 4px' }}>Defaults</h3>
            <div style={{ fontSize: 12, color: '#555', margin: '0 0 8px' }}>
              Seeded into new placements by the map editor's Enemy Type dropdown; per-instance values edited there override these.
            </div>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6, alignItems: 'end' }}>
              <label style={{ fontSize: 12 }}>Name
                <input style={{ width: '100%' }} type="text" value={(full && full.name) || ''}
                  onChange={(e) => { setFull((prev: any) => ({ ...prev, name: e.target.value })); setDirty(true); }} />
              </label>
              <label style={{ fontSize: 12 }}>Battle
                <select style={{ width: '100%' }} value={(full && full.battle_id) || 'BATTLE_NONE'}
                  onChange={(e) => { setFull((prev: any) => ({ ...prev, battle_id: e.target.value })); setDirty(true); }}>
                  {BATTLE_IDS.map((b) => <option key={b} value={b}>{b}</option>)}
                  {full && full.battle_id && !BATTLE_IDS.includes(full.battle_id) && (
                    <option value={full.battle_id}>{full.battle_id} (custom)</option>
                  )}
                </select>
              </label>
              <label style={{ fontSize: 12 }}>HP
                <input style={{ width: '100%' }} type="number" min={0} value={(full && full.hp) ?? 1}
                  onChange={(e) => { setFull((prev: any) => ({ ...prev, hp: parseInt(e.target.value) || 0 })); setDirty(true); }} />
              </label>
              <label style={{ fontSize: 12 }}>Max HP
                <input style={{ width: '100%' }} type="number" min={0} value={(full && full.max_hp) ?? 1}
                  onChange={(e) => { setFull((prev: any) => ({ ...prev, max_hp: parseInt(e.target.value) || 0 })); setDirty(true); }} />
              </label>
              <label style={{ fontSize: 12 }}>Gold reward
                <input style={{ width: '100%' }} type="number" min={0} value={(full && full.gold_reward) ?? 0}
                  onChange={(e) => { setFull((prev: any) => ({ ...prev, gold_reward: parseInt(e.target.value) || 0 })); setDirty(true); }} />
              </label>
              <label style={{ fontSize: 12 }}>AI
                <select style={{ width: '100%' }} value={(full && full.ai_types && full.ai_types[0]) || 'AI_NONE'}
                  onChange={(e) => { setFull((prev: any) => ({ ...prev, ai_types: [e.target.value] })); setDirty(true); }}>
                  {AI_IDS.map((a) => <option key={a} value={a}>{a}</option>)}
                </select>
              </label>
            </div>

            <button style={{ marginTop: 10 }} onClick={save} disabled={!dirty}>Save enemy{dirty ? ' *' : ''}</button>
            {status && <div style={{ marginTop: 8, fontSize: 13 }}>{status}</div>}
          </div>
        </div>
      )}
    </div>
  );
};