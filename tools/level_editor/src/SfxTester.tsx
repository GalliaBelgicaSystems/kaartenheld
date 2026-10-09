import React, { useRef, useState, useEffect } from 'react';

// Sound registry editor (screens/sfx.json).  The 7 SFX ids are fixed by
// the SFX_* enum in src/audio/audio.h; each one maps to an authored .uge
// (assets/sfx/* or assets/music/*).  tools/transcribe_sfx.py reads this
// same registry to emit the step tables (make sfx), so saving here then
// Compiling ROM rebuilds the tables and the previews below.
const SFX_LIST: { id: string; blurb: string }[] = [
  { id: 'CURSOR', blurb: 'menu navigation blip' },
  { id: 'CONFIRM', blurb: 'selection confirm' },
  { id: 'SELECT', blurb: 'battle/card select' },
  { id: 'BACK', blurb: 'cancel / go back' },
  { id: 'ATTACK', blurb: 'player slash (noise)' },
  { id: 'HIT', blurb: 'enemy strike (noise)' },
  { id: 'BLOCK', blurb: 'successful defend' },
];

type UgeFile = { path: string; name: string; dir: string };

export const SfxTesterModal: React.FC<{ onClose: () => void }> = ({ onClose }) => {
  const audioRef = useRef<HTMLAudioElement | null>(null);
  const [files, setFiles] = useState<UgeFile[]>([]);
  const [mapping, setMapping] = useState<Record<string, string>>({});
  const [playingId, setPlayingId] = useState<string | null>(null);
  const [status, setStatus] = useState<string>('');
  const [dirty, setDirty] = useState<boolean>(false);
  const [loading, setLoading] = useState<boolean>(true);

  useEffect(() => {
    const audio = audioRef.current;
    let cancelled = false;
    Promise.all([
      fetch('/api/uge-files').then((r) => r.json()),
      fetch('/api/sfx').then((r) => r.json()),
    ])
      .then(([f, s]) => {
        if (cancelled) return;
        setFiles(Array.isArray(f.files) ? f.files : []);
        setMapping((s && s.data) || {});
        setLoading(false);
      })
      .catch((e) => {
        if (cancelled) return;
        setStatus(`Load failed: ${e.message}`);
        setLoading(false);
      });
    return () => {
      cancelled = true;
      if (audio) audio.pause();
    };
  }, []);

  const play = (id: string) => {
    const audio = audioRef.current;
    if (!audio) return;
    audio.src = `/audio/sfx/${id.toLowerCase()}.wav`;
    audio.play().then(() => setPlayingId(id)).catch(() => setPlayingId(null));
  };

  const set = (id: string, value: string) => {
    setMapping((m) => ({ ...m, [id]: value }));
    setDirty(true);
  };

  const save = () => {
    setStatus('Saving…');
    fetch('/api/save-sfx', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ data: mapping }),
    })
      .then((r) => r.json())
      .then((r) => {
        if (!r.success) throw new Error(r.error || 'save failed');
        setDirty(false);
        setStatus('Saved. Compile ROM to rebuild the ROM tables + previews.');
      })
      .catch((e) => setStatus(`Save failed: ${e.message}`));
  };

  // Group files by folder so the dropdown reads assets/sfx vs assets/music.
  const byDir: Record<string, UgeFile[]> = {};
  for (const f of files) (byDir[f.dir] = byDir[f.dir] || []).push(f);

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h3>🔊 Sound Editor</h3>
          <button className="modal-close-btn" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="modal-body">
          <div style={{ marginBottom: '12px', fontSize: '13px', opacity: 0.85 }}>
            Map each fixed SFX id to an authored <code>.uge</code>. Preview plays the
            rendered WAV (<code>make sfx-preview</code>); the ROM synth mix stays
            authoritative.
          </div>
          {loading ? (
            <div style={{ opacity: 0.7 }}>Loading…</div>
          ) : (
            SFX_LIST.map((s) => (
              <div
                key={s.id}
                style={{ display: 'flex', gap: '10px', alignItems: 'center', marginBottom: '8px' }}
              >
                <button className="btn btn-sm" onClick={() => play(s.id)} title={`Play ${s.id}`}>
                  {playingId === s.id ? '🔊 Playing…' : '▶ Play'}
                </button>
                <code style={{ minWidth: '84px' }}>{s.id}</code>
                <select
                  className="input-select"
                  style={{ flex: 1, minWidth: '220px' }}
                  value={mapping[s.id] || ''}
                  onChange={(e) => set(s.id, e.target.value)}
                  title={s.blurb}
                >
                  <option value="">(unassigned)</option>
                  {mapping[s.id] && !files.some((f) => f.path === mapping[s.id]) && (
                    <option value={mapping[s.id]}>{mapping[s.id]} (missing)</option>
                  )}
                  {Object.keys(byDir).map((d) => (
                    <optgroup key={d} label={d}>
                      {byDir[d].map((f) => (
                        <option key={f.path} value={f.path}>
                          {f.name}
                        </option>
                      ))}
                    </optgroup>
                  ))}
                </select>
                <span style={{ fontSize: '12px', opacity: 0.7, minWidth: '150px' }}>{s.blurb}</span>
              </div>
            ))
          )}
          {status && (
            <div style={{ marginTop: '10px', fontSize: '13px', opacity: 0.9 }}>{status}</div>
          )}
          <audio
            ref={audioRef}
            preload="none"
            onEnded={() => setPlayingId(null)}
            onPause={() => setPlayingId(null)}
          />
        </div>
        <div className="modal-footer">
          <button className="btn" onClick={onClose}>
            Close
          </button>
          <button className="btn btn-primary" onClick={save} disabled={!dirty || loading}>
            Save
          </button>
        </div>
      </div>
    </div>
  );
};
