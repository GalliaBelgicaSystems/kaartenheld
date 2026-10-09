/** Dialogue + tutorial disk I/O via the vite dev API.
 *  Mirrors io/combatArt.ts: all functions throw on transport failure so
 *  callers can fall back to bundled behavior.
 *
 *  Dialogue ids are assigned by sorted filename (dialogue_compile.py);
 *  the id field equals the filename stem.  completion_flag is deliberately
 *  NOT exposed by the editor — flags/events/scenarios stay LLM-authored.
 */

export interface DialogueListItem {
  id: string;
  /** First line, used as the list label. */
  label: string;
}

export interface DialogueDraft {
  id: string;
  speaker: string;
  lines: string[];
  /** LLM-authored only; preserved by the editor, never edited. */
  completion_flag?: string;
}

export interface TutorialSlide {
  lines: string[];
}

export interface TutorialDraft {
  slides: TutorialSlide[];
}

export async function fetchDialogueList(): Promise<DialogueListItem[]> {
  const res = await fetch('/api/dialogues');
  if (!res.ok) throw new Error(`dialogues list returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'dialogues list failed');
  return body.items || [];
}

export async function fetchDialogue(id: string): Promise<DialogueDraft> {
  const res = await fetch(`/api/dialogue?id=${encodeURIComponent(id)}`);
  if (!res.ok) throw new Error(`dialogue ${id} returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || `dialogue ${id} failed`);
  return body.data;
}

export async function saveDialogue(id: string, data: DialogueDraft): Promise<void> {
  const res = await fetch('/api/save-dialogue', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id, data }),
  });
  if (!res.ok) throw new Error(`save dialogue returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save dialogue failed');
}

export async function fetchTutorial(): Promise<TutorialDraft> {
  const res = await fetch('/api/tutorial');
  if (!res.ok) throw new Error(`tutorial returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'tutorial failed');
  return body.data;
}

export async function saveTutorial(data: TutorialDraft): Promise<void> {
  const res = await fetch('/api/save-tutorial', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ data }),
  });
  if (!res.ok) throw new Error(`save tutorial returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save tutorial failed');
}
