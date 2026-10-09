import React, { useEffect, useState } from 'react';
import { LevelExit } from './model/Level';
import { ExitStatus, fetchExitStatus, connectLevels } from './io/exits';
import { unlinkTunnel } from './io/tunnels';

/** Auto-exit helper for the Exits tab: shows, for each exit in the level,
 *  whether the target scene has a return exit, and lets the author preview
 *  + create the missing one.  Placement is computed on the server (one
 *  source of truth) and written to the target level; the from-level's exit
 *  is upserted too, so the pair can never drift.
 *
 *  Tunnel pairing rides on the same panel: a 🔗 tunnel is two mouths
 *  sharing one id (mutual targets, landings on each other's gate), kept
 *  in sync on every save.  One-way pairs stay supported — tunnels are
 *  opt-in per exit, never forced.
 *
 *  This is what keeps every level reachable at scale — the walkthrough
 *  sweep fails on an orphan, and this fixes it in one click. */
export const ExitConnector: React.FC<{
  levelId: string;
  exits: LevelExit[];
  onChanged?: () => void;
  /** Server rewrites the from-level file (upsert + tunnel id); apply the
   *  returned exits so editor state cannot go stale and overwrite them. */
  onExitsSynced?: (exits: LevelExit[]) => void;
}> = ({ levelId, exits, onChanged, onExitsSynced }) => {
  const [items, setItems] = useState<ExitStatus[]>([]);
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');

  const refresh = () => {
    setBusy(true);
    fetchExitStatus(levelId, exits)
      .then(setItems)
      .catch((e) => setStatus(`check failed: ${e.message}`))
      .finally(() => setBusy(false));
  };

  useEffect(() => {
    if (open) refresh();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open, levelId, exits.length]);

  const missing = items.filter((it) => !it.has_return && it.proposal);

  const stripLocal = (tunnel: string): LevelExit[] =>
    exits.map((e) => {
      if (e.tunnel !== tunnel) return e;
      const { tunnel: _dropped, ...rest } = e;
      return rest as LevelExit;
    });

  const create = async (it: ExitStatus, asTunnel: boolean) => {
    setBusy(true);
    setStatus('');
    try {
      const res = await connectLevels(levelId, exits[it.index], { asTunnel });
      if (res.from_exits.length > 0) onExitsSynced?.(res.from_exits);
      setStatus(res.tunnel
        ? `🔗 tunnel ${res.tunnel} paired with ${it.target}: gate (${res.to_exit.x},${res.to_exit.y})`
        : res.created
          ? `created return in ${it.target}: gate (${res.to_exit.x},${res.to_exit.y}) ${res.to_exit.direction}`
          : `return already existed in ${it.target}`);
      refresh();
      onChanged?.();
    } catch (e: any) {
      setStatus(`create failed: ${e.message}`);
    } finally {
      setBusy(false);
    }
  };

  const unlink = async (it: ExitStatus) => {
    if (!it.tunnel) return;
    if (!confirm(
      `Unlink tunnel '${it.tunnel}'?\n\nBoth mouths stay as independent one-way exits.`
    )) return;
    setBusy(true);
    setStatus('');
    try {
      await unlinkTunnel(it.tunnel);
      onExitsSynced?.(stripLocal(it.tunnel));
      setStatus(`unlinked '${it.tunnel}' — both mouths are one-way exits now`);
      refresh();
      onChanged?.();
    } catch (e: any) {
      setStatus(`unlink failed: ${e.message}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div style={{ marginTop: 10, borderTop: '1px solid #555', paddingTop: 8 }}>
      <div className="section-header-row">
        <h5 style={{ margin: 0 }}>🔁 Return exits &amp; tunnels</h5>
        <button className="btn btn-sm" onClick={() => setOpen((o) => !o)}>
          {open ? 'Hide' : 'Check'}
        </button>
      </div>
      {open && (
        <div style={{ fontSize: 12, marginTop: 6 }}>
          {busy && <div style={{ opacity: 0.7 }}>Checking…</div>}
          {!busy && items.length === 0 && <div style={{ opacity: 0.7 }}>No exits yet.</div>}
          {items.map((it) => (
            <div key={it.index} style={{ display: 'flex', gap: 8, alignItems: 'center', marginBottom: 4, flexWrap: 'wrap' }}>
              <span style={{ minWidth: 28 }}>#{it.index + 1}</span>
              <span style={{ minWidth: 110 }}>→ <strong>{it.target}</strong></span>
              {it.tunnel && (
                it.tunnel_ok
                  ? <span style={{ color: '#16a085' }}>🔗 {it.tunnel} ✓ paired</span>
                  : <span style={{ color: '#a60' }}>🔗 {it.tunnel} ⚠ {it.tunnel_error || 'broken'}</span>
              )}
              {it.error ? (
                <span style={{ color: '#a60' }}>⚠ {it.error}</span>
              ) : it.tunnel ? (
                <button className="btn btn-sm" disabled={busy} onClick={() => unlink(it)}>
                  Unlink
                </button>
              ) : it.has_return ? (
                <>
                  <span style={{ color: '#393' }}>✓ return exists</span>
                  <button className="btn btn-sm" disabled={busy} onClick={() => create(it, true)}>
                    Make tunnel
                  </button>
                </>
              ) : (
                <>
                  <span style={{ color: '#a60' }}>
                    ⚠ none — would add gate ({it.proposal!.x},{it.proposal!.y}) {it.proposal!.direction}
                    , spawn ({it.proposal!.target_x},{it.proposal!.target_y})
                  </span>
                  <button className="btn btn-sm" disabled={busy} onClick={() => create(it, false)}>
                    Create
                  </button>
                  <button className="btn btn-sm btn-primary" disabled={busy} onClick={() => create(it, true)}>
                    Create tunnel
                  </button>
                </>
              )}
            </div>
          ))}
          {missing.length > 1 && (
            <div style={{ marginTop: 6 }}>
              <button
                className="btn btn-sm"
                disabled={busy}
                onClick={async () => { for (const it of missing) await create(it, false); }}
              >
                Create all missing ({missing.length})
              </button>
            </div>
          )}
          {status && <div style={{ marginTop: 6, color: '#555' }}>{status}</div>}
          <div style={{ marginTop: 6, color: '#777', lineHeight: 1.4 }}>
            Tunnels keep both mouths in sync on every save (target + landing).
            Deleting a tunnel mouth removes its partner on save; unlinking keeps
            both as one-way exits. Recompile to apply.
          </div>
        </div>
      )}
    </div>
  );
};
