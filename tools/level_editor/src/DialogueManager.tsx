import React, { useEffect, useState } from 'react';
import {
  DialogueListItem, DialogueDraft,
  fetchDialogueList, fetchDialogue, saveDialogue,
} from './io/dialogue';

/** Dialogue text editor: speaker tag + lines for each
 *  screens/dialogue/*.json record.  Text ONLY — story flags, events, and
 *  scenarios stay LLM-authored (the completion_flag field is preserved
 *  untouched on save, never shown).
 *
 *  Ids assign by sorted filename at compile time (dialogue_compile.py);
 *  nothing references a numeric dialogue id persistently, so renaming a
 *  file only renumbers the generated defines. */

const MAX_LINES = 8;
const STAGE_SPEAKER = 11;   // g_dlg_speaker[12] incl NUL
const RENDER_WIDTH = 18;    // dialogue box text width (staging allows 20)

export const DialogueManager: React.FC<{ initialId?: string }> = ({ initialId }) => {
  const [items, setItems] = useState<DialogueListItem[]>([]);
  const [activeId, setActiveId] = useState<string>('');
  const [draft, setDraft] = useState<DialogueDraft | null>(null);
  const [raw, setRaw] = useState<any>(null);
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState('');

  const refreshList = async (select?: string) => {
    const list = await fetchDialogueList();
    setItems(list);
    setActiveId((prev) => select || prev || initialId || (list[0]?.id ?? ''));
  };

  useEffect(() => {
    refreshList().catch((e) => setStatus(`load failed: ${e.message}`));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (!activeId) { setDraft(null); setRaw(null); return; }
    fetchDialogue(activeId).then((data) => {
      setRaw(data);
      setDraft({
        id: data.id || activeId,
        speaker: data.speaker || '',
        lines: (data.lines || []).slice(),
        completion_flag: (data as any).completion_flag,
      });
      setDirty(false);
      setStatus('');
    }).catch((e) => setStatus(`load failed: ${e.message}`));
  }, [activeId]);

  const patch = (p: Partial<DialogueDraft>) => {
    setDraft((prev) => (prev ? { ...prev, ...p } : prev));
    setDirty(true);
  };

  const save = async () => {
    if (!draft) return;
    try {
      // Preserve the LLM-authored flag field verbatim; never edit it here.
      const out: any = {
        id: draft.id,
        speaker: draft.speaker,
        lines: draft.lines.filter((l, i) => l !== '' || i < draft.lines.length),
      };
      if (raw && raw.completion_flag) out.completion_flag = raw.completion_flag;
      // Drop trailing empty lines (compiler pads with "" anyway).
      while (out.lines.length > 1 && out.lines[out.lines.length - 1] === '') {
        out.lines.pop();
      }
      await saveDialogue(draft.id, out as DialogueDraft);
      setRaw(out);
      setDirty(false);
      setStatus('saved — recompile to apply');
      await refreshList(draft.id);
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  const addNew = async () => {
    const id = prompt('New dialogue id (lowercase, underscores):', 'new_dialogue');
    if (!id) return;
    const clean = id.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_') || 'new_dialogue';
    try {
      await saveDialogue(clean, { id: clean, speaker: '', lines: [''] });
      await refreshList(clean);
    } catch (e: any) {
      setStatus(`create failed: ${e.message}`);
    }
  };

  const speakerLen = draft ? draft.speaker.length : 0;
  const lineWarn = (s: string) =>
    s.length > RENDER_WIDTH ? { color: '#a60' } : undefined;

  return (
    <div style={{ display: 'flex', gap: 16, padding: 16, height: '100%', overflow: 'auto', background: '#f4efe4', color: '#222' }}>
      <div style={{ minWidth: 200 }}>
        <div style={{ fontWeight: 'bold', marginBottom: 4 }}>Dialogues</div>
        {items.map((it) => (
          <button
            key={it.id}
            onClick={() => setActiveId(it.id)}
            style={{
              display: 'block', width: '100%', textAlign: 'left', background: 'none',
              border: 'none', color: 'inherit', cursor: 'pointer', padding: 3,
              fontWeight: it.id === activeId ? 'bold' : 'normal',
            }}
          >
            {it.label}
            <div style={{ fontSize: 11, color: '#777' }}>{it.id}</div>
          </button>
        ))}
        <div style={{ marginTop: 8 }}>
          <button onClick={addNew}>+ New Dialogue</button>
        </div>
        <div style={{ fontSize: 11, color: '#555', marginTop: 8, lineHeight: 1.4 }}>
          Text only. Flags/events/scenarios are LLM-authored and are not
          editable here.
        </div>
      </div>

      {draft && (
        <div style={{ flex: 1, minWidth: 320 }}>
          <h3 style={{ margin: '0 0 8px' }}>{draft.id}</h3>

          <div className="form-group">
            <label>
              Speaker ({speakerLen}/{STAGE_SPEAKER})
            </label>
            <input
              type="text"
              value={draft.speaker}
              maxLength={STAGE_SPEAKER}
              placeholder="GUARD:"
              onChange={(e) => patch({ speaker: e.target.value })}
            />
          </div>

          <div style={{ marginTop: 10, fontWeight: 600, fontSize: 13 }}>
            Lines ({draft.lines.length}/{MAX_LINES})
          </div>
          {draft.lines.map((line, i) => (
            <div key={i} className="form-row" style={{ alignItems: 'center', marginTop: 4 }}>
              <div className="form-group" style={{ gridColumn: 'span 2' }}>
                <input
                  type="text"
                  value={line}
                  placeholder={`line ${i + 1}`}
                  onChange={(e) => {
                    const lines = draft.lines.slice();
                    lines[i] = e.target.value;
                    patch({ lines });
                  }}
                />
                <span style={{ fontSize: 11, ...lineWarn(line) }}>
                  {line.length}/{RENDER_WIDTH}
                  {line.length > RENDER_WIDTH ? ' (clipped at render)' : ''}
                </span>
              </div>
            </div>
          ))}

          <div style={{ marginTop: 6 }}>
            <button
              disabled={draft.lines.length >= MAX_LINES}
              onClick={() => patch({ lines: [...draft.lines, ''] })}
            >
              + Add line
            </button>{' '}
            <button
              disabled={draft.lines.length <= 1}
              onClick={() => patch({ lines: draft.lines.slice(0, -1) })}
            >
              - Remove line
            </button>
          </div>

          <div style={{ marginTop: 14 }}>
            <button onClick={save} disabled={!dirty}>Save</button>
          </div>
          {status && <div style={{ marginTop: 8, fontSize: 12, color: '#555' }}>{status}</div>}
        </div>
      )}
    </div>
  );
};
