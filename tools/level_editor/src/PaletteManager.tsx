import React, { useEffect, useRef, useState } from 'react';
import {
  PaletteData, Ramp, TILESETS, fetchPalettes, assignPalette,
} from './io/palettes';
import { RampGuide, fetchRampGuide, refreshRampGuide, clearRecolorCache, recoloredImage } from './io/romRecolor';
import { RampEditor } from './RampEditor';

/** Palette preview / assignment.
 *
 *  The engine's CGB ramps are fixed: 8 BG ramps per tileset (from
 *  generated/tiles/<tileset>.json, compiled from tiles_content.c) and 8
 *  OBJ ramps (generated/tiles/obj.json) shared by all sprites.  This view
 *  renders a tile or enemy sprite recolored under any ramp with the exact
 *  pipeline mapping (png2gb.nearest_shade: exact match wins, else nearest
 *  shade within the ramp), so the author sees what the ROM will render.
 *
 *  BG assignment writes an explicit `palette` into the tileset JSON
 *  (palette_compiler.py honors it); enemy/hero assignment writes
 *  `overworld.palette` (already data-driven).  Recompile to apply.
  */

/** Thumbnail with pipeline-exact recolor. Shows the current image until
 *  the new recolor resolves (no flash-to-raw), reuses the shared
 *  (src, ramp) cache so wheel drags over seen hues are instant, and drops
 *  out-of-order resolutions via a generation counter (a slow older
 *  generation must never overwrite a newer one mid-drag). */
