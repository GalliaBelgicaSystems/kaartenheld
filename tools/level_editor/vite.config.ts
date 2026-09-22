import { defineConfig, Plugin } from 'vite';
import react from '@vitejs/plugin-react';
import fs from 'fs';
import path from 'path';
import os from 'os';
import { exec, execSync, spawn } from 'child_process';

// Full ROM toolchain probe, evaluated ONCE at server start (module scope).
// NOTE: never require() here: vite bundles this config to ESM, where
// dynamic require() throws "not supported" and kills the whole editor.
// Static imports above are the only safe form.
const TOOLCHAIN_TOOLS = ['python3', 'make', 'lcc', 'uge2source'];
function probeTools(): { ok: boolean; detail: { [k: string]: string } } {
  const detail: { [k: string]: string } = {};
  let ok = true;
  for (const t of TOOLCHAIN_TOOLS) {
    try {
      execSync(`${t} --version 2>&1 || ${t} -v 2>&1 || ${t} 2>&1`, { stdio: 'ignore' });
      detail[t] = 'runs';
    } catch (e: any) {
      // execSync throws on nonzero exit too; what matters is whether
      // the binary launched at all (status !== 127/126 and no ENOENT/ENOEXEC/Exec format error).
      const msg = String((e && e.message) || e);
      if (/ENOENT|ENOEXEC|Exec format error|command not found|not recognized/i.test(msg) || e.status === 127 || e.status === 126) {
        detail[t] = `MISSING/BROKEN (${msg.split('\n')[0]})`;
        ok = false;
      } else {
        detail[t] = 'runs';
      }
    }
  }
  try {
    execSync('rgbasm-huge --help 2>&1 || rgbasm --help 2>&1', { stdio: 'ignore' });
    detail['rgbasm'] = 'runs';
  } catch (e: any) {
    const msg = String((e && e.message) || e);
    detail['rgbasm'] = `MISSING/BROKEN (${msg.split('\n')[0]})`;
    ok = false;
  }
  return { ok, detail };
}

const toolchainProbe = probeTools();

function getNixBin(): string | null {
  const candidates = ['nix', '/run/current-system/sw/bin/nix', '/usr/bin/nix', '/bin/nix'];
  for (const c of candidates) {
    try {
      if (c.startsWith('/') && fs.existsSync(c)) return c;
    } catch {}
  }
  return 'nix';
}

const nixBin = getNixBin();

