import React, { useEffect, useMemo, useState } from 'react';
import { PaletteData, savePalette } from './io/palettes';
import { RampGuide } from './io/romRecolor';

interface RampEditorProps {
  rampName: string;
  guide: RampGuide;
  data: PaletteData;
  onPreview: (preview: { ramp: string; colors: string[] } | null) => void;
  onClose: () => void;
  onSaved: (summary: string) => void;
}

const HEX_RE = /^#[0-9a-fA-F]{6}$/;

/** Author a ramp: native color wells + hex fields edit the shared color
 *  values, per-position dropdowns repoint to another existing color.
 *  Dragging previews live through the exact pipeline mapping (no writes);
 *  Save rewrites palette.txt and refreshes the full manifest server-side.
 *  Ramp renames/additions and new colors stay out (they break slots/tags).
 */
export const RampEditor: React.FC<RampEditorProps> = ({
  rampName, guide, data, onPreview, onClose, onSaved,
}) => {
  const entry = guide.ramps[rampName];
  const origRefs = useMemo(() => entry?.refs || [], [entry]);
  const origColors = useMemo(() => entry?.colors || [], [entry]);

  const [hexes, setHexes] = useState<string[]>(origColors);
  const [refs, setRefs] = useState<string[]>(origRefs);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');

  // Color catalog: every SECTION/name referenced by any ramp, with its
  // current value (a shared definition has exactly one value).
  const catalog = useMemo(() => {
    const m = new Map<string, string>();
    for (const e of Object.values(guide.ramps)) {
      e.refs.forEach((ref, i) => {
        if (!m.has(ref)) m.set(ref, e.colors[i]);
      });
    }
    return m;
  }, [guide]);
  const catalogRefs = useMemo(() => [...catalog.keys()].sort(), [catalog]);

  // Other ramps sharing a color (cross-tileset impact included: any ramp
  // slotted anywhere that references it changes too).
  const usersOf = (ref: string): string[] =>
    Object.entries(guide.ramps)
      .filter(([n, e]) => n !== rampName && e.refs.includes(ref))
      .map(([n]) => n);

  // Tiles/enemies in the current view whose ramp is R (via slot -> name).
  const bgNameOf = (slot: number): string | null =>
    data.bg.find((r) => r.index === slot)?.name || null;
  const objNameOf = (slot: number): string | null =>
    data.obj.find((r) => r.index === slot)?.name || null;
  const tilesUsing = (ramp: string): string[] =>
    data.tiles.filter((t) => bgNameOf(t.palette) === ramp).map((t) => t.id);
  const enemiesUsing = (ramp: string): string[] =>
    data.enemies.filter((e) => objNameOf(e.palette) === ramp).map((e) => e.id);

  // Live drag preview (no writes): candidate colors flow into the tile
  // strips while the panel is open.
  const previewKey = hexes.join(',');
  useEffect(() => {
    if (hexes.length === 4 && hexes.every((h) => HEX_RE.test(h))) {
      onPreview({ ramp: rampName, colors: hexes.map((h) => h.toLowerCase()) });
    }
    return () => onPreview(null);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [previewKey, rampName]);

  if (!entry) return <div style={{ color: '#a00' }}>unknown ramp '{rampName}'</div>;

  const allValid = hexes.length === 4 && hexes.every((h) => HEX_RE.test(h));
  const refDirty = refs.some((r, i) => r !== origRefs[i]);
  const hexDirty = refs.some((r, i) => {
    const base = catalog.get(r);
    return base !== undefined && hexes[i].toLowerCase() !== base.toLowerCase();
  });
  const dirty = allValid && (refDirty || hexDirty);

  const adoptRef = (i: number, ref: string) => {
    setRefs((prev) => prev.map((r, j) => (j === i ? ref : r)));
    const v = catalog.get(ref);
    if (v) setHexes((prev) => prev.map((h, j) => (j === i ? v : h)));
    setError('');
  };

  const doSave = async () => {
    setError('');
    // Group hex edits by color ref: two positions sharing one ref must
    // agree (one definition, one value).
    const byRef = new Map<string, { positions: number[]; hex: string }>();
    for (let i = 0; i < 4; i++) {
      const hex = hexes[i].toLowerCase();
      const base = catalog.get(refs[i]) || '';
      if (hex === base.toLowerCase()) continue;
      const g = byRef.get(refs[i]);
      if (g && g.hex !== hex) {
        setError(`positions ${g.positions[0] + 1} and ${i + 1} share color ${refs[i]} — set the same value`);
        return;
      }
      if (g) g.positions.push(i);
      else byRef.set(refs[i], { positions: [i], hex });
    }
    const colorEdits = [...byRef.entries()].map(([ref, g]) => {
      const [section, name] = ref.split('/');
      return { section, name, hex: g.hex };
    });
    const rampRepoints = refs
      .map((ref, i) => ({ ramp: rampName, position: i, ref }))
      .filter((_, i) => refs[i] !== origRefs[i]);
    if (colorEdits.length === 0 && rampRepoints.length === 0) {
      onClose();
      return;
    }
    setSaving(true);
    try {
      await savePalette(colorEdits, rampRepoints);
      const bits: string[] = [];
      if (colorEdits.length > 0) {
        bits.push(`colors ${colorEdits.map((e) => `${e.section}/${e.name}=${e.hex}`).join(', ')}`);
      }
      if (rampRepoints.length > 0) {
        bits.push(`repoints ${rampRepoints.map((r) => `${r.position + 1}->${r.ref}`).join(', ')}`);
      }
      onSaved(`ramp ${rampName}: ${bits.join('; ')}. Recompile ROM to rebuild art.`);
    } catch (e: any) {
      setError(`save failed: ${e.message}`);
    } finally {
      setSaving(false);
    }
  };

  const doRevert = () => {
    setHexes(origColors);
    setRefs(origRefs);
    setError('');
  };

  const selfTiles = tilesUsing(rampName);
  const selfEnemies = enemiesUsing(rampName);

  return (
    <div style={{ marginTop: 10, padding: 10, border: '2px solid #1a7', background: '#fff' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <b>Edit ramp {rampName}</b>
        <span style={{ fontSize: 11, color: '#555' }}>
          slots: {(entry.slots || []).join(', ') || 'unslotted'}
        </span>
        <span style={{ flex: 1 }} />
        <button className="btn btn-sm" onClick={onClose} disabled={saving}>close</button>
      </div>
      <div style={{ fontSize: 11, color: '#555', marginTop: 4, lineHeight: 1.5 }}>
        Shade wells edit the <i>shared</i> color value (every ramp below using
        that color changes). Repoint swaps this position to another existing
        color (ramp-local). Dragging previews live — nothing is written until Save.
        {selfTiles.length > 0 && <span> Tiles here: {selfTiles.length}.</span>}
        {selfEnemies.length > 0 && <span> Enemies here: {selfEnemies.length}.</span>}
      </div>

      {[0, 1, 2, 3].map((i) => {
        const ref = refs[i];
        const users = usersOf(ref);
        const ok = HEX_RE.test(hexes[i] || '');
        return (
          <div key={i} style={{ display: 'flex', gap: 8, alignItems: 'center', marginTop: 8, flexWrap: 'wrap' }}>
            <b style={{ width: 52 }}>shade {i}</b>
            <input
              type="color"
              value={ok ? hexes[i].toLowerCase() : (catalog.get(ref) || '#000000')}
              onChange={(e) => {
                const v = e.target.value;
                setHexes((prev) => prev.map((h, j) => (j === i ? v : h)));
                setError('');
              }}
              title={`Pick color for ${ref}`}
              style={{ width: 40, height: 28, padding: 0, cursor: 'pointer' }}
            />
            <input
              value={hexes[i] || ''}
              onChange={(e) => {
                setHexes((prev) => prev.map((h, j) => (j === i ? e.target.value : h)));
                setError('');
              }}
              spellCheck={false}
              placeholder="#rrggbb"
              title="Exact hex value"
              style={{
                width: 76, fontFamily: 'monospace',
                border: ok ? '1px solid #999' : '2px solid #a00',
              }}
            />
            <select
              value={ref}
              onChange={(e) => adoptRef(i, e.target.value)}
              title="Repoint this position to another existing color"
              style={{ maxWidth: 220 }}
            >
              {catalogRefs.map((c) => (
                <option key={c} value={c}>{c}</option>
              ))}
            </select>
            <span style={{ fontSize: 11, color: '#555' }} title={`Other ramps using ${ref}`}>
              {users.length > 0 ? `also: ${users.join(', ')}` : 'unique to this ramp'}
            </span>
          </div>
        );
      })}

      {error && <div style={{ marginTop: 8, fontSize: 12, color: '#a00' }}>{error}</div>}

      <div style={{ display: 'flex', gap: 8, marginTop: 10 }}>
        <button className="btn btn-sm btn-primary" onClick={doSave} disabled={!dirty || saving}>
          {saving ? 'saving + refreshing manifest…' : 'Save to palette.txt'}
        </button>
        <button className="btn btn-sm" onClick={doRevert} disabled={saving}>revert</button>
      </div>
    </div>
  );
};