const Recolored: React.FC<{
  src: string; colors: string[]; size: number; title?: string;
  transparent0?: boolean;
}> =
  ({ src, colors, size, title, transparent0 }) => {
    const [out, setOut] = useState<string>(src);
    const genRef = useRef(0);
    const prevSrcRef = useRef(src);
    // eslint-disable-next-line react-hooks/exhaustive-deps
    const key = colors.join(',');
    useEffect(() => {
      const gen = ++genRef.current;
      if (prevSrcRef.current !== src) {
        prevSrcRef.current = src;
        setOut(src);
      }
      let cancelled = false;
      recoloredImage(src, colors, !!transparent0).then((img) => {
        if (cancelled || gen !== genRef.current) return;
        const url = img.src;
        setOut((prev) => (prev === url ? prev : url));
      }).catch(() => undefined);
      return () => { cancelled = true; };
      // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [src, key, transparent0]);
    return (
      <img src={out} title={title} width={size} height={size}
           style={{ imageRendering: 'pixelated', background: 'transparent' }} />
    );
  };

const Swatches: React.FC<{ ramp: Ramp; active: boolean; onClick: () => void; label?: string }> =
  ({ ramp, active, onClick, label }) => (
    <button onClick={onClick} title={ramp.name}
      style={{
        display: 'flex', alignItems: 'center', gap: 6, padding: 4, cursor: 'pointer',
        border: active ? '2px solid #1a7' : '1px solid #999', background: '#fff', color: '#222',
      }}>
      <span style={{ width: 60 }}>{label || `${ramp.index}`}</span>
      {ramp.colors.map((c, i) => (
        <span key={i} style={{ width: 16, height: 16, background: c, border: '1px solid #444' }} />
      ))}
      <span style={{ fontSize: 11, color: '#555' }}>{ramp.name}</span>
    </button>
  );

export const PaletteManager: React.FC<{
  onPaletteSaved?: () => void;
  /** Deep link: pre-open the ramp editor once, then consume. Set by the
   *  ✎ jump in enemy/hero/combat views via App. */
  editRequest?: string | null;
  onEditRequestConsumed?: () => void;
}> = ({
  onPaletteSaved, editRequest, onEditRequestConsumed,
}) => {
  const [tileset, setTileset] = useState<string>('forest');
  const [data, setData] = useState<PaletteData | null>(null);
  const [status, setStatus] = useState('');
  const [selTile, setSelTile] = useState<string>('');
  const [selEnemy, setSelEnemy] = useState<string>('');
  const [ramp, setRamp] = useState<number>(0);
  const [objRamp, setObjRamp] = useState<number>(0);
  // Ramp authoring: the static export (committed, always present) backs the
  // editor panel; editPreview carries live drag colors (no writes).
  const [guide, setGuide] = useState<RampGuide | null>(null);
  const [editRamp, setEditRamp] = useState<string | null>(null);
  const [editPreview, setEditPreview] = useState<{ ramp: string; colors: string[] } | null>(null);

  useEffect(() => {
    fetchPalettes(tileset)
      .then((d) => { setData(d); setStatus(''); })
      .catch((e) => setStatus(
        `load failed: ${e.message}. Run \`make manifest\` once, then restart the editor via \`make editor\`.`));
  }, [tileset]);
  useEffect(() => {
    fetchRampGuide().then(setGuide).catch(() => setGuide(null));
  }, []);
  // Deep-link consume: open the requested ramp editor once (the keyed
  // RampEditor below gets a fresh working copy for it).
  useEffect(() => {
    if (editRequest) {
      setEditRamp(editRequest);
      if (onEditRequestConsumed) onEditRequestConsumed();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [editRequest]);

  const reloadAll = async () => {
    try {
      setData(await fetchPalettes(tileset));
    } catch (e: any) {
      setStatus(`reload failed: ${e.message}`);
    }
    try {
      // Bypass the in-memory guide cache: the save rewrote the export.
      setGuide(await refreshRampGuide());
    } catch {
      // Static export is best-effort; palettes data is authoritative.
    }
  };

  // Preview-aware ramp colors: the edited ramp renders its candidate
  // colors everywhere while the editor panel is open.
  const bgColors = (r: Ramp): string[] =>
    editPreview && editPreview.ramp === r.name ? editPreview.colors : r.colors;

  const tile = data?.tiles.find((t) => t.id === selTile) || null;
  const enemy = data?.enemies.find((e) => e.id === selEnemy) || null;
  // 'sprites' is a pseudo-tileset: every pipeline sprite-sheet cell
  // (enemy_ow + hero_ow) with its OBJ ramp. Per-cell assignment does not
  // exist there (enemy types own their palette), so assign stays hidden.
  const isSprites = tileset === 'sprites';

  const assignTile = async (i: number) => {
    if (!tile) return;
    try {
      await assignPalette('tile', tile.id, i, tileset);
      setData({ ...data!, tiles: data!.tiles.map((t) => t.id === tile.id ? { ...t, palette: i } : t) });
      setRamp(i);
      setStatus(`tile ${tile.id} -> palette ${i}. Recompile to apply.`);
      if (onPaletteSaved) onPaletteSaved();
    } catch (e: any) { setStatus(`assign failed: ${e.message}`); }
  };
  const assignEnemy = async (i: number) => {
    if (!enemy) return;
    try {
      await assignPalette('enemy', enemy.id, i);
      setData({ ...data!, enemies: data!.enemies.map((e) => e.id === enemy.id ? { ...e, palette: i } : e) });
      setObjRamp(i);
      setStatus(`enemy ${enemy.id} -> OBJ palette ${i}. Recompile to apply.`);
      if (onPaletteSaved) onPaletteSaved();
    } catch (e: any) { setStatus(`assign failed: ${e.message}`); }
  };

  const pickTileset = (t: string) => {
    setTileset(t);
    setSelTile('');
    setSelEnemy('');
    setEditRamp(null);
    setEditPreview(null);
  };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', padding: 16, height: '100%', overflow: 'auto', background: '#f4efe4', color: '#222' }}>
      <div style={{
        display: 'flex', gap: 12, alignItems: 'center', flexWrap: 'wrap',
        padding: '8px 12px', marginBottom: 12, background: '#fff',
        border: '1px solid #ccc', borderRadius: 6,
      }}>
        <span style={{ fontWeight: 'bold' }}>Tileset</span>
        <select
          className="select-input"
          value={tileset}
          onChange={(e) => pickTileset(e.target.value)}
          title="Pick the tileset whose ramps and tiles to preview"
        >
          {TILESETS.map((t) => (
            <option key={t} value={t}>{t}</option>
          ))}
        </select>
        <span style={{ fontSize: 11, color: '#555', lineHeight: 1.4 }}>
          BG ramps come from generated/tiles/&lt;tileset&gt;.json; OBJ ramps
          from generated/tiles/obj.json. Assign writes into the content JSON;
          the ✎ button edits ramp colors in palette.txt (full manifest
          refresh on save).
        </span>
        {!data && status && (
          <span style={{ fontSize: 12, color: '#a00', lineHeight: 1.4 }}>
            {status}
          </span>
        )}
      </div>

      {data && (
        <div style={{ width: '100%', maxWidth: 1100, margin: '0 auto' }}>
          <h3 style={{ margin: '0 0 6px', textAlign: 'center' }}>
            {isSprites ? 'Sprite palettes (OBJ ramps — enemy_ow + hero_ow)' : `Background palettes (${tileset})`}
          </h3>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, justifyContent: 'center' }}>
            {data.bg.map((r) => (
              <div key={r.index} style={{ display: 'flex', gap: 2, alignItems: 'stretch' }}>
                <Swatches ramp={r} active={r.index === ramp} onClick={() => setRamp(r.index)} />
                <button
                  className="btn btn-sm"
                  title={`Edit ramp ${r.name} (color picker writes palette.txt)`}
                  onClick={() => { setEditRamp(r.name); }}
                  style={{ cursor: 'pointer' }}
                >✎</button>
              </div>
            ))}
          </div>

          {guide && editRamp && data.bg.some((r) => r.name === editRamp) && (
            <RampEditor
              key={editRamp}
              rampName={editRamp}
              guide={guide}
              data={data}
              onPreview={setEditPreview}
              onClose={() => { setEditRamp(null); setEditPreview(null); }}
              onSaved={(summary) => {
                setStatus(summary);
                setEditRamp(null);
                setEditPreview(null);
                // Ramp colors changed: drop cached recolors and push the new
                // generation to the level views (map canvas, tile picker).
                clearRecolorCache();
                if (onPaletteSaved) onPaletteSaved();
                reloadAll();
              }}
            />
          )}

          {tile && (
            <div style={{ marginTop: 10, padding: 8, border: '1px solid #999' }}>
              <b>{tile.label}</b> <code style={{ fontSize: 11 }}>{tile.id}</code>
              {!tile.image_url && (
                <div style={{ fontSize: 11, color: '#a00', marginTop: 4 }}>
                  No preview PNG for this cell — the sheet still encodes it for the ROM.
                </div>
              )}
              {isSprites && (
                <div style={{ fontSize: 11, color: '#555', marginTop: 4 }}>
                  Per-cell assignment does not exist for sprites — ramps are owned
                  by enemy types (see Enemies below).
                </div>
              )}
              {tile.image_url && (
                <div style={{ display: 'flex', gap: 10, marginTop: 6, flexWrap: 'wrap', justifyContent: 'center' }}>
                  {data.bg.map((r) => (
                    <div key={r.index} style={{ textAlign: 'center' }}>
                      <Recolored src={tile.image_url || ''} colors={bgColors(r)} size={48} title={r.name} />
                      <div style={{ fontSize: 10 }}>{r.index} {r.name}</div>
                      {!isSprites && (
                        <button className="btn btn-sm" onClick={() => assignTile(r.index)}>
                          {tile.palette === r.index ? '✓' : 'assign'}
                        </button>
                      )}
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}

          <div style={{ marginTop: 12, fontWeight: 600, textAlign: 'center' }}>
            {isSprites ? 'Sprite cells (click to preview under every ramp)' : 'Tiles (click to preview/assign)'}
          </div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 6, justifyContent: 'center' }}>
            {data.tiles.map((t) => (
              <button key={t.id} onClick={() => setSelTile(t.id)} title={`${t.label} — palette ${t.palette}`}
                style={{ padding: 2, border: t.id === selTile ? '2px solid #1a7' : '1px solid #bbb',
                         background: t.id === selTile ? '#e8f5ee' : '#fff', cursor: 'pointer' }}>
                {t.image_url ? (
                  <Recolored
                    src={t.image_url}
                    colors={(() => {
                      const r = data.bg[t.palette] || data.bg[0];
                      return r ? bgColors(r) : data.bg[0].colors;
                    })()}
                    size={32}
                  />
                ) : (
                  <div title={`${t.id} — no preview PNG`} style={{
                    width: 32, height: 32, border: '1px dashed #999', color: '#999',
                    fontSize: 9, display: 'flex', alignItems: 'center',
                    justifyContent: 'center', textAlign: 'center', lineHeight: 1.1,
                  }}>
                    no png
                  </div>
                )}
              </button>
            ))}
          </div>

          <h3 style={{ margin: '16px 0 6px', textAlign: 'center' }}>Object palettes (all sprites)</h3>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, justifyContent: 'center' }}>
            {data.obj.map((r) => (
              <div key={r.index} style={{ display: 'flex', gap: 2, alignItems: 'stretch' }}>
                <Swatches ramp={r} active={r.index === objRamp} onClick={() => setObjRamp(r.index)}
                  label={`OBJ ${r.index}`} />
                <button
                  className="btn btn-sm"
                  title={`Edit ramp ${r.name} (color picker writes palette.txt)`}
                  onClick={() => { setEditRamp(r.name); }}
                  style={{ cursor: 'pointer' }}
                >✎</button>
              </div>
            ))}
          </div>

          {guide && editRamp && !data.bg.some((r) => r.name === editRamp) && (
            <RampEditor
              key={editRamp}
              rampName={editRamp}
              guide={guide}
              data={data}
              onPreview={setEditPreview}
              onClose={() => { setEditRamp(null); setEditPreview(null); }}
              onSaved={(summary) => {
                setStatus(summary);
                setEditRamp(null);
                setEditPreview(null);
                // Ramp colors changed: drop cached recolors and push the new
                // generation to the level views (map canvas, tile picker).
                clearRecolorCache();
                if (onPaletteSaved) onPaletteSaved();
                reloadAll();
              }}
            />
          )}
          <div style={{ fontSize: 11, color: '#777', marginTop: 4 }}>
            OBJ ramps come from generated/tiles/obj.json (palette_compiler).
            Recoloring uses the exact pipeline mapping (exact match, else
            nearest shade in the ramp; white is the transparent key).
          </div>

          {enemy && (
            <div style={{ marginTop: 8, padding: 8, border: '1px solid #999' }}>
              <b>{enemy.label}</b> <code style={{ fontSize: 11 }}>{enemy.id}</code>
              {enemy.ramp && (
                <span style={{ fontSize: 11, color: '#555' }}> — ramp {enemy.ramp}, cells: {enemy.cells.join(', ') || 'none'}</span>
              )}
              {!enemy.image_url && (
                <div style={{ fontSize: 11, color: '#a00', marginTop: 4 }}>
                  No preview PNG for this enemy type.
                </div>
              )}
              {(() => {
                const src = enemy.image_url;
                return src && (
                  <div style={{ display: 'flex', gap: 10, marginTop: 6, flexWrap: 'wrap', justifyContent: 'center' }}>
                    {data.obj.map((r) => (
                      <div key={r.index} style={{ textAlign: 'center' }}>
                        <Recolored src={src} colors={bgColors(r)} size={48} title={r.name} transparent0 />
                      <div style={{ fontSize: 10 }}>OBJ {r.index} {r.name}</div>
                      <button className="btn btn-sm" onClick={() => assignEnemy(r.index)}>
                        {enemy.palette === r.index ? '✓' : 'assign'}
                      </button>
                      </div>
                    ))}
                  </div>
                );
              })()}
            </div>
          )}

          <div style={{ marginTop: 12, fontWeight: 600, textAlign: 'center' }}>Enemies (click to preview/assign)</div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 6, justifyContent: 'center' }}>
            {data.enemies.map((e) => (
              <button key={e.id} onClick={() => setSelEnemy(e.id)}
                title={`${e.label} — OBJ ${e.palette}${e.ramp ? ` ${e.ramp}` : ''}${e.cells.length > 0 ? ` — ${e.cells.join(', ')}` : ''}`}
                style={{ padding: 2, border: e.id === selEnemy ? '2px solid #1a7' : '1px solid #bbb',
                         background: e.id === selEnemy ? '#e8f5ee' : '#fff', cursor: 'pointer' }}>
                {e.image_url ? (
                  <Recolored
                    src={e.image_url}
                    colors={(() => {
                      const r = data.obj[e.palette] || data.obj[0];
                      return r ? bgColors(r) : data.obj[0].colors;
                    })()}
                    size={40}
                    transparent0
                  />
                ) : (
                  <div title={`${e.id} — no preview PNG`} style={{
                    width: 40, height: 40, border: '1px dashed #999', color: '#999',
                    fontSize: 9, display: 'flex', alignItems: 'center',
                    justifyContent: 'center', textAlign: 'center', lineHeight: 1.1,
                  }}>
                    no png
                  </div>
                )}
              </button>
            ))}
          </div>

          {status && <div style={{ marginTop: 10, fontSize: 12, color: '#555', textAlign: 'center' }}>{status}</div>}
        </div>
      )}
    </div>
  );
};
