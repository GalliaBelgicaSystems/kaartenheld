import React, { useEffect, useState } from 'react';
import { BUILTIN_TILESETS, TileDefinition, TilesetDefinition } from './model/Tileset';
import { PaletteData, fetchPalettes } from './io/palettes';
import { recoloredDataUrl as romRecoloredDataUrl } from './io/romRecolor';

export type Fidelity = 'raw' | 'rom';

interface TilesetPaletteProps {
  tilesetId: string;
  selectedTileId: string;
  onSelectTileset: (tilesetId: string) => void;
  onSelectTile: (tileId: string) => void;
  categoryFilter?: 'enemy' | 'npc' | 'terrain' | 'ui' | 'object' | 'all';
  fidelity?: Fidelity;
}

/** One picker thumbnail: raw artist PNG, or the pipeline-exact ROM recolor
 *  (png2gb.nearest_shade through the tile's assigned BG ramp) when
 *  fidelity is 'rom', so mistakes are visible before Compile ROM. */
const Thumb: React.FC<{ src: string; colors: string[] | null; fidelity: Fidelity; label: string }> =
  ({ src, colors, fidelity, label }) => {
    const [romSrc, setRomSrc] = useState<string | null>(null);
    useEffect(() => {
      if (fidelity !== 'rom' || !colors) { setRomSrc(null); return; }
      let alive = true;
      const img = new Image();
      img.onload = () => { if (alive) setRomSrc(romRecoloredDataUrl(img, colors, false)); };
      img.src = src;
      return () => { alive = false; };
    }, [src, fidelity, colors?.join(',')]);
    return (
      <img
        src={romSrc || src}
        alt={label}
        width={32}
        height={32}
        style={{
          imageRendering: 'pixelated',
          display: 'block',
        }}
        onError={(e: React.SyntheticEvent<HTMLImageElement>) => {
          const img = e.currentTarget;
          img.style.display = 'none';
          const fallback = img.nextElementSibling as HTMLElement | null;
          if (fallback) fallback.style.display = 'flex';
        }}
      />
    );
  };

export const TilesetPalette: React.FC<TilesetPaletteProps> = ({
  tilesetId,
  selectedTileId,
  onSelectTileset,
  onSelectTile,
  categoryFilter = 'all',
  fidelity = 'raw',
}) => {
  const currentTileset: TilesetDefinition = BUILTIN_TILESETS[tilesetId] || BUILTIN_TILESETS.forest;
  if (!BUILTIN_TILESETS[tilesetId]) {
    console.warn(`[TilesetPalette] unknown tileset '${tilesetId}', falling back to forest`);
  }

  // Assigned BG ramp per tile (slot -> ramp colors) from the manifest via
  // the dev API; offline/missing = raw thumbnails (no wrong colors shown).
  const [palData, setPalData] = useState<PaletteData | null>(null);
  useEffect(() => {
    setPalData(null);
    fetchPalettes(tilesetId).then(setPalData).catch(() => setPalData(null));
  }, [tilesetId]);
  const slotOf = new Map<string, number>();
  (palData?.tiles || []).forEach((t) => slotOf.set(t.id, t.palette));
  const rampOf = (tileId: string): { slot: number; colors: string[]; name: string } | null => {
    if (!palData) return null;
    const slot = slotOf.get(tileId);
    if (slot === undefined) return null;
    const ramp = palData.bg.find((r) => r.index === slot);
    if (!ramp) return null;
    return { slot, colors: ramp.colors, name: ramp.name };
  };

  // Ramp filter: show only tiles assigned to one ramp (or all).  Catches
  // mis-tagged tiles (e.g. a floor rect on a solid prop's ramp) early.
  const [rampFilter, setRampFilter] = useState<string>('all');

  const filteredTiles = currentTileset.tiles.filter((tile: TileDefinition) => {
    if (categoryFilter !== 'all' && tile.category !== categoryFilter) return false;
    if (rampFilter !== 'all' && slotOf.size > 0) {
      const slot = slotOf.get(tile.id);
      if (slot === undefined || String(slot) !== rampFilter) return false;
    }
    return true;
  });

  return (
    <div className="panel tileset-panel">
      <div className="panel-header">
        <h3>Tileset Palette</h3>
        <select
          className="select-input"
          value={tilesetId}
          onChange={(e) => onSelectTileset(e.target.value)}
        >
          {Object.values(BUILTIN_TILESETS).map((ts) => (
            <option key={ts.id} value={ts.id}>
              {ts.label}
            </option>
          ))}
        </select>
      </div>
      <div className="panel-header" style={{ marginTop: 8 }}>
        <h3 title="Show only tiles assigned to one BG ramp (ROM slot)">Ramp</h3>
        <select
          className="select-input"
          value={rampFilter}
          onChange={(e) => setRampFilter(e.target.value)}
          title="Filter tiles by assigned BG ramp"
        >
          <option value="all">all ramps</option>
          {(palData?.bg || []).map((r) => (
            <option key={r.index} value={String(r.index)}>
              {r.index} {r.name}
            </option>
          ))}
        </select>
      </div>
      {fidelity === 'rom' && !palData && (
        <div style={{ fontSize: 11, color: '#a00', padding: '4px 8px' }}>
          ROM preview offline — showing raw art.
        </div>
      )}

      <div className="panel-body">
        <div className="tiles-grid">
          {filteredTiles.map((tile: TileDefinition) => {
            const isSelected = selectedTileId === tile.id;
            const ramp = rampOf(tile.id);
            return (
              <button
                key={tile.id}
                className={`tile-btn ${isSelected ? 'selected' : ''}`}
                onClick={() => onSelectTile(tile.id)}
                title={`${tile.label} (${tile.gb_constant}) - ${tile.walkable ? 'Walkable' : 'Solid Wall'}${ramp ? ` - ramp ${ramp.slot} ${ramp.name}` : ''}`}
              >
                <div
                  className="tile-swatch"
                  style={{
                    borderColor: isSelected ? '#ffffff' : '#000000',
                  }}
                >
                  <Thumb
                    src={tile.image_url}
                    colors={fidelity === 'rom' ? (ramp?.colors || null) : null}
                    fidelity={fidelity}
                    label={tile.label}
                  />
                  <div
                    className="tile-ascii-fallback"
                    style={{
                      display: 'none',
                      width: '32px',
                      height: '32px',
                      backgroundColor: tile.color,
                      alignItems: 'center',
                      justifyContent: 'center',
                      fontSize: '24px',
                    }}
                  >
                    {tile.ascii || '.'}
                  </div>
                </div>
                <div className="tile-info">
                  <span className="tile-name">{tile.label}</span>
                  <span className={`tile-tag ${tile.walkable ? 'walkable' : 'blocked'}`}>
                    {tile.walkable ? 'Walk' : 'Solid'}
                  </span>
                  {ramp && (
                    <span className="tile-tag" title={`BG ramp ${ramp.slot} ${ramp.name}`}>
                      R{ramp.slot}
                    </span>
                  )}
                </div>
              </button>
            );
          })}
        </div>
      </div>
    </div>
  );
};