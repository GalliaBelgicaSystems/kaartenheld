/** Two-way tunnel disk I/O via the vite dev API.
 *
 *  A tunnel is two point exits sharing a `tunnel` id (one in each of two
 *  levels, mutual targets, each landing on the other's gate).  The ROM
 *  format is unchanged — the id lives only in JSON + tooling, which keeps
 *  the pair in sync on every save.
 */

export async function unlinkTunnel(tunnel: string): Promise<{ cleared: number; exits: number }> {
  const res = await fetch('/api/tunnel-unlink', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ tunnel }),
  });
  const body = await res.json();
  if (!res.ok || !body.success) throw new Error(body.error || `status ${res.status}`);
  return { cleared: body.cleared ?? 0, exits: body.exits ?? 0 };
}
