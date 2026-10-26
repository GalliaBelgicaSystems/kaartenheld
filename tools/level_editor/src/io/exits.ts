/** Auto-exit disk I/O via the vite dev API.
 *
 *  A hand-authored exit is one-directional; a level with no incoming exit
 *  is unreachable (the walkthrough sweep fails loudly on that).  These
 *  calls compute the reciprocal exit for a level's exits (`exit-status`)
 *  and write it into the target (`connect-levels`) with one click.
 */

import { LevelExit } from '../model/Level';

export interface ExitStatus {
  index: number;
  target: string;
  has_return: boolean;
  return_exit: LevelExit | null;
  proposal: LevelExit | null;
  error: string | null;
  /** Tunnel pairing state for this exit (null tunnel = one-way exit). */
  tunnel: string | null;
  tunnel_ok: boolean | null;
  tunnel_error: string | null;
  /** Suggested id when creating a tunnel for this exit. */
  suggested_tunnel: string | null;
}

export async function fetchExitStatus(fromId: string, exits: LevelExit[]): Promise<ExitStatus[]> {
  const res = await fetch('/api/exit-status', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ from_id: fromId, exits }),
  });
  if (!res.ok) throw new Error(`exit status returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'exit status failed');
  return body.items || [];
}

export interface ConnectResult {
  created: boolean;
  to_exit: LevelExit;
  /** Updated from-level exits (server upsert, incl. tunnel id).  Apply to
   *  editor state so the next save cannot overwrite the pairing. */
  from_exits: LevelExit[];
  from_exit: LevelExit;
  tunnel: string | null;
}

export async function connectLevels(
  fromId: string,
  exit: LevelExit,
  opts?: { asTunnel?: boolean; tunnel?: string },
): Promise<ConnectResult> {
  const res = await fetch('/api/connect-levels', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      from_id: fromId,
      exit,
      as_tunnel: !!opts?.asTunnel,
      tunnel: opts?.tunnel ?? null,
    }),
  });
  if (!res.ok) throw new Error(`connect returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'connect failed');
  return {
    created: !!body.created,
    to_exit: body.to_exit,
    from_exits: body.from_exits || [],
    from_exit: body.from_exit,
    tunnel: body.tunnel ?? null,
  };
}