function levelEditorApiPlugin(): Plugin {
  return {
    name: 'level-editor-api',
    configureServer(server) {
      console.log(`[level-editor] toolchain=${toolchainProbe.ok ? 'direct' : 'nix-develop'}`,
        JSON.stringify(toolchainProbe.detail), `nix=${nixBin}`);
      server.middlewares.use((req, res, next) => {
        const repoRoot = path.resolve(__dirname, '../..');

        // Editor-facing ids for the screen mockups (the save/load maps
        // below must stay in sync with App.tsx).  Battle screens are NOT
        // listed here as editable "screens" — the Battle view
        // (BattleManager) owns screens/battle/*.json and saves through
        // the dedicated /api/save-battle-screen endpoint.
        const SCREEN_ID_TO_PATH: Record<string, string> = {
          'title': 'screens/title.json',
        };
        const isSafeId = (id: unknown) =>
          typeof id === 'string' && /^[A-Za-z0-9_]+$/.test(id);

        // Scene id registry (levels/registry.json): single source of
        // truth for real-scene ids.  Ids are append-only and never reused
        // (deleted levels tombstone into _retired, protecting saves);
        // every write is atomic (tmp + rename); the schema is versioned.
        // TEST names are refused (the fixed 240+ block).
        const REGISTRY_REL = path.join('levels', 'registry.json');
        const REGISTRY_VERSION = 1;
        const registryAbs = () => path.join(repoRoot, REGISTRY_REL);
        const levelAbs = (id: string) => path.join(repoRoot, 'levels', `${id}.json`);

        /** Atomic JSON write: write a sibling .tmp then rename over the
         *  target, so a crash can never leave a half-written file (the
         *  registry must never be corrupted). */
        const writeJsonAtomic = (abs: string, obj: unknown) => {
          fs.mkdirSync(path.dirname(abs), { recursive: true });
          const tmp = `${abs}.tmp`;
          fs.writeFileSync(tmp, JSON.stringify(obj, null, 2) + '\n', 'utf-8');
          fs.renameSync(tmp, abs);
        };

        const readRegistry = () => {
          const reg = JSON.parse(fs.readFileSync(registryAbs(), 'utf-8'));
          const v = typeof reg.version === 'number' ? reg.version : 0;
          if (v > REGISTRY_VERSION) {
            throw new Error(
              `registry version ${v} is not supported (this editor understands ${REGISTRY_VERSION}); update the editor or restore an older registry`);
          }
          reg.version = v || REGISTRY_VERSION;   // upgraded on next write
          reg.scenes = reg.scenes || {};
          reg._retired = reg._retired || {};
          reg._test_base = reg._test_base ?? 240;
          return reg;
        };
        const writeRegistry = (reg: any) => writeJsonAtomic(registryAbs(), reg);

        const validateSceneId = (sid: unknown): string => {
          if (typeof sid !== 'string' || !/^[a-z][a-z0-9_]*$/.test(sid)) {
            throw new Error(
              `invalid scene id '${sid}': lowercase letters, digits and underscores, starting with a letter`);
          }
          if ((sid as string).startsWith('test_')) {
            throw new Error(
              `scene id '${sid}' is reserved for harness fixtures (TEST block)`);
          }
          return sid as string;
        };

        const nextSceneId = (reg: any): number => {
          const used: number[] = Object.values(reg.scenes).filter(
            (v): v is number => typeof v === 'number');
          const retired: number[] = Object.values(reg._retired).filter(
            (v): v is number => typeof v === 'number');
          const next = [...used, ...retired, -1].reduce((a, b) => Math.max(a, b), -1) + 1;
          if (next >= reg._test_base) {
            throw new Error(
              `scene id space exhausted (next ${next} hits the TEST block at ${reg._test_base})`);
          }
          return next;
        };

        /** Engine-wired guard: a scene whose MAP_/SCENE_ symbol appears in
         *  hand-written C cannot be renamed or deleted from the editor (the
         *  C references would break the build).  GENERATED files are
         *  excluded: they carry a "Generated by" banner, reference every
         *  scene symbol, and are rebuilt from the registry on the next
         *  compile, so they must never block a rename/delete. */
        const cWiredScene = (sid: string): string[] => {
          const upper = sid.toUpperCase();
          const re = new RegExp(`\\b(?:MAP|SCENE)_${upper}\\b`);
          const hits: string[] = [];
          const walk = (dir: string) => {
            for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
              const p = path.join(dir, entry.name);
              if (entry.isDirectory()) { walk(p); continue; }
              if (!/\.(c|h)$/.test(entry.name)) continue;
              const text = fs.readFileSync(p, 'utf-8');
              // Generated files (scenes_content.c, actors_content.c,
              // scene_ids_generated.h, ...) all banner themselves; skip.
              if (/Generated by|DO NOT EDIT/i.test(text.slice(0, 600))) continue;
              if (re.test(text)) {
                hits.push(path.relative(repoRoot, p));
              }
            }
          };
          try { walk(path.join(repoRoot, 'src')); } catch { /* no src */ }
          return hits;
        };

        /** Rewrite target_scene old->new (rename) or remove it (delete)
         *  across every real level file, in exits[] AND neighbors{}.
         *  Returns the number of files changed. */
        const retargetExits = (oldId: string, newId: string | null): number => {
          let changed = 0;
          for (const f of fs.readdirSync(path.join(repoRoot, 'levels'))) {
            if (!f.endsWith('.json') || f === 'registry.json') continue;
            const abs = path.join(repoRoot, 'levels', f);
            let data: any;
            try { data = JSON.parse(fs.readFileSync(abs, 'utf-8')); } catch { continue; }
            if (data.id === oldId) continue;   // the level being renamed/deleted
            let touched = false;
            if (Array.isArray(data.exits)) {
              for (const e of data.exits) {
                if (e && e.target_scene === oldId) {
                  touched = true;
                  if (newId === null) { e.__remove = true; } else { e.target_scene = newId; }
                }
              }
              if (touched && newId === null) {
                data.exits = data.exits.filter((e: any) => !e.__remove);
              }
            }
            const neighbors = (data as any).neighbors;
            if (neighbors && typeof neighbors === 'object') {
              for (const dir of ['north', 'south', 'east', 'west']) {
                if (neighbors[dir] === oldId) {
                  neighbors[dir] = newId === null ? '' : newId;
                  touched = true;
                }
              }
            }
            if (touched) {
              writeJsonAtomic(abs, data);
              changed++;
            }
          }
          return changed;
        };

        const saveRealLevel = (idRaw: unknown, previousIdRaw: unknown, data: any): number => {
          const id = validateSceneId(idRaw);
          const reg = readRegistry();
          const previousId = previousIdRaw ? validateSceneId(previousIdRaw) : null;

          if (previousId && previousId !== id) {
            // RENAME: preserve the numeric scene id (saves + references stay
            // valid), rename the file, and rewire exit targets everywhere.
            if (typeof reg.scenes[previousId] !== 'number') {
              throw new Error(`cannot rename '${previousId}': it has no scene id (save it first)`);
            }
            if (typeof reg.scenes[id] === 'number') {
              throw new Error(`cannot rename to '${id}': that scene id is already in use`);
            }
            const wired = cWiredScene(previousId);
            if (wired.length) {
              throw new Error(
                `scene '${previousId}' is referenced in C (${wired.join(', ')}); it cannot be renamed from the editor`);
            }
            reg.scenes[id] = reg.scenes[previousId];
            delete reg.scenes[previousId];
            writeJsonAtomic(levelAbs(id), data);
            try { fs.unlinkSync(levelAbs(previousId)); } catch { /* absent */ }
            retargetExits(previousId, id);
            writeRegistry(reg);
            return reg.scenes[id];
          }

          if (typeof reg.scenes[id] !== 'number') {
            reg.scenes[id] = nextSceneId(reg);
          }
          writeJsonAtomic(levelAbs(id), data);
          writeRegistry(reg);
          return reg.scenes[id];
        };

        const deleteRealLevel = (idRaw: unknown): { id: string; scene_id: number; cleared: number } => {
          const id = validateSceneId(idRaw);
          const reg = readRegistry();
          if (typeof reg.scenes[id] !== 'number') {
            throw new Error(`'${id}' is not a registered level`);
          }
          const wired = cWiredScene(id);
          if (wired.length) {
            throw new Error(
              `scene '${id}' is referenced in C (${wired.join(', ')}); it cannot be deleted from the editor`);
          }
          // 1. retire the id (never reused) and persist first, so a crash
          //    can never leave the id free for reassignment.
          const sceneId = reg.scenes[id];
          reg._retired[id] = sceneId;
          delete reg.scenes[id];
          writeRegistry(reg);
          // 2. clear exits that targeted it (deleting a referenced level is
          //    allowed; the dangling links go away).
          const cleared = retargetExits(id, null);
          // 3. remove the file last.  Verify it is really gone: a silent
          //    unlink failure would leave a retired id with a lingering
          //    file, which then hard-fails the next compile and keeps dead
          //    actor ids reserved.
          try { fs.unlinkSync(levelAbs(id)); } catch { /* already absent */ }
          if (fs.existsSync(levelAbs(id))) {
            throw new Error(
              `retired '${id}' but could not delete levels/${id}.json; ` +
              `remove it manually before recompiling`);
          }
          return { id, scene_id: sceneId, cleared };
        };

        // Live disk reads (no editor rebuild needed after editing JSON by
        // hand or via another tool).  Bundled static imports in App.tsx /
        // Tileset.ts remain as the fallback for built bundles served
        // without this dev API.
        const sendJson = (obj: unknown) => {
          // no-store: content files change under the running server
          // (palette saves rewrite manifests); heuristic browser caching
          // of these GETs would show pre-save state after view switches.
          res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
          res.end(JSON.stringify(obj));
        };
        const readJsonFile = (rel: string) => {
          const target = path.resolve(repoRoot, rel);
          if (!target.startsWith(repoRoot + path.sep)) throw new Error('bad path');
          return JSON.parse(fs.readFileSync(target, 'utf-8'));
        };
        if (req.method === 'GET' && req.url === '/api/levels') {
          try {
            let sceneIds: Record<string, number> = {};
            try {
              sceneIds = readRegistry().scenes || {};
            } catch { /* registry unreadable: scene_id stays null */ }
            const levels = fs.readdirSync(path.join(repoRoot, 'levels'))
              .filter((f) => f.endsWith('.json') && f !== 'registry.json')
              .map((f) => {
                const data = readJsonFile(path.join('levels', f));
                const id = data.id || f.replace(/\.json$/, '');
                const sid = sceneIds[id];
                return { id, name: data.name || f, category: 'levels',
                         scene_id: typeof sid === 'number' ? sid : null };
              });
            const screens: Array<{ id: string; name: string; category: string }> = [];
            for (const [id, rel] of Object.entries(SCREEN_ID_TO_PATH)) {
              try {
                const data = readJsonFile(rel);
                screens.push({ id, name: data.title || data.label || id, category: 'screens' });
              } catch { /* missing screen file: skip */ }
            }
            sendJson({ success: true, levels, screens });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        // Actor-id registry across ALL scenes: ActorIds must be unique
        // across levels (the toolchain's cross-file check), so the
        // editor's auto-assign and browser validation need the global
        // picture, not just the current level.  ?exclude=<levelId>
        // omits one level (used when validating that level itself).
        if (req.method === 'GET' && (req.url || '').startsWith('/api/actor-ids')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const exclude = u.searchParams.get('exclude') || '';
            const used: Array<{ id: number; level: string }> = [];
            const dir = path.join(repoRoot, 'levels');
            // Retired ids are tombstoned and their (stale) files must not
            // reserve actor ids — otherwise a leftover file silently blocks
            // reuse and collides with the level the author just added.
            let retired: Record<string, unknown> = {};
            try { retired = readRegistry()._retired || {}; } catch { /* registry unreadable */ }
            for (const f of fs.readdirSync(dir).filter((f) => f.endsWith('.json'))) {
              const levelId = f.replace(/\.json$/, '');
              if (levelId === 'registry') continue;
              if (exclude && levelId === exclude) continue;
              if (levelId in retired) continue;
              const data = readJsonFile(path.join('levels', f));
              for (const o of (data.objects || []) as Array<{ properties?: Record<string, unknown> }>) {
                const aid = ((o.properties || {}) as Record<string, unknown>).actor_id;
                if (typeof aid === 'number' && Number.isInteger(aid) && aid > 0) {
                  used.push({ id: aid, level: levelId });
                }
              }
            }
            sendJson({ success: true, used });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'GET' && (req.url || '').startsWith('/api/level')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const category = u.searchParams.get('category') || 'levels';
            const id = u.searchParams.get('id') || '';
            if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
            let rel: string;
            if (category === 'screens') {
              rel = SCREEN_ID_TO_PATH[id];
              if (!rel) throw new Error(`unknown screen '${id}'`);
            } else if (category === 'levels') {
              rel = path.join('levels', `${id}.json`);
            } else {
              throw new Error(`unknown category '${category}'`);
            }
            sendJson({ success: true, id, category, data: readJsonFile(rel) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        // Enemy types + combat art sets (screens/enemy_types/*.json,
        // screens/combat_art/*.json): catalogue, single reads, and saves
        // for the battle-art studio.  New combat-art ids are appended by
        // the client with an explicit order (blob offsets must stay
        // stable, see battle_compile.py).
        const listJsonDir = (relDir: string, pick: (d: any, f: string) => any) => {
          return fs.readdirSync(path.join(repoRoot, relDir))
            .filter((f) => f.endsWith('.json'))
            .map((f) => pick(readJsonFile(path.join(relDir, f)), f));
        };
        if (req.method === 'GET' && req.url === '/api/enemy-types') {
          try {
            const items = listJsonDir('screens/enemy_types', (d, f) => ({
              id: d.id || f.replace(/\.json$/, ''), label: d.label || f,
              category: d.category || '', art: (d.sprite && d.sprite.art) || null,
              ow: !!((d.overworld && d.overworld.cells && d.overworld.cells.length)),
            }));
            sendJson({ success: true, items });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'GET' && (req.url || '').startsWith('/api/enemy-type')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const id = u.searchParams.get('id') || '';
            if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
            sendJson({ success: true, id, data: readJsonFile(path.join('screens', 'enemy_types', `${id}.json`)) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'GET' && req.url === '/api/combat-art') {
          try {
            const items = listJsonDir('screens/combat_art', (d, f) => ({
              id: d.id || f.replace(/\.json$/, ''), label: d.label || f,
              order: d.order ?? 0, width: d.width ?? 0, height: d.height ?? 0,
            }));
            sendJson({ success: true, items });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'GET' && (req.url || '').startsWith('/api/combat-art-set')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const id = u.searchParams.get('id') || '';
            if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
            sendJson({ success: true, id, data: readJsonFile(path.join('screens', 'combat_art', `${id}.json`)) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && (req.url === '/api/save-enemy-type' || req.url === '/api/save-combat-art')) {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id, data } = JSON.parse(body);
              if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
              const subdir = req.url === '/api/save-enemy-type' ? 'enemy_types' : 'combat_art';
              const targetPath = path.join(repoRoot, 'screens', subdir, `${id}.json`);
              fs.writeFileSync(targetPath, JSON.stringify(data, null, 2), 'utf-8');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Entity types (screens/enemy_types/*.json + screens/entity_types/*.json)
        // are the single source of truth for the ENTITY_ID_* game range
        // (tools/screen_compiler/entity_compile.py).  The editor lists them
        // for the NPC Entity-ID picker and can create/delete either kind.
        const ENTITY_DIRS: Array<[string, string]> = [
          ['enemy_types', 'enemy'],
          ['entity_types', 'entity'],
        ];
        const entityTypeDirs = () => {
          const out: Array<{ id: string; label: string; kind: string; dir: string }> = [];
          for (const [dir, kind] of ENTITY_DIRS) {
            for (const f of fs.readdirSync(path.join(repoRoot, 'screens', dir))) {
              if (!f.endsWith('.json')) continue;
              let label = f.replace(/\.json$/, '');
              try { const d = readJsonFile(path.join('screens', dir, f)); label = d.label || label; } catch { /* keep stem */ }
              out.push({ id: f.replace(/\.json$/, ''), label, kind, dir });
            }
          }
          out.sort((a, b) => a.id.localeCompare(b.id));
          return out;
        };
        if (req.method === 'GET' && req.url === '/api/entity-types') {
          try {
            sendJson({ success: true, items: entityTypeDirs().map((e) => ({
              id: e.id, label: e.label, kind: e.kind,
              entity_id: 'ENTITY_ID_' + e.id.toUpperCase(),
            })) });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }
        if (req.method === 'GET' && (req.url || '').startsWith('/api/entity-type')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const id = u.searchParams.get('id') || '';
            if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
            const dir = u.searchParams.get('dir') || 'entity_types';
            sendJson({ success: true, id, data: readJsonFile(path.join('screens', dir, `${id}.json`)) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }
        if (req.method === 'POST' && req.url === '/api/save-entity-type') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id, dir, data } = JSON.parse(body);
              if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
              const dirs = ENTITY_DIRS.map((e) => e[0]);
              if (!dirs.includes(dir)) throw new Error(`invalid dir '${dir}'`);
              if (!data || typeof data !== 'object') throw new Error('data must be an object');
              data.id = id;
              writeJsonAtomic(path.join(repoRoot, 'screens', dir, `${id}.json`), data);
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }
        if (req.method === 'POST' && req.url === '/api/delete-entity-type') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id, dir } = JSON.parse(body);
              if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
              const dirs = ENTITY_DIRS.map((e) => e[0]);
              if (!dirs.includes(dir)) throw new Error(`invalid dir '${dir}'`);
              fs.unlinkSync(path.join(repoRoot, 'screens', dir, `${id}.json`));
              sendJson({ success: true });
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Dialogue content (screens/dialogue/*.json): list, single read,
        // and save for the dialogue text editor.  Ids assign by sorted
        // filename at compile time; the UI edits speaker + lines only.
        if (req.method === 'GET' && req.url === '/api/dialogues') {
          try {
            const items = listJsonDir('screens/dialogue', (d, f) => {
              const id = d.id || f.replace(/\.json$/, '');
              const lines: string[] = Array.isArray(d.lines) ? d.lines : [];
              return { id, label: lines[0] || id };
            });
            sendJson({ success: true, items });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'GET' && (req.url || '').startsWith('/api/dialogue')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const id = u.searchParams.get('id') || '';
            if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
            sendJson({ success: true, id, data: readJsonFile(path.join('screens', 'dialogue', `${id}.json`)) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && req.url === '/api/save-dialogue') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id, data } = JSON.parse(body);
              if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
              const targetPath = path.join(repoRoot, 'screens', 'dialogue', `${id}.json`);
              fs.mkdirSync(path.dirname(targetPath), { recursive: true });
              fs.writeFileSync(targetPath, JSON.stringify(data, null, 2) + '\n', 'utf-8');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Tutorial slides (screens/tutorial.json): single read + save for
        // the slide text editor.  Slide order is navigation order.
        if (req.method === 'GET' && req.url === '/api/tutorial') {
          try {
            sendJson({ success: true, data: readJsonFile(path.join('screens', 'tutorial.json')) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && req.url === '/api/save-tutorial') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { data } = JSON.parse(body);
              const targetPath = path.join(repoRoot, 'screens', 'tutorial.json');
              fs.writeFileSync(targetPath, JSON.stringify(data, null, 2) + '\n', 'utf-8');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Sound registry (screens/sfx.json): maps each fixed SFX id to a
        // .uge file.  Curator only — the .uge stays the authored source;
        // tools/transcribe_sfx.py reads this same file to emit the step
        // tables (make sfx), so the editor and the build cannot drift.
        const UGE_DIRS = ['assets/sfx', 'assets/music'];
        if (req.method === 'GET' && req.url === '/api/uge-files') {
          try {
            const files: Array<{ path: string; name: string; dir: string }> = [];
            for (const d of UGE_DIRS) {
              for (const f of fs.readdirSync(path.join(repoRoot, d))) {
                if (f.endsWith('.uge')) {
                  files.push({ path: `${d}/${f}`, name: f, dir: d });
                }
              }
            }
            files.sort((a, b) => a.path.localeCompare(b.path));
            sendJson({ success: true, files });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'GET' && req.url === '/api/sfx') {
          try {
            sendJson({ success: true, data: readJsonFile(path.join('screens', 'sfx.json')) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && req.url === '/api/save-sfx') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { data } = JSON.parse(body);
              const targetPath = path.join(repoRoot, 'screens', 'sfx.json');
              writeJsonAtomic(targetPath, data);
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Card catalogue (src/game/cards_content.c): parsed by
        // tools/card_catalog.py so the editor's shop picker shows the real
        // symbols/names/prices without a second source of truth.
        if (req.method === 'GET' && req.url === '/api/cards') {
          try {
            const out = execSync('python3 tools/card_catalog.py --json', {
              cwd: repoRoot, encoding: 'utf-8',
            });
            sendJson({ success: true, cards: JSON.parse(out) });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        // Shop registry (screens/shops/<id>.json): id = filename stem, items
        // are CARD_* symbols.  The same files are compiled by
        // tools/screen_compiler/shops_compile.py (make shops).
        const SHOP_MAX_ITEMS = 50;
        const shopPath = (id: number) => path.join(repoRoot, 'screens', 'shops', `${id}.json`);
        const needShopId = (raw: unknown): number => {
          const n = Number(raw);
          if (!Number.isInteger(n) || n < 1 || n > 255) {
            throw new Error(`invalid shop id '${raw}'`);
          }
          return n;
        };
        if (req.method === 'GET' && req.url === '/api/shops') {
          try {
            const dir = path.join(repoRoot, 'screens', 'shops');
            const items = fs.readdirSync(dir)
              .filter((f) => /^\d+\.json$/.test(f))
              .map((f) => {
                const id = parseInt(f, 10);
                const d = readJsonFile(path.join('screens', 'shops', f));
                return { id, label: d.label || `Shop ${id}`, buys: d.buys ? 1 : 0,
                         items: Array.isArray(d.items) ? d.items : [],
                         count: Array.isArray(d.items) ? d.items.length : 0 };
              })
              .sort((a, b) => a.id - b.id);
            sendJson({ success: true, items });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }
        if (req.method === 'GET' && (req.url || '').startsWith('/api/shop')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const id = needShopId(u.searchParams.get('id'));
            sendJson({ success: true, id,
                       data: readJsonFile(path.join('screens', 'shops', `${id}.json`)) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }
        if (req.method === 'POST' && req.url === '/api/save-shop') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id: rawId, data } = JSON.parse(body);
              const id = needShopId(rawId);
              const items = Array.isArray(data.items) ? data.items : [];
              if (items.length > SHOP_MAX_ITEMS) {
                throw new Error(`${items.length} items exceeds SHOP_MAX_ITEMS (${SHOP_MAX_ITEMS})`);
              }
              for (const it of items) {
                if (typeof it !== 'string' || !/^CARD_[A-Z0-9_]+$/.test(it)) {
                  throw new Error(`invalid card symbol '${it}'`);
                }
              }
              const out = {
                label: typeof data.label === 'string' ? data.label : '',
                buys: data.buys ? 1 : 0,
                items,
              };
              writeJsonAtomic(shopPath(id), out);
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: shopPath(id) }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }
        if (req.method === 'POST' && req.url === '/api/delete-shop') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const id = needShopId(JSON.parse(body).id);
              fs.unlinkSync(shopPath(id));
              sendJson({ success: true });
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // ── Palette preview / assignment ──────────────────────────────
        // BG ramps live in generated/tiles/<tileset>.json (palettes +
        // per-sheet-tile tile_palettes, produced by palette_compiler.py
        // from src/game/tiles_content.c).  OBJ ramps live in ui.c.  An
        // explicit per-tile `palette` (editor's Palette view) overrides
        // the auto-match in palette_compiler.py; enemy/hero `overworld.
        // palette` is already data-driven (battle_compile.py -> ow_palette).
        // Slot 4 of every WORLD tileset is hardware-reserved (UI_COLOR_PAPER
        // in src/ui/ui.h: ui_draw_dialogue re-programs it at runtime), so
        // no world tile may be assigned to it (mirrors PAPER_SLOT in
        // tools/verify_palette_manifest.py).  OBJ slots are all usable.
        const TILESETS = ['forest', 'castle', 'desolate_landscape', 'village', 'sprites'];
        const PAPER_SLOT = 4;
        const PAPER_REASON = 'slot 4 = UI_COLOR_PAPER, reprogrammed by dialogue boxes at runtime';
        // Sprite art lives per sheet cell, not per enemy type: enemy_ow
        // cells at public/tiles/enemies/<cell>.png, hero cells at
        // public/tiles/hero/<cell>.png. Town NPC portraits live in the
        // actors tileset (tools/compose_enemy_sprites.py NPC_FILES:
        // npc_guard -> actors_guard, i.e. actors_<suffix>). Probe the
        // filesystem so new cells resolve without a code change; null
        // means the client renders a named placeholder (visible gap).
        const spriteImageFor = (cell: string, hero = false): string | null => {
          const pub = (...parts: string[]) =>
            path.join(repoRoot, 'tools', 'level_editor', 'public', ...parts);
          if (fs.existsSync(pub('tiles', hero ? 'hero' : 'enemies', `${cell}.png`))) {
            return `/tiles/${hero ? 'hero' : 'enemies'}/${cell}.png`;
          }
          const npc = cell.match(/^npc_(.*)$/);
          if (npc && fs.existsSync(pub('tiles', 'actors', `actors_${npc[1]}.png`))) {
            return `/tiles/actors/actors_${npc[1]}.png`;
          }
          return null;
        };
        const objSlotOfRamps = () => {
          const objManifest = readJsonFile(path.join('generated', 'tiles', 'obj.json'));
          const m: Record<string, number> = {};
          for (const key of Object.keys((objManifest && objManifest.slots) || {})) {
            m[objManifest.slots[key].ramp] = Number(key);
          }
          return m;
        };
        const parseObjPalettes = () => {
          // OBJ ramps live in generated/tiles/obj.json (palette_compiler:
          // slots 0-4 artist ramps, 5-7 grey). Same file battle_compile
          // resolves content ramp names against.
          const manifest = readJsonFile(path.join('generated', 'tiles', 'obj.json'));
          const out: Array<{ index: number; name: string; colors: string[] }> = [];
          const slots = (manifest && manifest.slots) || {};
          for (const key of Object.keys(slots).sort((a, b) => Number(a) - Number(b))) {
            out.push({ index: Number(key), name: slots[key].ramp, colors: slots[key].colors });
          }
          return out;
        };
        const readTilesetManifest = (tileset: string) => {
          if (!TILESETS.includes(tileset)) throw new Error(`unknown tileset '${tileset}'`);
          const manifest = readJsonFile(path.join('generated', 'tiles', `${tileset}.json`));
          const ts = readJsonFile(path.join('tools', 'level_editor', 'tilesets', `${tileset}.json`));
          const vb = (ts.vram_block && ts.vram_block.tiles) || [];
          const maxX = vb.reduce((mx: number, t: any) => Math.max(mx, t.x || 0), 0);
          const byId: Record<string, any> = {};
          for (const t of ts.tiles || []) byId[t.id] = t;
          const pal = manifest.tile_palettes || [];
          const ids = manifest.tile_ids || [];
          const slotOfId: Record<string, number> = {};
          for (let k = 0; k < ids.length; k++) slotOfId[ids[k]] = pal[k];
          const tiles = [...vb]
            .sort((a: any, b: any) => ((a.y * (maxX + 1) + a.x) - (b.y * (maxX + 1) + b.x)))
            .map((v: any, i: number) => {
              const t = byId[v.tile] || {};
              const slot = slotOfId[v.tile];
              return {
                id: v.tile, label: t.label || v.tile,
                image_url: t.image_url || null,
                palette: typeof slot === 'number' ? slot : 0,
              };
            });
          return { manifest, ts, tiles };
        };

        if (req.method === 'GET' && (req.url || '').startsWith('/api/palettes')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const tileset = u.searchParams.get('tileset') || 'forest';
            // Content files name artist ramps; the editor UI works in
            // hardware slots, so resolve names -> slots for display (the
            // reverse of /api/assign-palette below).
            const objSlotOf: Record<string, number> = objSlotOfRamps();
            const readEnemyType = (f: string) => {
              const d = readJsonFile(path.join('screens', 'enemy_types', f));
              const id = d.id || f.replace(/\.json$/, '');
              const ow = d.overworld || {};
              const cells: string[] = ow.cells || [];
              const pal = ow.palette || 0;
              return { id, label: d.label || id, cells,
                       ramp: typeof pal === 'string' ? pal : null,
                       image_url: cells.length > 0 ? spriteImageFor(cells[0]) : null,
                       palette: typeof pal === 'string' ? (objSlotOf[pal] ?? 0) : pal };
            };
            const enemies = fs.readdirSync(path.join(repoRoot, 'screens', 'enemy_types'))
              .filter((f) => f.endsWith('.json'))
              .map(readEnemyType)
              .sort((a, b) => a.id.localeCompare(b.id));
            const hero = readJsonFile(path.join('screens', 'hero.json'));
            const heroOw = hero.overworld || {};
            const heroCells: string[] = heroOw.cells || [];
            const heroPal = heroOw.palette || 0;
            const heroRamp = typeof heroPal === 'string' ? heroPal : null;
            if (tileset === 'sprites') {
              // Pseudo-tileset: every pipeline sprite sheet cell
              // (enemy_ow + hero_ow) with its OBJ ramp, so the sprite
              // sheets are browsable like the world tilesets. Per-cell
              // assignment does not exist (enemy types own their palette;
              // use the Enemies section below), so assign stays disabled.
              const tiles: Array<{ id: string; label: string; image_url: string | null; palette: number }> = [];
              for (const e of enemies) {
                const slot = typeof (e as any).palette === 'number' ? (e as any).palette : 0;
                for (const cell of e.cells) {
                  tiles.push({ id: cell, label: `${e.label} · ${cell}`,
                               image_url: spriteImageFor(cell), palette: slot });
                }
              }
              const heroSlot = typeof heroPal === 'string' ? (objSlotOf[heroPal] ?? 0) : heroPal;
              for (const cell of heroCells) {
                tiles.push({ id: cell, label: `Hero · ${cell}`,
                             image_url: spriteImageFor(cell, true), palette: heroSlot });
              }
              sendJson({
                success: true, tileset,
                bg: parseObjPalettes(),
                obj: parseObjPalettes(),
                tiles,
                enemies,
                hero: { palette: heroSlot, ramp: heroRamp,
                        image_url: heroCells.length > 0 ? spriteImageFor(heroCells[0], true) : null },
              });
              return;
            }
            const { manifest, tiles } = readTilesetManifest(tileset);
            sendJson({
              success: true, tileset,
              bg: (manifest.palettes || []).map((p: any) => (p.index === PAPER_SLOT
                ? { ...p, reserved: true, reservedReason: PAPER_REASON }
                : p)),
              obj: parseObjPalettes(),
              tiles,
              enemies,
              hero: { palette: typeof heroPal === 'string' ? (objSlotOf[heroPal] ?? 0) : heroPal,
                      ramp: heroRamp,
                      image_url: heroCells.length > 0 ? spriteImageFor(heroCells[0], true) : null },
            });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && req.url === '/api/assign-palette') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { kind, tileset, id, palette } = JSON.parse(body);
              const p = Number(palette);
              if (!Number.isInteger(p) || p < 0 || p > 7) {
                throw new Error(`palette ${palette} out of 0-7`);
              }
              // The editor assigns hardware slots; content files store
              // artist ramp names, so resolve slot -> ramp here.
              const rampOfSlot = (manifestPath: string) => {
                const m = readJsonFile(manifestPath);
                const entry = m.slots
                  ? m.slots[String(p)]
                  : (m.palettes || []).find((e: any) => e.index === p);
                if (!entry) throw new Error(`no ramp at slot ${p} in ${manifestPath}`);
                return entry.ramp || entry.name;
              };
              if (kind === 'tile') {
                if (tileset === 'sprites') {
                  throw new Error(`tileset 'sprites' has no per-cell assignment: `
                    + `sprite ramps are owned by enemy types (use kind 'enemy')`);
                }
                if (!TILESETS.includes(tileset)) throw new Error(`unknown tileset '${tileset}'`);
                if (p === PAPER_SLOT) {
                  throw new Error(`palette slot ${p} is hardware-reserved (${PAPER_REASON}); `
                    + `pick another slot`);
                }
                const rel = path.join('tools', 'level_editor', 'tilesets', `${tileset}.json`);
                const ts = readJsonFile(rel);
                const tile = (ts.tiles || []).find((t: any) => t.id === id);
                if (!tile) throw new Error(`unknown tile '${id}' in ${tileset}`);
                tile.palette = rampOfSlot(path.join('generated', 'tiles', `${tileset}.json`));
                writeJsonAtomic(path.join(repoRoot, rel), ts);
              } else if (kind === 'enemy') {
                if (!isSafeId(id)) throw new Error(`invalid enemy id '${id}'`);
                const rel = path.join('screens', 'enemy_types', `${id}.json`);
                const d = readJsonFile(rel);
                d.overworld = { ...(d.overworld || {}),
                                palette: rampOfSlot(path.join('generated', 'tiles', 'obj.json')) };
                writeJsonAtomic(path.join(repoRoot, rel), d);
              } else if (kind === 'hero') {
                const rel = path.join('screens', 'hero.json');
                const d = readJsonFile(rel);
                d.overworld = { ...(d.overworld || {}),
                                palette: rampOfSlot(path.join('generated', 'tiles', 'obj.json')) };
                writeJsonAtomic(path.join(repoRoot, rel), d);
              } else {
                throw new Error(`unknown kind '${kind}'`);
              }
              sendJson({ success: true });
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Ramp authoring (assets/palette.txt, the artist source of truth).
        // Edits color definitions and/or ramp-row refs with a line-preserving
        // rewrite (comments, order, blank lines untouched), then runs the
        // full palette_compiler.py so the export, shades, manifests and
        // mismatch report stay mutually consistent. The export is NEVER
        // written directly (ramp-check freshness arbitrates).
        // Body: { colorEdits: [{section, name, hex}],
        //         rampRepoints: [{ramp, position 0-3, ref: "SECTION/name"}] }
        // Writes happen only on explicit client Save, never per drag tick.
        if (req.method === 'POST' && req.url === '/api/save-palette') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const parsed = JSON.parse(body);
              const colorEdits = parsed.colorEdits || [];
              const rampRepoints = parsed.rampRepoints || [];
              if ((!Array.isArray(colorEdits) || colorEdits.length === 0) &&
                  (!Array.isArray(rampRepoints) || rampRepoints.length === 0)) {
                throw new Error('nothing to save: want colorEdits and/or rampRepoints');
              }
              const NAME_RE = /^[A-Za-z0-9_]+$/;
              const HEX_RE = /^#[0-9a-fA-F]{6}$/;
              const REF_RE = /^[A-Za-z0-9_]+\/[A-Za-z0-9_]+$/;
              for (const e of colorEdits) {
                if (!e || !NAME_RE.test(e.section || '') || !NAME_RE.test(e.name || '')) {
                  throw new Error(`bad color edit target '${e && e.section}/${e && e.name}'`);
                }
                if (!HEX_RE.test(e.hex || '')) {
                  throw new Error(`bad hex '${e && e.hex}' (want #rrggbb)`);
                }
              }
              for (const r of rampRepoints) {
                if (!r || !NAME_RE.test(r.ramp || '')) {
                  throw new Error(`bad repoint ramp '${r && r.ramp}'`);
                }
                if (!Number.isInteger(r.position) || r.position < 0 || r.position > 3) {
                  throw new Error(`bad repoint position '${r && r.position}' (want 0-3)`);
                }
                if (!REF_RE.test(r.ref || '')) {
                  throw new Error(`bad repoint ref '${r && r.ref}' (want SECTION/name)`);
                }
              }
              // No duplicate targets (ambiguous otherwise).
              const seenColor = new Set<string>();
              for (const e of colorEdits) {
                const k = `${e.section}/${e.name}`;
                if (seenColor.has(k)) throw new Error(`duplicate color edit for '${k}'`);
                seenColor.add(k);
              }
              const seenPos = new Set<string>();
              for (const r of rampRepoints) {
                const k = `${r.ramp}:${r.position}`;
                if (seenPos.has(k)) throw new Error(`duplicate repoint for '${k}'`);
                seenPos.add(k);
              }

              // Minimal palette.txt model (mirrors palette_parse.py): color
              // definitions live under SECTION headers, ramp rows carry 4
              // refs (UNUSED already expanded on read like the parser does).
              const abs = path.join(repoRoot, 'assets', 'palette.txt');
              const raw = fs.readFileSync(abs, 'utf-8');
              const lines = raw.split('\n');
              const colorLine = new Map<string, number>();
              const rampRow = new Map<string, { line: number; prefix: string; refs: string[] }>();
              let section: string | null = null;
              lines.forEach((line, i) => {
                const s = line.trim();
                if (!s || s.startsWith('#')) return;
                if (!s.includes(':')) { section = s; return; }
                const colon = line.indexOf(':');
                const name = line.slice(0, colon).trim();
                const value = line.slice(colon + 1).trim();
                if (!name) throw new Error(`palette.txt:${i + 1}: empty name`);
                if (value.startsWith('#') && !value.includes(',')) {
                  if (section === null) throw new Error(`palette.txt:${i + 1}: color outside any section`);
                  colorLine.set(`${section}/${name}`, i);
                } else if (value.includes(',')) {
                  const refs = value.split(',').map((r) => r.trim());
                  if (refs.length !== 4) throw new Error(`palette.txt:${i + 1}: ramp '${name}' wants 4 refs`);
                  // Expand UNUSED to the previous ref (mirrors palette_parse:
                  // UNUSED repeats the previous shade, so repeating its ref
                  // is equivalent for position-based replacement).
                  const resolved: string[] = [];
                  for (const r of refs) {
                    if (r === 'UNUSED') {
                      if (resolved.length === 0) {
                        throw new Error(`palette.txt:${i + 1}: ramp '${name}': UNUSED in first position`);
                      }
                      resolved.push(resolved[resolved.length - 1]);
                    } else {
                      resolved.push(r);
                    }
                  }
                  rampRow.set(name, { line: i, prefix: line.slice(0, colon), refs: resolved });
                } else {
                  throw new Error(`palette.txt:${i + 1}: cannot parse line`);
                }
              });

              // Two-phase: plan every edit, write only if ALL resolve.
              const planned: Array<{ line: number; text: string }> = [];
              for (const e of colorEdits) {
                const idx = colorLine.get(`${e.section}/${e.name}`);
                if (idx === undefined) throw new Error(`unknown color '${e.section}/${e.name}'`);
                const line = lines[idx];
                const hash = line.indexOf('#');
                if (hash < 0) throw new Error(`palette.txt:${idx + 1}: color line has no hex`);
                planned.push({ line: idx, text: line.slice(0, hash) + e.hex.toLowerCase() + line.slice(hash + 7) });
              }
              // Grouped per ramp: separate planned entries for one line
              // would overwrite each other (last wins, earlier lost).
              const repointsByRamp = new Map<string, Map<number, string>>();
              for (const r of rampRepoints) {
                const row = rampRow.get(r.ramp);
                if (!row) throw new Error(`unknown ramp '${r.ramp}'`);
                const [sec, nm] = r.ref.split('/');
                if (!colorLine.has(`${sec}/${nm}`)) throw new Error(`unknown color ref '${r.ref}'`);
                if (!repointsByRamp.has(r.ramp)) repointsByRamp.set(r.ramp, new Map());
                repointsByRamp.get(r.ramp)!.set(r.position, r.ref);
              }
              for (const [ramp, posMap] of repointsByRamp) {
                const row = rampRow.get(ramp)!;
                const finalRefs = row.refs.map((v, i) => posMap.has(i) ? posMap.get(i)! : v);
                // Canonicalize repeats to UNUSED (the file's own convention:
                // a repeated ref is always spelled UNUSED). This keeps
                // reverts byte-stable across saves: reverting to the
                // repeated value restores the keyword even though the
                // previous save spelled it out. A repinned neighbor keeps
                // its effective color instead of silently following.
                // (Position 0 can never match: prev starts empty and refs
                // are validated SECTION/name strings.)
                const out: string[] = [];
                let prev = '';
                for (let i = 0; i < 4; i++) {
                  if (finalRefs[i] === prev) out.push('UNUSED');
                  else out.push(finalRefs[i]);
                  prev = finalRefs[i];
                }
                planned.push({ line: row.line, text: `${row.prefix}: ${out.join(', ')}` });
              }
              for (const p of planned) lines[p.line] = p.text;
              const tmp = `${abs}.tmp`;
              fs.writeFileSync(tmp, lines.join('\n'), 'utf-8');
              fs.renameSync(tmp, abs);

              runInToolchain('python3 tools/palette_compiler.py', (err: any, stdout: string, stderr: string) => {
                if (err) {
                  const combined = [stderr, stdout, err.message].filter(Boolean).join('\n\n');
                  res.writeHead(200, { 'Content-Type': 'application/json' });
                  res.end(JSON.stringify({
                    success: false,
                    error: `palette.txt saved but manifest refresh failed (revert via git if needed): ${combined}`,
                  }));
                  return;
                }
                sendJson({ success: true, log: stdout });
              });
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Manifest freshness for the palette UI: are generated/tiles/*
        // (+ both palette_ramps.json copies) newer than every source that
        // feeds palette_compiler.py? Hand edits (palette.txt, content JSON,
        // art PNGs) invalidate the preview the editor renders from the
        // manifests. Our own saves refresh the manifest server-side, so a
        // stale result here always means an out-of-band change.
        if (req.method === 'GET' && (req.url || '').startsWith('/api/palette-freshness')) {
          try {
            const mtime = (rel: string): number | null => {
              try {
                return fs.statSync(path.join(repoRoot, rel)).mtimeMs;
              } catch {
                return null;
              }
            };
            const sources = [
              'assets/palette.txt',
              'tools/palette_slots.json',
              ...fs.readdirSync(path.join(repoRoot, 'tools', 'level_editor', 'tilesets'))
                .filter((f) => f.endsWith('.json')).map((f) => path.join('tools', 'level_editor', 'tilesets', f)),
              ...fs.readdirSync(path.join(repoRoot, 'screens', 'enemy_types'))
                .filter((f) => f.endsWith('.json')).map((f) => path.join('screens', 'enemy_types', f)),
              'screens/hero.json',
              'screens/cards_skin.json',
              'screens/battle_hud.json',
              ...fs.readdirSync(path.join(repoRoot, 'screens', 'combat_art'))
                .filter((f) => f.endsWith('.json')).map((f) => path.join('screens', 'combat_art', f)),
            ];
            const products = [
              'assets/palette_ramps.json',
              'tools/level_editor/public/palette_ramps.json',
              ...['forest', 'castle', 'desolate_landscape', 'village', 'base', 'obj', 'title']
                .map((s) => path.join('generated', 'tiles', `${s}.json`)),
            ];
            const missing = products.filter((p) => mtime(p) === null);
            if (missing.length > 0) {
              sendJson({ success: true, fresh: false, state: 'missing', missing });
              return;
            }
            let maxSource = 0;
            for (const s of sources) {
              const m = mtime(s);
              if (m !== null && m > maxSource) maxSource = m;
            }
            let minProduct = Infinity;
            for (const p of products) {
              const m = mtime(p);
              if (m !== null && m < minProduct) minProduct = m;
            }
            sendJson({
              success: true,
              fresh: minProduct >= maxSource,
              state: minProduct >= maxSource ? 'fresh' : 'stale',
              sourceMtime: maxSource,
              productMtime: minProduct,
            });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        // Hero definition (screens/hero.json): single read + save for the
        // hero manager (art, stats, starter deck).  The client sends and
        // receives the hero object directly (not wrapped).
        if (req.method === 'GET' && req.url === '/api/card-skin') {
          try {
            sendJson({ success: true, data: readJsonFile(path.join('screens', 'cards_skin.json')) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && req.url === '/api/save-card-skin') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { data } = JSON.parse(body);
              const targetPath = path.join(repoRoot, 'screens', 'cards_skin.json');
              fs.writeFileSync(targetPath, JSON.stringify(data, null, 1) + '\n', 'utf-8');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Battle HUD skin (screens/battle_hud.json): singleton read + save
        // for the battle manager's HUD tab.
        if (req.method === 'GET' && req.url === '/api/battle-hud') {
          try {
            sendJson({ success: true, data: readJsonFile(path.join('screens', 'battle_hud.json')) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && req.url === '/api/save-battle-hud') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { data } = JSON.parse(body);
              const targetPath = path.join(repoRoot, 'screens', 'battle_hud.json');
              fs.writeFileSync(targetPath, JSON.stringify(data, null, 1) + '\n', 'utf-8');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Battle screen layout (screens/battle/<id>.json): single read for
        // the battle manager's Layout tab.  Saving reuses /api/save-level
        // with category 'screens' (SCREEN_ID_TO_PATH routes battle_* ids).
        if (req.method === 'GET' && (req.url || '').startsWith('/api/battle-screen')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const id = u.searchParams.get('id') || '';
            if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
            sendJson({ success: true, id, data: readJsonFile(path.join('screens', 'battle', `${id}.json`)) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'GET' && req.url === '/api/hero') {
          try {
            sendJson(readJsonFile(path.join('screens', 'hero.json')));
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && req.url === '/api/save-hero') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { data } = JSON.parse(body);
              if (!data || typeof data !== 'object') throw new Error('missing hero data');
              const targetPath = path.join(repoRoot, 'screens', 'hero.json');
              fs.writeFileSync(targetPath, JSON.stringify(data, null, 2), 'utf-8');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        if (req.method === 'GET' && req.url === '/api/tilesets') {
          try {
            const dir = path.join(repoRoot, 'tools', 'level_editor', 'tilesets');
            const tilesets = fs.readdirSync(dir)
              .filter((f) => f.endsWith('.json'))
              .map((f) => {
                const data = readJsonFile(path.join('tools', 'level_editor', 'tilesets', f));
                return { id: data.id || f.replace(/\.json$/, ''), label: data.label || f };
              });
            sendJson({ success: true, tilesets });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'GET' && (req.url || '').startsWith('/api/tileset')) {
          try {
            const u = new URL(req.url || '', 'http://localhost');
            const id = u.searchParams.get('id') || '';
            if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
            sendJson({ success: true, id, data: readJsonFile(path.join('tools', 'level_editor', 'tilesets', `${id}.json`)) });
          } catch (err: any) {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        if (req.method === 'POST' && req.url === '/api/save-battle-screen') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id, data } = JSON.parse(body);
              if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
              const targetPath = path.join(repoRoot, 'screens', 'battle', `${id}.json`);
              fs.writeFileSync(targetPath, JSON.stringify(data, null, 1) + '\n', 'utf-8');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // ── Auto exits ────────────────────────────────────────────────
        // A one-directional exit (the hand-authored norm) leaves a level
        // unreachable from the other side.  These endpoints compute the
        // reciprocal ("return") exit so the editor can preview it and
        // assign it in one click.  Placement is deterministic:
        //   direction  -> opposite side of the target map
        //   gate       -> one tile inside that side, on the landing row/col
        //   return spawn -> the tile just inside the from-gate
        const EXIT_OPPOSITE: Record<string, string> = {
          NORTH: 'SOUTH', SOUTH: 'NORTH', EAST: 'WEST', WEST: 'EAST',
        };
        const EXIT_VEC: Record<string, [number, number]> = {
          NORTH: [0, -1], SOUTH: [0, 1], EAST: [1, 0], WEST: [-1, 0],
        };
        const clamp = (v: number, a: number, b: number) => Math.max(a, Math.min(b, v));
        const readLevel = (id: string) => {
          if (!isSafeId(id)) throw new Error(`invalid level id '${id}'`);
          return readJsonFile(path.join('levels', `${id}.json`));
        };
        const proposeReturn = (fromId: string, ex: any) => {
          const toId = ex?.target_scene;
          if (!toId || !isSafeId(toId)) throw new Error('exit has no valid target scene');
          if (toId === fromId) throw new Error('exit targets its own level');
          const to = readLevel(toId);
          const bw: number = to.map?.width, bh: number = to.map?.height;
          if (!bw || !bh) throw new Error(`level ${toId} has no map size`);
          const dir = ex.direction || 'SOUTH';
          const rdir = EXIT_OPPOSITE[dir];
          if (!rdir) throw new Error(`exit direction '${dir}' is invalid`);
          let gx: number, gy: number;
          if (rdir === 'WEST') { gx = 1; gy = clamp(ex.target_y ?? 1, 1, bh - 2); }
          else if (rdir === 'EAST') { gx = bw - 2; gy = clamp(ex.target_y ?? 1, 1, bh - 2); }
          else if (rdir === 'NORTH') { gy = 1; gx = clamp(ex.target_x ?? 1, 1, bw - 2); }
          else { gy = bh - 2; gx = clamp(ex.target_x ?? 1, 1, bw - 2); }
          const from = readLevel(fromId);
          const fw: number = from.map?.width, fh: number = from.map?.height;
          const dv = EXIT_VEC[dir] || [0, 1];
          const sx = clamp((ex.x ?? 0) - dv[0], 0, (fw || 1) - 1);
          const sy = clamp((ex.y ?? 0) - dv[1], 0, (fh || 1) - 1);
          return {
            x: gx, y: gy, target_scene: fromId,
            target_x: sx, target_y: sy, direction: rdir,
            tile_char: rdir === 'WEST' ? '<' : '>',
          };
        };
        const findReturn = (toLevel: any, fromId: string) =>
          (toLevel.exits || []).find((e: any) => e.target_scene === fromId) || null;

        // ── Two-way tunnels ─────────────────────────────────────────
        // A tunnel is two point exits sharing a `tunnel` id (one in each
        // of two levels, mutual targets, each landing on the other's
        // gate).  The ROM format is unchanged (two plain SceneExit rows);
        // the id lives only in JSON + tooling, which keeps the pair in
        // sync.  Same id shape as scene ids (lowercase, letter-first).
        const TUNNEL_ID_RE = /^[a-z][a-z0-9_]*$/;
        const tunnelIdValid = (t: unknown) =>
          typeof t === 'string' && TUNNEL_ID_RE.test(t);
        const levelFiles = () =>
          fs.readdirSync(path.join(repoRoot, 'levels'))
            .filter((f) => f.endsWith('.json') && f !== 'registry.json');
        const tunnelTakenOnDisk = (tunnel: string): boolean => {
          for (const f of levelFiles()) {
            let data: any;
            try { data = JSON.parse(fs.readFileSync(path.join(repoRoot, 'levels', f), 'utf-8')); }
            catch { continue; }
            for (const e of data.exits || []) {
              if (e && e.tunnel === tunnel) return true;
            }
          }
          return false;
        };
        // Deterministic fresh id for a from->to pair; suffixed on collision.
        const newTunnelId = (fromId: string, toId: string): string => {
          const [a, b] = [fromId, toId].sort();
          const base = `tunnel_${a}_${b}`;
          let cand = base, n = 2;
          while (tunnelTakenOnDisk(cand)) cand = `${base}_${n++}`;
          return cand;
        };
        // Per-tunnel pairing status over a levels map (disk + overrides).
        // Mirrors tools/level_compiler/collision.py tunnel_report().
        const tunnelStatus = (tunnel: string, levels: Record<string, any>) => {
          const ends: Array<{ level: string; index: number; exit: any }> = [];
          for (const name of Object.keys(levels).sort()) {
            (levels[name].exits || []).forEach((e: any, i: number) => {
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
          return { ok: true, error: null as string | null };
        };
        // Full auto-sync after saving level `savedId`: every tunnel mouth
        // in the saved data gets its partner created/updated (target back
        // at the saver, landing on the saver's gate); lone partner rows
        // left behind by a deleted mouth are removed.  Returns counts for
        // the save response.  Throws nothing — partner files that cannot
        // be read are skipped (the compiler gate flags the pair loudly).
        const syncTunnels = (savedId: string, savedData: any) => {
          let synced = 0;
          const removed: string[] = [];
          const savedExits = Array.isArray(savedData.exits) ? savedData.exits : [];
          const live = new Map<string, any>();
          for (const e of savedExits) {
            if (e && typeof e.tunnel === 'string' && e.tunnel && e.target_scene) {
              if (!tunnelIdValid(e.tunnel)) continue;
              live.set(e.tunnel, e);
            }
          }
          for (const [tunnel, e] of live) {
            const toId = e.target_scene;
            if (!isSafeId(toId) || toId === savedId) continue;
            let to: any;
            try { to = readLevel(toId); } catch { continue; }
            to.exits = to.exits || [];
            let partner = to.exits.find((x: any) => x && x.tunnel === tunnel);
            if (!partner) {
              partner = { ...proposeReturn(savedId, e), tunnel };
              to.exits.push(partner);
            }
            partner.target_scene = savedId;
            partner.target_x = e.x;
            partner.target_y = e.y;
            partner.tunnel = tunnel;
            writeJsonAtomic(levelAbs(toId), to);
            synced++;
          }
          // Lone ends elsewhere (partner of a deleted mouth): drop the
          // orphan row.  Count ends disk-wide first (the saved file is
          // already fresh on disk): a healthy pair in two untouched
          // levels counts 2 and is never disturbed; only a true lone row
          // (count 1, outside the saved level) is removed.  Lone ends IN
          // the saved level are left alone — the author may be
          // mid-authoring, and the compiler flags them loudly.
          const counts = new Map<string, number>();
          for (const f of levelFiles()) {
            let data: any;
            try { data = JSON.parse(fs.readFileSync(path.join(repoRoot, 'levels', f), 'utf-8')); }
            catch { continue; }
            for (const x of data.exits || []) {
              if (x && typeof x.tunnel === 'string' && x.tunnel) {
                counts.set(x.tunnel, (counts.get(x.tunnel) || 0) + 1);
              }
            }
          }
          for (const f of levelFiles()) {
            if (f === `${savedId}.json`) continue;
            let data: any;
            try { data = JSON.parse(fs.readFileSync(path.join(repoRoot, 'levels', f), 'utf-8')); }
            catch { continue; }
            const before = (data.exits || []).length;
            data.exits = (data.exits || []).filter((x: any) => {
              if (x && typeof x.tunnel === 'string' && x.tunnel &&
                  (counts.get(x.tunnel) || 0) <= 1) {
                removed.push(`${data.id || f}:${x.tunnel}`);
                return false;
              }
              return true;
            });
            if (data.exits.length !== before) {
              writeJsonAtomic(path.join(repoRoot, 'levels', f), data);
            }
          }
          return { synced, removed };
        };

        if (req.method === 'POST' && req.url === '/api/save-level') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id, category, data, previousId } = JSON.parse(body);
              if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
              if (category === 'screens') {
                // Screens route through SCREEN_ID_TO_PATH only ('title');
                // battle screens save via /api/save-battle-screen.
                const rel = SCREEN_ID_TO_PATH[id];
                if (!rel) throw new Error(`unknown screen '${id}'`);
                const targetPath = path.join(repoRoot, rel);
                fs.writeFileSync(targetPath, JSON.stringify(data, null, 2), 'utf-8');
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true, path: targetPath }));
                return;
              }
              // Real levels: registry-backed, atomic, rename-aware.  This
              // assigns the next dense scene id on first save, preserves the
              // numeric id on rename (rewiring exits), and never reuses ids.
              const sceneId = saveRealLevel(id, previousId ?? null, data);
              // Tunnel auto-sync: partner mouths follow this save
              // (target back here, landing on this gate); orphaned rows of
              // deleted mouths are removed.  Reported, never silent.
              let tunnels_synced = 0;
              let tunnels_removed: string[] = [];
              let tunnel_error: string | null = null;
              try {
                const sync = syncTunnels(id, data);
                tunnels_synced = sync.synced;
                tunnels_removed = sync.removed;
              } catch (err: any) {
                tunnel_error = err.message;
              }
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: levelAbs(id), scene_id: sceneId,
                                       tunnels_synced, tunnels_removed, tunnel_error }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Delete a real level: retire its id (never reused), clear every
        // exit that targeted it, unlink the file.  Engine-wired scenes are
        // refused (their MAP_/SCENE_ symbols live in hand-written C).
        if (req.method === 'POST' && req.url === '/api/delete-level') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id } = JSON.parse(body);
              const result = deleteRealLevel(id);
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, ...result }));
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Clean retired orphans: remove every levels/<id>.json whose id is
        // tombstoned in the registry (a delete whose unlink did not stick,
        // e.g. an interrupted editor delete or a restored tracked file).
        // Such a file hard-fails the next compile and keeps its actor ids
        // reserved, so this is a one-click repair.
        if (req.method === 'POST' && req.url === '/api/clean-retired-orphans') {
          try {
            const retired = readRegistry()._retired || {};
            const removed: string[] = [];
            for (const sid of Object.keys(retired)) {
              if (!isSafeId(sid)) continue;   // defensive
              const p = levelAbs(sid);
              if (fs.existsSync(p)) {
                fs.unlinkSync(p);
                removed.push(sid);
              }
            }
            sendJson({ success: true, removed });
          } catch (err: any) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: false, error: err.message }));
          }
          return;
        }

        // Preview: for each exit of `from_id`, does the target already
        // have a return, and if not, what would we create?  Tunnel pairing
        // state rides along so the editor can show 🔗 vs one-way per exit.
        if (req.method === 'POST' && req.url === '/api/exit-status') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { from_id, exits } = JSON.parse(body);
              if (!isSafeId(from_id)) throw new Error(`invalid from_id '${from_id}'`);
              const list = Array.isArray(exits) ? exits : [];
              // Disk levels with the from-level overridden by the editor's
              // (possibly unsaved) exits, so tunnel status reflects what a
              // save would produce.
              const levels: Record<string, any> = {};
              for (const f of levelFiles()) {
                try {
                  const data = JSON.parse(
                    fs.readFileSync(path.join(repoRoot, 'levels', f), 'utf-8'));
                  if (data && data.id) levels[data.id] = data;
                } catch { /* skip unreadable */ }
              }
              levels[from_id] = { ...(levels[from_id] || {}), exits: list };
              const items = list.map((ex: any, i: number) => {
                const tunnel = (ex && typeof ex.tunnel === 'string' && ex.tunnel)
                  ? ex.tunnel : null;
                let tunnel_ok: boolean | null = null;
                let tunnel_error: string | null = null;
                if (tunnel) {
                  if (!tunnelIdValid(tunnel)) {
                    tunnel_error = `tunnel id '${tunnel}' is invalid (lowercase, letter-first)`;
                  } else {
                    const st = tunnelStatus(tunnel, levels);
                    tunnel_ok = st.ok;
                    tunnel_error = st.error;
                  }
                }
                try {
                  const to = readLevel(ex.target_scene);
                  const ret = findReturn(to, from_id);
                  return { index: i, target: ex.target_scene, has_return: !!ret,
                           return_exit: ret, proposal: ret ? null : proposeReturn(from_id, ex),
                           error: null, tunnel, tunnel_ok, tunnel_error,
                           suggested_tunnel: tunnel || newTunnelId(from_id, ex.target_scene) };
                } catch (e: any) {
                  return { index: i, target: ex?.target_scene || '?', has_return: false,
                           return_exit: null, proposal: null, error: e.message,
                           tunnel, tunnel_ok, tunnel_error, suggested_tunnel: null };
                }
              });
              sendJson({ success: true, items });
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Assign: upsert the exit into the from-level and create the
        // reciprocal in the target level when missing.  Both files are
        // written atomically; existing returns are never duplicated.
        // With as_tunnel, both ends share one tunnel id (adopting a legacy
        // return when present) with spawn-tracks-gate landings, and the
        // updated from-level exits come back so editor state cannot go
        // stale (a later save would otherwise overwrite the pairing).
        if (req.method === 'POST' && req.url === '/api/connect-levels') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { from_id, exit, as_tunnel, tunnel } = JSON.parse(body);
              if (!isSafeId(from_id)) throw new Error(`invalid from_id '${from_id}'`);
              const toId = exit.target_scene;
              if (!isSafeId(toId)) throw new Error('exit has no valid target scene');
              if (toId === from_id) throw new Error('exit targets its own level');
              const a = readLevel(from_id);
              a.exits = a.exits || [];
              let tunnelId: string | null = null;
              if (as_tunnel) {
                tunnelId = (typeof tunnel === 'string' && tunnel && tunnelIdValid(tunnel) &&
                            !tunnelTakenOnDisk(tunnel))
                  ? tunnel : newTunnelId(from_id, toId);
                exit.tunnel = tunnelId;
                // This mouth lands on the partner gate: adopt the existing
                // return's gate when present, else the fresh proposal's.
                const toPeek = readLevel(toId);
                const legacy = (toPeek.exits || []).find((e: any) =>
                  e && (e.tunnel === tunnelId || e.target_scene === from_id));
                if (legacy) {
                  exit.target_x = legacy.x;
                  exit.target_y = legacy.y;
                } else {
                  const prop = proposeReturn(from_id, exit);
                  exit.target_x = prop.x;
                  exit.target_y = prop.y;
                }
              }
              const idx = a.exits.findIndex((e: any) =>
                e.x === exit.x && e.y === exit.y && e.target_scene === exit.target_scene);
              if (idx >= 0) a.exits[idx] = exit; else a.exits.push(exit);
              writeJsonAtomic(levelAbs(from_id), a);

              const to = readLevel(toId);
              to.exits = to.exits || [];
              let toExit = tunnelId
                ? to.exits.find((e: any) => e && e.tunnel === tunnelId) || findReturn(to, from_id)
                : findReturn(to, from_id);
              let created = false;
              if (!toExit) {
                toExit = tunnelId
                  ? { ...proposeReturn(from_id, exit), tunnel: tunnelId }
                  : proposeReturn(from_id, exit);
                to.exits.push(toExit);
                created = true;
              }
              if (tunnelId) {
                // Adopt a legacy return into the tunnel: same id, mutual
                // target, landing on this mouth.
                toExit.tunnel = tunnelId;
                toExit.target_scene = from_id;
                toExit.target_x = exit.x;
                toExit.target_y = exit.y;
              }
              writeJsonAtomic(levelAbs(toId), to);
              sendJson({ success: true, from_exit: exit, from_exits: a.exits,
                         to_exit: toExit, created, tunnel: tunnelId });
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Unlink a tunnel: strip the id from every mouth (both rows stay
        // as independent one-way exits).  Deleting is different — that
        // goes through save-time sync, which removes the orphaned row.
        if (req.method === 'POST' && req.url === '/api/tunnel-unlink') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { tunnel } = JSON.parse(body);
              if (!tunnelIdValid(tunnel)) throw new Error(`invalid tunnel id '${tunnel}'`);
              let clearedFiles = 0, clearedExits = 0;
              for (const f of levelFiles()) {
                const abs = path.join(repoRoot, 'levels', f);
                let data: any;
                try { data = JSON.parse(fs.readFileSync(abs, 'utf-8')); }
                catch { continue; }
                let touched = false;
                for (const e of data.exits || []) {
                  if (e && e.tunnel === tunnel) {
                    delete e.tunnel;
                    touched = true;
                    clearedExits++;
                  }
                }
                if (touched) {
                  writeJsonAtomic(abs, data);
                  clearedFiles++;
                }
              }
              sendJson({ success: true, cleared: clearedFiles, exits: clearedExits });
            } catch (err: any) {
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        // Edge-neighbor pairing check (one source of truth: collision.py
        // via validate.py --neighbor-check). The editor's current level
        // data is passed as an override so unsaved links/terrain are
        // checked; the other levels come from disk.
        if (req.method === 'POST' && req.url === '/api/neighbor-check') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            let tmpDir: string | null = null;
            try {
              const { from_id, data } = JSON.parse(body);
              if (!isSafeId(from_id)) throw new Error(`invalid from_id '${from_id}'`);
              tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'kh-nbr-'));
              const overridePath = path.join(tmpDir, 'override.json');
              fs.writeFileSync(overridePath, JSON.stringify({ from_id, data }));
              const cmd = `python3 tools/level_compiler/validate.py --neighbor-check ${JSON.stringify(overridePath)}`;
              runInToolchain(cmd, ((err: any, stdout: string, stderr: string) => {
                if (tmpDir) { try { fs.rmSync(tmpDir, { recursive: true, force: true }); } catch { /* ignore */ } }
                if (err) {
                  res.writeHead(200, { 'Content-Type': 'application/json' });
                  res.end(JSON.stringify({ success: false, error: (stderr || err.message || '').trim() }));
                  return;
                }
                const line = (stdout || '').split('\n').reverse().find((l) => l.trim().startsWith('{'));
                if (!line) {
                  res.writeHead(200, { 'Content-Type': 'application/json' });
                  res.end(JSON.stringify({ success: false, error: 'neighbor check produced no JSON' }));
                  return;
                }
                sendJson(JSON.parse(line));
              }) as any);
            } catch (err: any) {
              if (tmpDir) { try { fs.rmSync(tmpDir, { recursive: true, force: true }); } catch { /* ignore */ } }
              res.writeHead(500, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        if (req.method === 'POST' && req.url === '/api/save-tileset') {
          let body = '';
          req.on('data', chunk => { body += chunk; });
          req.on('end', () => {
            try {
              const { id, data, images } = JSON.parse(body);
              if (!isSafeId(id)) throw new Error(`invalid id '${id}'`);
              const targetPath = path.join(repoRoot, 'tools', 'level_editor', 'tilesets', `${id}.json`);
              fs.mkdirSync(path.dirname(targetPath), { recursive: true });
              fs.writeFileSync(targetPath, JSON.stringify(data, null, 2), 'utf-8');

              if (images && typeof images === 'object') {
                const tilesDir = path.join(repoRoot, 'tools', 'level_editor', 'public', 'tiles', id);
                fs.mkdirSync(tilesDir, { recursive: true });
                for (const [tileId, base64Data] of Object.entries(images)) {
                  if (typeof base64Data === 'string' && base64Data.startsWith('data:image/')) {
                    const base64Content = base64Data.split(',')[1];
                    if (base64Content) {
                      const imgBuffer = Buffer.from(base64Content, 'base64');
                      fs.writeFileSync(path.join(tilesDir, `${tileId}.png`), imgBuffer);
                    }
                  }
                }
              }

              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, path: targetPath }));
            } catch (err: any) {
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: err.message }));
            }
          });
          return;
        }

        function runInToolchain(cmd: string, callback: (err: any, stdout: string, stderr: string) => void) {
          const nixCmd = `${nixBin} develop --command bash --norc -c ${JSON.stringify(cmd)}`;
          const attempts: { route: string; cmd: string; error?: string; stderr?: string }[] = [];
          const finish = (err: any, stdout: string, stderr: string, route: string) => {
            if (err) attempts.push({ route, cmd: route === 'direct' ? cmd : nixCmd, error: err.message, stderr });
            (callback as any)(err, stdout, stderr, attempts);
          };
          const runNix = (cb: (err: any, stdout: string, stderr: string) => void) => {
            exec(nixCmd, { cwd: repoRoot, maxBuffer: 10 * 1024 * 1024 }, (err, stdout, stderr) => cb(err, stdout, stderr));
          };
          const runDirect = (cb: (err: any, stdout: string, stderr: string) => void) => {
            exec(cmd, { cwd: repoRoot, maxBuffer: 10 * 1024 * 1024 }, (err, stdout, stderr) => cb(err, stdout, stderr));
          };
          if (toolchainProbe.ok) {
            // Direct tools verified: run direct; on failure retry once via
            // nix (host env quirks) and report BOTH attempts explicitly.
            runDirect((err, stdout, stderr) => {
              if (!err) return callback(err, stdout, stderr);
              attempts.push({ route: 'direct', cmd, error: err.message, stderr });
              runNix((err2, stdout2, stderr2) => finish(err2, stdout2, stderr2, 'nix-develop'));
            });
          } else if (nixBin) {
            // Toolchain incomplete: direct execution is known-broken, so do
            // not attempt it (it only produces cryptic late failures).
            runNix((err, stdout, stderr) => finish(err, stdout, stderr, 'nix-develop'));
          } else {
            const err: any = new Error(
              `ROM toolchain incomplete (${Object.entries(toolchainProbe.detail).filter(([, v]) => v !== 'runs').map(([k]) => k).join(', ')}) ` +
              `and no 'nix' binary found. Run the editor from inside \`nix develop\` (AGENTS.md section 1).`);
            callback(err, '', '');
          }
        }

        if (req.method === 'POST' && req.url === '/api/compile-rom') {
          // CLEAN build: the ROM objects have no fine-grained header
          // dependency graph (Makefile tracks .c -> .o only), so an
          // incremental link after pulling commits can pair stale objects
          // with a drifted struct layout and produce a subtly broken ROM
          // (AGENTS.md 52.2).  `make clean` plus the header safety-net
          // makes every Compile-ROM click a known-good full build.
          // Both ROMs in ONE make instance with parallel jobs: the shared
          // prerequisites (generated C, crt0.o, gb_lite/sm83_lite) are
          // built exactly once even with -j, the object sets are disjoint
          // (build/*.o vs build/debug/*.o), and the two link steps write
          // disjoint outputs.  Two SEPARATE make processes would race on
          // the shared lite libs -- never split this into parallel execs.
          runInToolchain('make clean && python3 tools/level_compiler/compile.py --all -o src/game/scenes_content.c && python3 tools/screen_compiler/title_compile.py -o src/game/title_data.c screens/title.json && python3 tools/screen_compiler/battle_compile.py --all -o src/game/ && python3 tools/screen_compiler/dialogue_compile.py --all -o src/game/ && python3 tools/screen_compiler/tutorial_compile.py --all -o src/screens/ && make debug release -j$(nproc 2>/dev/null || echo 4) && make sfx-preview', ((err: any, stdout: string, stderr: string, attempts: any[]) => {
            if (err) {
              console.error('Compile error:', err.message);
              if (stderr) console.error('Compile stderr:\n' + stderr);
              if (stdout) console.error('Compile stdout:\n' + stdout);
              const combinedError = [stderr, stdout, err.message].filter(Boolean).join('\n\n');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: combinedError || err.message, log: stdout, attempts: attempts || [] }));
            } else {
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: true, log: stdout, romPath: ['build/kaartenheld_debug.gb', 'build/kaartenheld.gb'] }));
            }
          }) as any);
          return;
        }

        if (req.method === 'POST' && req.url === '/api/run-game') {
          // Run Game always plays the RELEASE ROM (the debug ROM's
          // per-frame harness work makes it feel sluggish in play).
          const romPath = path.join(repoRoot, 'build', 'kaartenheld.gb');
          const contentDirs = ['levels', 'screens',
            path.join('tools', 'level_editor', 'tilesets')];

          // Staleness probe: content JSON newer than the release ROM means
          // the build is stale; a missing ROM must be built, not launched.
          const findStale = (): string[] => {
            try {
              const romMtime = fs.statSync(romPath).mtimeMs;
              const fresh: string[] = [];
              const scan = (dir: string) => {
                for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
                  const p = path.join(dir, e.name);
                  if (e.isDirectory()) { scan(p); continue; }
                  if (!e.name.endsWith('.json')) continue;
                  try {
                    if (fs.statSync(p).mtimeMs > romMtime) fresh.push(path.relative(repoRoot, p));
                  } catch { /* raced deletion: ignore */ }
                }
              };
              for (const d of contentDirs) {
                try { scan(path.join(repoRoot, d)); } catch { /* missing dir: ignore */ }
              }
              return fresh.sort().slice(0, 8);
            } catch {
              return ['<rom missing: building release>'];
            }
          };

          const launch = () => {
            runInToolchain('which sameboy mgba pyboy 2>&1', (err, stdout) => {
              const lines = stdout.split('\n').map(l => l.trim()).filter(l => l && !l.includes('no ') && !l.includes('warning'));
              const emu = lines[0] || 'pyboy';
              const hasDirectTools = (() => {
                try {
                  execSync(`${emu} --help 2>&1`, { stdio: 'ignore' });
                  return true;
                } catch {
                  return false;
                }
              })();

              try {
                const child = hasDirectTools
                  ? spawn(emu, [romPath], { cwd: repoRoot, detached: true, stdio: 'ignore' })
                  : spawn(getNixBin() || 'nix', ['develop', '--command', emu, romPath], { cwd: repoRoot, detached: true, stdio: 'ignore' });

                child.unref();
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true, emulator: path.basename(emu), message: `Launched ${path.basename(emu)} on desktop (release ROM)`, stale: [] }));
              } catch (spawnErr: any) {
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: false, error: spawnErr.message }));
              }
            });
          };

          if (findStale().length === 0) {
            launch();
            return;
          }
          // Missing or stale release ROM: build it first, then launch.
          runInToolchain('make release', ((err: any, stdout: string, stderr: string) => {
            if (err || !fs.existsSync(romPath)) {
              console.error('Release build error:', err && err.message);
              if (stderr) console.error('Release stderr:\n' + stderr);
              const combinedError = [stderr, stdout, err && err.message].filter(Boolean).join('\n\n');
              res.writeHead(200, { 'Content-Type': 'application/json' });
              res.end(JSON.stringify({ success: false, error: combinedError || 'release ROM missing and rebuild failed' }));
              return;
            }
            launch();
          }) as any);
          return;
        }

        if (req.method === 'GET' && req.url === '/api/rom') {
          const romPath = path.join(repoRoot, 'build', 'kaartenheld_debug.gb');
          if (fs.existsSync(romPath)) {
            const data = fs.readFileSync(romPath);
            res.writeHead(200, {
              'Content-Type': 'application/octet-stream',
              'Content-Length': data.length,
              'Content-Disposition': 'inline; filename="kaartenheld_debug.gb"'
            });
            res.end(data);
          } else {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'ROM not built yet. Click Compile ROM first!' }));
          }
          return;
        }

        next();
      });
    }
  };
}

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react(), levelEditorApiPlugin()],
  server: {
    port: 3000
  }
});
