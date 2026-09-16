import React, { useEffect, useMemo, useState } from 'react';
import {
  CombatArtSet, CombatArtListItem, EnemyTypeListItem,
  SHEET_TILE_NAMES, COMBAT_BRUSH_NAMES, COMBAT_TILE_URL, MAX_ART_W, MAX_ART_H, frameCells,
  fetchCombatArtList, fetchCombatArtSet, saveCombatArtSet,
  fetchEnemyTypeList, fetchEnemyType, saveEnemyType,
} from './io/combatArt';

/** Battle-art studio: combat-art meta-tile composer + per-enemy combat
 *  sprite assignment.  Edits screens/combat_art/*.json (compiled by
 *  battle_compile.py into the ROM blob + offsets) and the sprite.art key
 *  of screens/enemy_types/*.json.  Overworld sprites stay on placed level
 *  enemies (Inspector); this modal owns the combat side. */

const cellPx = 30;

function blankSet(id: string, order: number): CombatArtSet {
  const cells: Array<string | null> = new Array(3 * 2).fill(null);
  return {
    $schema: '../schema/combat_art.schema.json',
    id, label: id, order, width: 3, height: 2, palette: '',
    frame0: cells.slice(),
  };
}

function resizeCells(cells: Array<string | null>, oldW: number, oldH: number, w: number, h: number): Array<string | null> {
  const out: Array<string | null> = [];
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      out.push(x < oldW && y < oldH ? cells[y * oldW + x] : null);
    }
  }
  return out;
}

