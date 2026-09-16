import React, { useEffect, useState } from 'react';
import { fetchEnemyTypeList } from './io/combatArt';
import { BUILTIN_TILESETS } from './model/Tileset';

/** Hero manager (art + stats + starter deck): one record for the hero.
 *  The level view owns placement; this view owns looks.  Selections apply
 *  everywhere the hero appears. */

interface HeroDraft {
  name: string;
  start_hp: number;
  start_gold: number;
  starter_deck: string[];
  overworld: { cells: string[]; palette: string } | null;
}

const owTiles = (BUILTIN_TILESETS.hero?.tiles || []).filter((t) => t.category === 'hero');
const tileUrl = (id: string) => {
  const t = owTiles.find((x) => x.id === id);
  return t ? t.image_url : `/tiles/hero/${id}.png`;
};

export const HeroManager: React.FC<{ onOpenComposer: () => void }> = ({ onOpenComposer }) => {
  const [hero, setHero] = useState<HeroDraft | null>(null);
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState('');

  useEffect(() => {
    fetch('/api/hero').then(r => r.json()).then(setHero).catch(() => setStatus('load failed'));
  }, []);

  const save = async () => {
    if (!hero) return;
    try {
      await fetch('/api/save-hero', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: 'hero', data: hero }),
      });
      setDirty(false);
      setStatus('saved screens/hero.json — run make screens + make gfx to recompile');
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  const setCell = (idx: number, id: string) => {
    setHero((prev) => {
      const cells = ((prev && prev.overworld && prev.overworld.cells) || []).slice();
      cells[idx] = id;
      const newHero = { ...prev!, overworld: { cells: cells.slice(0, 2), palette: prev?.overworld?.palette || '' } };
      setDirty(true);
      return newHero;
    });
  };

  const setFrames = (n: number) => {
    n = Math.max(1, Math.min(2, n));
    setHero((prev) => {
      const cells = ((prev && prev.overworld && prev.overworld.cells) || []).slice(0, n);
      while (cells.length < n) cells.push((prev && prev.overworld && prev.overworld.cells[0]) || (owTiles[0] && owTiles[0].id) || '');
      return { ...prev!, overworld: { cells: cells.slice(), palette: prev?.overworld?.palette || '' } };
    });
    setDirty(true);
  };

  const heroTiles = (hero && hero.overworld && hero.overworld.cells) || [];

  return (
    <div style={{ display: 'flex', gap: 16, padding: 16, height: '100%', overflow: 'auto', background: '#f4efe4', color: '#222' }}>
      <div style={{ minWidth: 190 }}>
        <h3 style={{ margin: '0 0 4px' }}>Hero (singleton)</h3>
        <div style={{ fontSize: 12, color: '#555', marginBottom: 8 }}>
          One hero definition shared everywhere. Configure name, stats, starter deck, and overworld sprite.
        </div>
      </div>

      <div style={{ display: 'flex', gap: 24 }}>
        <div>
          <h3 style={{ margin: '0 0 4px' }}>Overworld Sprite</h3>
          <div style={{ fontSize: 12, color: '#555', marginBottom: 8 }}>
            One transparent-background sprite shared by every world.
          </div>
          {(hero?.overworld?.cells || []).map((cell, i) => (
            <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 6 }}>
              <img src={tileUrl(cell)} alt={cell} width={32} height={32} style={{ imageRendering: 'pixelated', background: '#888' }} />
              <label>Frame {i}:{' '}
                <select value={cell} onChange={(e) => setCell(i, e.target.value)}>
                  {owTiles.map((t) => <option key={t.id} value={t.id}>{t.label}</option>)}
                </select>
              </label>
            </div>
          ))}
          <div style={{ marginTop: 6, fontSize: 13 }}>
            Frames: <button onClick={() => setFrames(((hero?.overworld?.cells?.length) || 1) - 1)}>-</button>{' '}
            {(hero?.overworld?.cells?.length) || 0}{' '}
            <button onClick={() => setFrames(((hero?.overworld?.cells?.length) || 0) + 1)}>+</button>{' '}
            (max 2)
          </div>
          <div style={{ marginTop: 6, fontSize: 13 }}>
            <label>Palette (OBJ ramp name, e.g. sprites_again):{' '}
              <input type="text" style={{ width: 130 }} placeholder="sprites_again"
                value={hero?.overworld?.palette || ''}
                onChange={(e) => {
                  const palette = e.target.value.trim();
                  setHero((prev) => ({ ...prev!, overworld: { ...prev!.overworld!, palette } }));
                  setDirty(true);
                }} />
            </label>
          </div>
        </div>

        <div>
          <h3 style={{ margin: '0 0 4px' }}>Name & Stats</h3>
          <div style={{ marginBottom: 6 }}>
            <label>Name: <input value={hero?.name || ''} onChange={(e) => {
              setHero((prev) => ({ ...prev!, name: e.target.value }));
              setDirty(true);
            }} style={{ width: 200 }} /></label>
          </div>
          <div style={{ marginBottom: 6 }}>
            <label>Start HP: <input type="number" min={1} max={255} style={{ width: 60 }}
              value={hero?.start_hp || 10}
              onChange={(e) => { setHero((prev) => ({ ...prev!, start_hp: Math.max(1, Math.min(255, parseInt(e.target.value) || 1)) })); setDirty(true); }} /></label>
            {'  '}
            <label>Start Gold: <input type="number" min={0} max={65535} style={{ width: 80 }}
              value={hero?.start_gold || 20}
              onChange={(e) => { setHero((prev) => ({ ...prev!, start_gold: Math.max(0, Math.min(65535, parseInt(e.target.value) || 0)) })); setDirty(true); }} /></label>
          </div>

          <h3 style={{ margin: '12px 0 4px' }}>Starter Deck ({(hero?.starter_deck || []).length}/20 cards, ordered draw-pile order)</h3>
          <div style={{ fontSize: 12, color: '#555', marginBottom: 8 }}>
            Max 20 cards. Edit the JSON array directly in the textarea below.
          </div>
          <textarea
            style={{ width: '100%', height: 120, fontFamily: 'monospace', fontSize: 12 }}
            value={hero?.starter_deck?.join(',\n') || ''}
            onChange={(e) => {
              const cards = e.target.value.split(',').map(s => s.trim()).filter(Boolean);
              setHero((prev) => ({ ...prev!, starter_deck: cards }));
              setDirty(true);
            }}
            placeholder="CARD_IRON_SWORD,&#10;CARD_IRON_SWORD,&#10;CARD_WOODEN_SHIELD..."
          />
          <div style={{ fontSize: 12, color: '#555', marginTop: 4 }}>
            Valid card IDs: CARD_IRON_SWORD, CARD_WOODEN_SHIELD, CARD_FIRE_SWORD, CARD_POISON_DAGGER, etc.
          </div>
        </div>

        <div>
          <button onClick={save} disabled={!hero || !dirty}>Save hero{dirty ? ' *' : ''}</button>
          {status && <div style={{ marginTop: 8, fontSize: 13 }}>{status}</div>}
        </div>
      </div>
    </div>
  );
};