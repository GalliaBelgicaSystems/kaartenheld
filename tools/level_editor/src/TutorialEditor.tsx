import React, { useEffect, useState } from 'react';
import { TutorialDraft, fetchTutorial, saveTutorial } from './io/dialogue';

/** Tutorial slides editor (screens/tutorial.json): the title-menu
 *  TUTORIAL entries.  Text only — fixed 4-row geometry, 18 cols.  The
 *  slide count (generated) follows the list length.  Self-contained:
 *  its own fetch/save, so it never touches the title-layout draft. */
const MAX_ROWS = 4;
const RENDER_WIDTH = 18;
const STAGE_COLS = 20;

export const TutorialEditor: React.FC = () => {
  const [draft, setDraft] = useState<TutorialDraft | null>(null);
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState('');
  const [open, setOpen] = useState(false);

  useEffect(() => {
    fetchTutorial()
      .then((data) => setDraft({ slides: (data.slides || []).map((s) => ({ lines: (s.lines || []).slice() })) }))
      .catch((e) => setStatus(`load failed: ${e.message}`));
  }, []);

  if (!draft) {
    return (
      <div className="form-group" style={{ marginTop: 12 }}>
        <label style={{ fontWeight: 600 }}>📖 Tutorial Slides</label>
        <div style={{ fontSize: 12, color: '#a66' }}>{status || 'loading...'}</div>
      </div>
    );
  }

  const patch = (slides: TutorialDraft['slides']) => {
    setDraft({ slides });
    setDirty(true);
  };

  const save = async () => {
    // Drop trailing blank rows; the compiler pads them back.
    const cleaned = draft.slides.map((s) => {
      const lines = s.lines.slice();
      while (lines.length > 1 && lines[lines.length - 1] === '') lines.pop();
      return { lines };
    });
    try {
      await saveTutorial({ slides: cleaned });
      setDirty(false);
      setStatus('saved — recompile to apply');
    } catch (e: any) {
      setStatus(`save failed: ${e.message}`);
    }
  };

  return (
    <div style={{ marginTop: 12, borderTop: '1px solid #555', paddingTop: 8 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <label style={{ fontWeight: 600, cursor: 'pointer' }} onClick={() => setOpen(!open)}>
          📖 Tutorial Slides ({draft.slides.length}) {open ? '▾' : '▸'}
        </label>
        {open && <button onClick={save} disabled={!dirty}>Save</button>}
      </div>
      {open && (
        <>
          <p className="hint-text">
            Title-menu TUTORIAL entries. Text only; flags/events stay LLM-authored.
          </p>
          {draft.slides.map((slide, si) => (
            <div key={si} style={{ border: '1px solid #555', borderRadius: 4, padding: 6, marginBottom: 6 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span style={{ fontSize: 12, fontWeight: 600 }}>Slide {si + 1}</span>
                <button
                  onClick={() => patch(draft.slides.filter((_, i) => i !== si))}
                  disabled={draft.slides.length <= 1}
                  title="Remove slide"
                >
                  ✕
                </button>
              </div>
              {slide.lines.map((line, li) => (
                <div key={li} style={{ marginTop: 4 }}>
                  <input
                    type="text"
                    style={{ width: '100%' }}
                    value={line}
                    placeholder={`line ${li + 1}`}
                    onChange={(e) => {
                      const slides = draft.slides.map((s, i) =>
                        i === si ? { lines: s.lines.map((l, j) => (j === li ? e.target.value : l)) } : s
                      );
                      patch(slides);
                    }}
                  />
                  <span style={{ fontSize: 11, color: line.length > RENDER_WIDTH ? '#a60' : '#777' }}>
                    {line.length}/{RENDER_WIDTH}
                    {line.length > RENDER_WIDTH ? ' (clipped)' : ''}
                  </span>
                </div>
              ))}
              <button
                style={{ marginTop: 4 }}
                disabled={slide.lines.length >= MAX_ROWS}
                onClick={() => patch(draft.slides.map((s, i) => (i === si ? { lines: [...s.lines, ''] } : s)))}
              >
                + line
              </button>{' '}
              <button
                disabled={slide.lines.length <= 1}
                onClick={() => patch(draft.slides.map((s, i) => (i === si ? { lines: s.lines.slice(0, -1) } : s)))}
              >
                - line
              </button>
            </div>
          ))}
          <div style={{ display: 'flex', gap: 6 }}>
            <button onClick={() => patch([...draft.slides, { lines: [''] }])}>+ Add slide</button>
          </div>
          {status && <div style={{ marginTop: 6, fontSize: 12, color: '#a66' }}>{status}</div>}
        </>
      )}
    </div>
  );
};