export const CombatArtStudio: React.FC<{ onClose: () => void }> = ({ onClose }) => {
  const [tab, setTab] = useState<'sets' | 'enemies'>('sets');
  const [sets, setSets] = useState<CombatArtListItem[]>([]);
  const [activeId, setActiveId] = useState<string>('');
  const [set, setSet] = useState<CombatArtSet | null>(null);
  const [frame, setFrame] = useState<0 | 1>(0);
  const [brush, setBrush] = useState<string | null>(null);
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState('');
  const [images, setImages] = useState<Map<string, HTMLImageElement>>(new Map());
  const [enemies, setEnemies] = useState<EnemyTypeListItem[]>([]);
  const [enemyRows, setEnemyRows] = useState<Record<string, { art: string; frames: number; dirty: boolean }>>({});

  useEffect(() => {
    fetchCombatArtList().then((items) => {
      setSets(items);
      if (items.length > 0) setActiveId(items[0].id);
    }).catch((e) => setStatus(`load failed: ${e.message}`));
    fetchEnemyTypeList().then(setEnemies).catch(() => undefined);
    const map = new Map<string, HTMLImageElement>();
    let done = 0;
    COMBAT_BRUSH_NAMES.forEach((name) => {
      const img = new Image();
      img.src = COMBAT_TILE_URL(name);
      const fin = () => { done++; map.set(name, img); if (done === COMBAT_BRUSH_NAMES.length) setImages(new Map(map)); };
      img.onload = fin;
      img.onerror = fin;
    });
  }, []);

  useEffect(() => {
    if (!activeId) { setSet(null); return; }
    fetchCombatArtSet(activeId).then((s) => { setSet(s); setFrame(0); setDirty(false); setStatus(''); })
      .catch((e) => setStatus(`load failed: ${e.message}`));
  }, [activeId]);

  const refreshEnemies = () => {
    fetchEnemyTypeList().then((items) => {
      setEnemies(items);
      const rows: Record<string, { art: string; frames: number; dirty: boolean }> = {};
      items.forEach((e) => { rows[e.id] = { art: e.art || '', frames: 1, dirty: false }; });
      setEnemyRows(rows);
    }).catch((e) => setStatus(`enemies failed: ${e.message}`));
  };
  useEffect(() => { if (tab === 'enemies') refreshEnemies(); }, [tab]);

  const mutate = (fn: (s: CombatArtSet) => CombatArtSet) => {
    setSet((prev) => {
      if (!prev) return prev;
      const next = fn(JSON.parse(JSON.stringify(prev)));
      setDirty(true);
      return next;
    });
  };

  const paint = (idx: number) => {
    mutate((s) => {
      const cells = frameCells(s, frame).slice();
      cells[idx] = brush;
      if (frame === 0) s.frame0 = cells;
      else s.frame1 = cells;
      return s;
    });
  };

  const changeDims = (w: number, h: number) => {
    if (!set) return;
    w = Math.max(1, Math.min(MAX_ART_W, w));
    h = Math.max(1, Math.min(MAX_ART_H, h));
    mutate((s) => {
      s.frame0 = resizeCells(frameCells(s, 0), s.width, s.height, w, h);
      if (s.frame1) s.frame1 = resizeCells(frameCells(s, 1), s.width, s.height, w, h);
      s.width = w;
      s.height = h;
      return s;
    });
  };

  const save = async () => {
    if (!set) return;
    try {
      await saveCombatArtSet(set);
      setDirty(false);
      setStatus(`saved screens/combat_art/${set.id}.json — run make screens + make gfx to recompile`);
      fetchCombatArtList().then(setSets).catch(() => undefined);
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  const createSet = () => {
    const id = (window.prompt('New combat-art id (letters, digits, underscore):', 'my_set') || '').trim();
    if (!id || !/^[A-Za-z0-9_]+$/.test(id)) return;
    if (sets.some((s) => s.id === id)) { setStatus(`id '${id}' already exists`); return; }
    const order = sets.reduce((m, s) => Math.max(m, s.order), -1) + 1;
    const fresh = blankSet(id, order);
    saveCombatArtSet(fresh).then(() => {
      fetchCombatArtList().then((items) => { setSets(items); setActiveId(id); });
      setStatus(`created '${id}' at order ${order} (appended: blob offsets stay stable)`);
    }).catch((e: any) => setStatus(`create failed: ${e.message}`));
  };

  const saveEnemyRow = async (id: string) => {
    const row = enemyRows[id];
    if (!row) return;
    try {
      const data = await fetchEnemyType(id);
      data.sprite = row.art ? { art: row.art, frames: row.frames } : null;
      await saveEnemyType(id, data);
      setEnemyRows((prev) => ({ ...prev, [id]: { ...row, dirty: false } }));
      setStatus(`saved screens/enemy_types/${id}.json — run make screens to recompile`);
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  const cells = useMemo(() => (set ? frameCells(set, frame) : []), [set, frame]);
  const problems = useMemo(() => {
    const out: string[] = [];
    if (set) {
      if (set.frame0.length !== set.width * set.height) out.push('frame0 length != W*H');
      if (set.frame1 && set.frame1.length !== set.width * set.height) out.push('frame1 length != W*H');
      const unknown = cells.filter((c) => c !== null && !COMBAT_BRUSH_NAMES.includes(c as string));
      if (unknown.length > 0) out.push(`unknown tiles: ${[...new Set(unknown)].join(', ')}`);
      const noSheet = cells.filter((c) => c !== null && !SHEET_TILE_NAMES.includes(c as string));
      if (noSheet.length > 0) out.push(`not compiled to ROM (add LAYOUT entry + compose + make gfx): ${[...new Set(noSheet)].join(', ')}`);
    }
    return out;
  }, [set, cells]);

  const renderGrid = (f: 0 | 1, interactive: boolean) => {
    if (!set) return null;
    const list = frameCells(set, f);
    return (
      <div style={{ display: 'grid', gridTemplateColumns: `repeat(${set.width}, ${cellPx}px)`, gap: 1, background: '#222', padding: 2, width: 'fit-content' }}>
        {list.map((name, i) => (
          <div
            key={i}
            onClick={() => { if (interactive) paint(i); }}
            title={name || 'blank'}
            style={{
              width: cellPx, height: cellPx, background: '#fff', cursor: interactive ? 'crosshair' : 'default',
              outline: interactive && frame === f ? 'none' : undefined, position: 'relative',
            }}
          >
            {name && images.get(name) && (
              <img src={COMBAT_TILE_URL(name)} alt={name} width={cellPx} height={cellPx} style={{ imageRendering: 'pixelated', display: 'block' }} draggable={false} />
            )}
          </div>
        ))}
      </div>
    );
  };

  return (
    <div style={{ position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.55)', zIndex: 60, display: 'flex', justifyContent: 'center', alignItems: 'center' }} onClick={onClose}>
      <div style={{ background: '#f4efe4', color: '#222', width: 'min(1060px, 96vw)', maxHeight: '92vh', overflow: 'auto', padding: 16, borderRadius: 8 }} onClick={(e) => e.stopPropagation()}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <h2 style={{ margin: 0 }}>Combat Art Studio</h2>
          <button onClick={onClose}>Close</button>
        </div>
        <div style={{ margin: '8px 0' }}>
          <button disabled={tab === 'sets'} onClick={() => setTab('sets')}>Art Sets</button>{' '}
          <button disabled={tab === 'enemies'} onClick={() => setTab('enemies')}>Enemies</button>
        </div>

        {tab === 'sets' && (
          <div style={{ display: 'flex', gap: 16 }}>
            <div style={{ minWidth: 170 }}>
              <div style={{ fontWeight: 'bold', marginBottom: 4 }}>Sets (blob order)</div>
              {sets.map((s) => (
                <div key={s.id}>
                  <button
                    style={{ width: '100%', fontWeight: s.id === activeId ? 'bold' : 'normal' }}
                    onClick={() => setActiveId(s.id)}
                  >
                    {s.label} ({s.width}x{s.height})
                  </button>
                </div>
              ))}
              <button style={{ marginTop: 8 }} onClick={createSet}>+ New set</button>
            </div>

            {set && (
              <>
                <div>
                  <div style={{ marginBottom: 4 }}>
                    <button disabled={frame === 0} onClick={() => setFrame(0)}>Frame 0</button>{' '}
                    <button disabled={frame === 1} onClick={() => setFrame(1)}>Frame 1</button>{' '}
                    {!set.frame1 && <span style={{ fontSize: 12 }}>(frame 1 = frame 0)</span>}
                    {set.frame1 && frame === 1 && (
                      <button onClick={() => mutate((s) => { delete s.frame1; return s; })}>drop frame 1</button>
                    )}
                    {!set.frame1 && (
                      <button onClick={() => mutate((s) => { s.frame1 = s.frame0.slice(); return s; })}>split frame 1</button>
                    )}
                  </div>
                  {renderGrid(frame, true)}
                  <div style={{ marginTop: 8, fontSize: 13 }}>
                    W: <button onClick={() => changeDims(set.width - 1, set.height)}>-</button> {set.width} <button onClick={() => changeDims(set.width + 1, set.height)}>+</button>{' '}
                    H: <button onClick={() => changeDims(set.width, set.height - 1)}>-</button> {set.height} <button onClick={() => changeDims(set.width, set.height + 1)}>+</button>{' '}
                    <span style={{ color: '#666' }}>(max 6x4; overlaps HUD rows in battle)</span>
                  </div>
                  <div style={{ marginTop: 8, display: 'flex', gap: 12 }}>
                    <div>Preview F0:<br />{renderGrid(0, false)}</div>
                    <div>Preview F1:<br />{renderGrid(1, false)}</div>
                  </div>
                </div>

                <div style={{ minWidth: 220 }}>
                  <div style={{ fontWeight: 'bold' }}>Brush</div>
                  <div style={{ display: 'grid', gridTemplateColumns: `repeat(6, 26px)`, gap: 2, margin: '4px 0' }}>
                    <div
                      onClick={() => setBrush(null)}
                      title="blank"
                      style={{ width: 26, height: 26, background: '#fff', border: brush === null ? '2px solid #c00' : '1px solid #999', boxSizing: 'border-box' }}
                    />
                    {COMBAT_BRUSH_NAMES.map((name) => (
                      <div key={name} onClick={() => setBrush(name)}
                        title={SHEET_TILE_NAMES.includes(name) ? name : `${name} (not in ROM sheet)`}
                        style={{ width: 26, height: 26, background: '#fff', border: brush === name ? '2px solid #c00' : '1px solid #999', boxSizing: 'border-box' }}>
                        {images.get(name) && (
                          <img src={COMBAT_TILE_URL(name)} alt={name} width={22} height={22} style={{ imageRendering: 'pixelated', display: 'block' }} draggable={false} />
                        )}
                      </div>
                    ))}
                  </div>
                  <div style={{ fontSize: 12, color: '#555' }}>Every curated combat tile is brushable. Tiles not in the composed sheet (title "… (not in ROM sheet)") compile only after a LAYOUT entry + compose + make gfx.</div>
                  <div style={{ marginTop: 8 }}>
                    <label>Label: <input value={set.label} onChange={(e) => mutate((s) => { s.label = e.target.value; return s; })} /></label>
                  </div>
                  <div style={{ marginTop: 4 }}>
                    <label>Palette (base ramp name, e.g. fight_card, fight_standard): <input type="text" style={{ width: 130 }} placeholder="fight_card"
                      value={set.palette}
                      onChange={(e) => mutate((s) => { s.palette = e.target.value.trim(); return s; })} /></label>
                  </div>
                  <div style={{ marginTop: 4, fontSize: 13 }}>Order: {set.order} (stable, do not renumber)</div>
                  {problems.map((p) => <div key={p} style={{ color: '#a00', fontSize: 13 }}>{p}</div>)}
                  <div style={{ marginTop: 8 }}>
                    <button onClick={save} disabled={!dirty && problems.length === 0}>Save set{dirty ? ' *' : ''}</button>
                  </div>
                </div>
              </>
            )}
          </div>
        )}

        {tab === 'enemies' && (
          <div>
            <div style={{ fontSize: 13, color: '#555', marginBottom: 8 }}>
              Combat sprite per enemy type (shared everywhere the enemy appears). Overworld sprites stay on placed enemies in the Inspector.
            </div>
            <table>
              <thead><tr><th>Enemy</th><th>Category</th><th>Combat art</th><th>Frames</th><th></th></tr></thead>
              <tbody>
                {enemies.map((e) => {
                  const row = enemyRows[e.id];
                  return (
                    <tr key={e.id}>
                      <td>{e.label}</td>
                      <td>{e.category}</td>
                      <td>
                        <select
                          value={row ? row.art : e.art || ''}
                          onChange={async (ev) => {
                            const art = ev.target.value;
                            let frames = 1;
                            try {
                              const full = await fetchEnemyType(e.id);
                              frames = (full.sprite && full.sprite.frames) || 1;
                            } catch { /* keep default */ }
                            setEnemyRows((prev) => ({ ...prev, [e.id]: { art, frames, dirty: true } }));
                          }}
                        >
                          <option value="">-- text fallback --</option>
                          {sets.map((s) => <option key={s.id} value={s.id}>{s.label}</option>)}
                        </select>
                      </td>
                      <td>
                        <input type="number" min={1} max={2} style={{ width: 44 }}
                          value={row ? row.frames : 1}
                          onChange={(ev) => {
                            const frames = Math.max(1, Math.min(2, parseInt(ev.target.value) || 1));
                            setEnemyRows((prev) => ({ ...prev, [e.id]: { art: prev[e.id]?.art ?? e.art ?? '', frames, dirty: true } }));
                          }} />
                      </td>
                      <td>
                        <button disabled={!row?.dirty} onClick={() => saveEnemyRow(e.id)}>Save</button>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
            <div style={{ fontSize: 12, color: '#555', marginTop: 6 }}>
              Note: battles resolve art by BattleId (src/game/enemy_art_content.c) — a row only appears in battle once content maps its battle id.
            </div>
          </div>
        )}

        {status && <div style={{ marginTop: 8, fontSize: 13 }}>{status}</div>}
      </div>
    </div>
  );
};
