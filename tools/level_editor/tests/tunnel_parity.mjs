// Parity guard for two-way tunnel diagnostics (review-13ba2f5 #2).
//
// Locks the TS side of the contract shared with Python tunnel_report()
// in tools/level_compiler/collision.py over the shared corpus at
// tools/level_compiler/tests/tunnel_parity_cases.json.
//
// tunnelStatus() below is a faithful copy of the function in
// vite.config.ts (same file, same invariant order, same error classes).
// To catch silent drift between this copy and the shipped handler, the
// test also asserts vite.config.ts still contains the invariant markers
// plus the findTunnelPartner / readAllLevels helpers (reviews #3 / #5).
//
// Run: node tools/level_editor/tests/tunnel_parity.mjs
// Exit code 0 = agree, 1 = drift/failure.
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';
import assert from 'assert/strict';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../../..');
const corpusPath = path.join(repoRoot, 'tools/level_compiler/tests/tunnel_parity_cases.json');
const viteConfigPath = path.join(repoRoot, 'tools/level_editor/vite.config.ts');

// Faithful copy of tunnelStatus() in vite.config.ts.
const tunnelStatus = (tunnel, levels) => {
  const ends = [];
  for (const name of Object.keys(levels).sort()) {
    (levels[name].exits || []).forEach((e, i) => {
      if (e && e.tunnel === tunnel) ends.push({ level: name, index: i, exit: e });
    });
  }
  if (ends.length !== 2) {
    return {
      ok: false,
      error: ends.length < 2
        ? `tunnel '${tunnel}' has only one end — create the return exit with the same tunnel id, or remove it to keep a one-way exit`
        : `tunnel '${tunnel}' has ${ends.length} ends — a tunnel is exactly two mouths`,
    };
  }
  const [A, B] = ends;
  if (A.level === B.level) {
    return { ok: false, error: `tunnel '${tunnel}' has both ends in '${A.level}' — mouths must live in different levels` };
  }
  if (A.exit.target_scene !== B.level || B.exit.target_scene !== A.level) {
    return { ok: false, error: `tunnel '${tunnel}' targets are not mutual (${A.level} → '${A.exit.target_scene}', ${B.level} → '${B.exit.target_scene}')` };
  }
  if (A.exit.target_x !== B.exit.x || A.exit.target_y !== B.exit.y ||
      B.exit.target_x !== A.exit.x || B.exit.target_y !== A.exit.y) {
    return { ok: false, error: `tunnel '${tunnel}' landings drifted — save the level to re-sync each landing onto the other mouth` };
  }
  return { ok: true, error: null };
};

const { cases } = JSON.parse(readFileSync(corpusPath, 'utf-8'));
let failed = 0;
for (const c of cases) {
  const st = tunnelStatus(c.tunnel, c.levels);
  try {
    assert.equal(st.ok, c.ok, `${c.name}: ok`);
    if (c.ts_fragment !== null && c.ts_fragment !== undefined) {
      assert.ok(
        st.error && st.error.includes(c.ts_fragment),
        `${c.name}: fragment '${c.ts_fragment}' not in '${st.error}'`,
      );
    }
    console.log(`ok ${c.name}`);
  } catch (e) {
    console.error(`FAIL ${c.name}: ${e.message}`);
    failed++;
  }
}

// Drift markers: the shipped handler must still implement the same
// invariants and helpers. If any assert below fails, tunnelStatus (or
// the #3/#5 helpers) changed in vite.config.ts without updating this
// copy + the Python mirror.
const src = readFileSync(viteConfigPath, 'utf-8');
const markers = [
  'has only one end',
  'exactly two mouths',
  'must live in different levels',
  'targets are not mutual',
  'landings drifted',
  'const findTunnelPartner',
  'const readAllLevels',
  'findTunnelPartner(toPeek.exits',
];
for (const m of markers) {
  try {
    assert.ok(src.includes(m), `vite.config.ts marker missing: ${m}`);
    console.log(`ok marker ${m}`);
  } catch (e) {
    console.error(`FAIL marker: ${e.message}`);
    failed++;
  }
}

if (failed) {
  console.error(`${failed} check(s) FAILED`);
  process.exit(1);
}
console.log(`all ${cases.length} cases + ${markers.length} markers pass`);
