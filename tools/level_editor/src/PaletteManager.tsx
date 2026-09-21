import React, { useEffect, useState } from 'react';
import {
  PaletteData, Ramp, TILESETS, fetchPalettes, assignPalette,
} from './io/palettes';
import { recoloredDataUrl as romRecoloredDataUrl } from './io/romRecolor';

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

const Recolored: React.FC<{
  src: string; colors: string[]; size: number; title?: string;
  transparent0?: boolean;
}> =
  ({ src, colors, size, title, transparent0 }) => {
    const [out, setOut] = useState<string>(src);
    useEffect(() => {
      let alive = true;
      const img = new Image();
      img.onload = () => { if (alive) setOut(romRecoloredDataUrl(img, colors, !!transparent0)); };
      img.src = src;
      return () => { alive = false; };
      // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [src, colors.join(','), transparent0]);
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

export const PaletteManager: React.FC = () => {
  const [tileset, setTileset] = useState<string>('forest');
  const [data, setData] = useState<PaletteData | null>(null);
  const [status, setStatus] = useState('');
  const [selTile, setSelTile] = useState<string>('');
  const [selEnemy, setSelEnemy] = useState<string>('');
  const [ramp, setRamp] = useState<number>(0);
  const [objRamp, setObjRamp] = useState<number>(0);

  useEffect(() => {
    fetchPalettes(tileset)
      .then((d) => { setData(d); setStatus(''); })
      .catch((e) => setStatus(`load failed: ${e.message}`));
  }, [tileset]);

  const tile = data?.tiles.find((t) => t.id === selTile) || null;
  const enemy = data?.enemies.find((e) => e.id === selEnemy) || null;

  const assignTile = async (i: number) => {
    if (!tile) return;
    try {
      await assignPalette('tile', tile.id, i, tileset);
      setData({ ...data!, tiles: data!.tiles.map((t) => t.id === tile.id ? { ...t, palette: i } : t) });
      setRamp(i);
      setStatus(`tile ${tile.id} -> palette ${i}. Recompile to apply.`);
    } catch (e: any) { setStatus(`assign failed: ${e.message}`); }
  };
  const assignEnemy = async (i: number) => {
    if (!enemy) return;
    try {
      await assignPalette('enemy', enemy.id, i);
      setData({ ...data!, enemies: data!.enemies.map((e) => e.id === enemy.id ? { ...e, palette: i } : e) });
      setObjRamp(i);
      setStatus(`enemy ${enemy.id} -> OBJ palette ${i}. Recompile to apply.`);
    } catch (e: any) { setStatus(`assign failed: ${e.message}`); }
  };

  return (
    <div style={{ display: 'flex', gap: 16, padding: 16, height: '100%', overflow: 'auto', background: '#f4efe4', color: '#222' }}>
      <div style={{ minWidth: 150 }}>
        <div style={{ fontWeight: 'bold', marginBottom: 4 }}>Tileset</div>
        {TILESETS.map((t) => (
          <button key={t} onClick={() => { setTileset(t); setSelTile(''); setSelEnemy(''); }}
            style={{ display: 'block', width: '100%', textAlign: 'left', padding: 3,
                     background: 'none', border: 'none', cursor: 'pointer',
                     fontWeight: t === tileset ? 'bold' : 'normal' }}>
            {t}
          </button>
        ))}
        <div style={{ fontSize: 11, color: '#555', marginTop: 8, lineHeight: 1.4 }}>
          BG ramps come from the ROM's tiles_content.c; OBJ ramps from ui.c.
          Assign writes the current palette into the content JSON.
        </div>
      </div>

      {data && (
        <div style={{ flex: 1, minWidth: 380 }}>
          <h3 style={{ margin: '0 0 6px' }}>Background palettes ({tileset})</h3>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
            {data.bg.map((r) => (
              <Swatches key={r.index} ramp={r} active={r.index === ramp} onClick={() => setRamp(r.index)} />
            ))}
          </div>

          {tile && (
            <div style={{ marginTop: 10, padding: 8, border: '1px solid #999' }}>
              <b>{tile.label}</b> <code style={{ fontSize: 11 }}>{tile.id}</code>
              <div style={{ display: 'flex', gap: 10, marginTop: 6, flexWrap: 'wrap' }}>
                {data.bg.map((r) => (
                  <div key={r.index} style={{ textAlign: 'center' }}>
                    <Recolored src={tile.image_url || ''} colors={r.colors} size={48} title={r.name} />
                    <div style={{ fontSize: 10 }}>{r.index} {r.name}</div>
                    <button className="btn btn-sm" onClick={() => assignTile(r.index)}>
                      {tile.palette === r.index ? '✓' : 'assign'}
                    </button>
                  </div>
                ))}
              </div>
            </div>
          )}

          <div style={{ marginTop: 12, fontWeight: 600 }}>Tiles (click to preview/assign)</div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 6 }}>
            {data.tiles.filter((t) => t.image_url).map((t) => (
              <button key={t.id} onClick={() => setSelTile(t.id)} title={`${t.label} — palette ${t.palette}`}
                style={{ padding: 2, border: t.id === selTile ? '2px solid #1a7' : '1px solid #bbb',
                         background: t.id === selTile ? '#e8f5ee' : '#fff', cursor: 'pointer' }}>
                <Recolored src={t.image_url!} colors={data.bg[t.palette]?.colors || data.bg[0].colors} size={32} />
              </button>
            ))}
          </div>

          <h3 style={{ margin: '16px 0 6px' }}>Object palettes (all sprites)</h3>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
            {data.obj.map((r) => (
              <Swatches key={r.index} ramp={r} active={r.index === objRamp} onClick={() => setObjRamp(r.index)}
                label={`OBJ ${r.index}`} />
            ))}
          </div>
          <div style={{ fontSize: 11, color: '#777', marginTop: 4 }}>
            OBJ ramps come from generated/tiles/obj.json (palette_compiler).
            Recoloring uses the exact pipeline mapping (exact match, else
            nearest shade in the ramp; white is the transparent key).
          </div>

          {enemy && (
            <div style={{ marginTop: 8, padding: 8, border: '1px solid #999' }}>
              <b>{enemy.label}</b> <code style={{ fontSize: 11 }}>{enemy.id}</code>
              <div style={{ display: 'flex', gap: 10, marginTop: 6, flexWrap: 'wrap' }}>
                {data.obj.map((r) => (
                  <div key={r.index} style={{ textAlign: 'center' }}>
                    <Recolored src={enemy.image_url} colors={r.colors} size={48} title={r.name} transparent0 />
                    <div style={{ fontSize: 10 }}>OBJ {r.index} {r.name}</div>
                    <button className="btn btn-sm" onClick={() => assignEnemy(r.index)}>
                      {enemy.palette === r.index ? '✓' : 'assign'}
                    </button>
                  </div>
                ))}
              </div>
            </div>
          )}

          <div style={{ marginTop: 12, fontWeight: 600 }}>Enemies (click to preview/assign)</div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 6 }}>
            {data.enemies.map((e) => (
              <button key={e.id} onClick={() => setSelEnemy(e.id)} title={`${e.label} — OBJ ${e.palette}`}
                style={{ padding: 2, border: e.id === selEnemy ? '2px solid #1a7' : '1px solid #bbb',
                         background: e.id === selEnemy ? '#e8f5ee' : '#fff', cursor: 'pointer' }}>
                <Recolored src={e.image_url} colors={data.obj[e.palette]?.colors || data.obj[0].colors} size={40} transparent0 />
              </button>
            ))}
          </div>

          {status && <div style={{ marginTop: 10, fontSize: 12, color: '#555' }}>{status}</div>}
        </div>
      )}
    </div>
  );
};
