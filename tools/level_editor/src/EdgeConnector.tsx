import React, { useEffect, useState } from 'react';
import { EditorLevel, normalizeNeighbors } from './model/Level';
import { NeighborLinkStatus, fetchNeighborCheck } from './io/neighbors';

/** Inline whole-edge link diagnostics for the Exits tab.
 *
 *  Each linked border drops the player one cell inside the neighbor's
 *  opposite edge; if that cell is a wall the player is stuck.  The check
 *  runs the compiler's own pairing logic (collision.py) against the
 *  current, possibly unsaved, level so the mapper sees the problem before
 *  saving.  The build refuses to compile a trapping pair (compile.py). */
export const EdgeConnector: React.FC<{ level: EditorLevel }> = ({ level }) => {
  const neighbors = normalizeNeighbors(level.neighbors);
  const linkedDirs = (['north', 'south', 'east', 'west'] as const).filter(
    (d) => (neighbors[d] || '').trim(),
  );
  const key = `${level.id}|${linkedDirs.map((d) => `${d}:${neighbors[d]}`).join(',')}`;
  const [items, setItems] = useState<NeighborLinkStatus[]>([]);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');

  const refresh = () => {
    setBusy(true);
    setStatus('');
    fetchNeighborCheck(level)
      .then(setItems)
      .catch((e) => setStatus(`check failed: ${e.message}`))
      .finally(() => setBusy(false));
  };

  useEffect(() => {
    if (linkedDirs.length === 0) {
      setItems([]);
      return;
    }
    refresh();
    // Re-runs when the level id or the link set changes; use Recheck after
    // terrain edits (the check depends on painted border/entry art).
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key]);

  if (linkedDirs.length === 0) return null;

  const bad = items.filter((it) => !it.ok && (it.errors.length > 0)).length;

  return (
    <div style={{ marginTop: 10, borderTop: '1px solid #555', paddingTop: 8 }}>
      <div className="section-header-row">
        <h5 style={{ margin: 0 }}>
          {bad > 0 ? '⛔ Edge links' : '⇄ Edge links'}
        </h5>
        <button className="btn btn-sm" onClick={refresh} disabled={busy}>
          {busy ? 'Checking…' : 'Recheck'}
        </button>
      </div>
      <div style={{ fontSize: 12, marginTop: 6 }}>
        {status && <div style={{ color: '#a60', marginBottom: 4 }}>{status}</div>}
        {items.map((it, i) => (
          <div key={i} style={{ marginBottom: 6 }}>
            <div>
              <strong style={{ textTransform: 'capitalize' }}>{it.direction}</strong>
              {' → '}
              <strong>{it.target}</strong>{' '}
              {it.ok ? (
                <span style={{ color: '#393' }}>✓ crossing lands on ground</span>
              ) : (
                <span style={{ color: '#a00' }}>⛔ trap</span>
              )}
            </div>
            {it.errors.map((e, j) => (
              <div key={j} style={{ color: '#a00', marginLeft: 12 }}>
                {e}
              </div>
            ))}
            {it.warnings.map((w, j) => (
              <div key={j} style={{ color: '#a60', marginLeft: 12 }}>
                ⚠ {w}
              </div>
            ))}
          </div>
        ))}
        <div style={{ color: '#777', lineHeight: 1.4, marginTop: 4 }}>
          The player enters one cell inside the neighbour; every crossing must
          land on walkable ground. Open the entry line in the neighbour
          (whole-edge corridor) or wall the crossing cells here.
        </div>
      </div>
    </div>
  );
};
