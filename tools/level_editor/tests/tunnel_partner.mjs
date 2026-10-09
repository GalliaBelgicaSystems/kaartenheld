// Adoption guard for /api/connect-levels tunnels (review-13ba2f5 #3).
//
// findTunnelPartner() below is a faithful copy of the helper in
// vite.config.ts. Cases cover the multi-exit-to-same-target hazard:
// with several untunneled returns, only the coordinate-aligned one is
// adopted; ambiguity creates a fresh partner (null) instead of absorbing
// an unrelated passage. Mouths carrying a different tunnel id are never
// adopted.
//
// Run: node tools/level_editor/tests/tunnel_partner.mjs
// Exit code 0 = agree, 1 = failure.
import assert from 'assert/strict';

// Faithful copy of findTunnelPartner() in vite.config.ts.
const findTunnelPartner = (toExits, tunnelId, fromId, mouth) => {
  const byTunnel = (toExits || []).find((e) => e && e.tunnel === tunnelId);
  if (byTunnel) return byTunnel;
  const candidates = (toExits || []).filter((e) =>
    e && e.target_scene === fromId && !(typeof e.tunnel === 'string' && e.tunnel));
  if (candidates.length === 1) return candidates[0];
  if (candidates.length > 1 && mouth) {
    const aligned = candidates.find((e) => e.x === mouth.target_x && e.y === mouth.target_y);
    if (aligned) return aligned;
    return null;
  }
  return candidates[0] || null;
};

let failed = 0;
const check = (name, fn) => {
  try { fn(); console.log(`ok ${name}`); }
  catch (e) { console.error(`FAIL ${name}: ${e.message}`); failed++; }
};

check('tunnel-id match wins', () => {
  const paired = { x: 1, y: 1, target_scene: 'a', tunnel: 'tunnel_a_b' };
  const other = { x: 9, y: 9, target_scene: 'a' };
  assert.equal(findTunnelPartner([other, paired], 'tunnel_a_b', 'a', { target_x: 9, target_y: 9 }), paired);
});

check('single untunneled candidate adopted', () => {
  const ret = { x: 5, y: 6, target_scene: 'a' };
  assert.equal(findTunnelPartner([ret], 'tunnel_a_b', 'a', { target_x: 5, target_y: 6 }), ret);
});

check('multi-exit aligns by coordinates', () => {
  const north = { x: 5, y: 1, target_scene: 'a' };
  const south = { x: 5, y: 6, target_scene: 'a' };
  assert.equal(findTunnelPartner([north, south], 'tunnel_a_b', 'a', { target_x: 5, target_y: 6 }), south);
  assert.equal(findTunnelPartner([north, south], 'tunnel_a_b', 'a', { target_x: 5, target_y: 1 }), north);
});

check('multi-exit ambiguity returns null', () => {
  const north = { x: 5, y: 1, target_scene: 'a' };
  const south = { x: 5, y: 6, target_scene: 'a' };
  assert.equal(findTunnelPartner([north, south], 'tunnel_a_b', 'a', { target_x: 0, target_y: 0 }), null);
});

check('different tunnel never adopted', () => {
  const other = { x: 5, y: 6, target_scene: 'a', tunnel: 'tunnel_a_c' };
  assert.equal(findTunnelPartner([other], 'tunnel_a_b', 'a', { target_x: 5, target_y: 6 }), null);
});

check('no candidates returns null', () => {
  assert.equal(findTunnelPartner([], 'tunnel_a_b', 'a', { target_x: 1, target_y: 1 }), null);
  assert.equal(findTunnelPartner([{ x: 1, y: 1, target_scene: 'zzz' }], 'tunnel_a_b', 'a', null), null);
});

if (failed) {
  console.error(`${failed} check(s) FAILED`);
  process.exit(1);
}
console.log('all 6 partner checks pass');
