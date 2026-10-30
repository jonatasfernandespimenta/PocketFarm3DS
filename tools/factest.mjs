// Differential test of the factory sim: random factories run through the web's factory.js and through
// `build/pf --factory`, final states must match. node tools/factest.mjs [cases=200] [steps=400]
import { execFileSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
const web = n => import(pathToFileURL(`../PocketFarmWeb/src/${n}.js`).href);
const { MACHINES } = await web('data');
const { newState } = await web('state');
const { step, machineAt, currentTick } = await web('factory');
const { rng } = await web('systems');

const cases = +(process.argv[2] || 200), steps = +(process.argv[3] || 400);
const TYPES = Object.keys(MACHINES), DIRS = ['up', 'down', 'left', 'right'];
const ITEMS = ['stone', 'coal', 'copper_ore', 'iron_ore', 'gold_ore', 'milk', 'egg', 'wheat', 'carrot'];
const sorted = v => Array.isArray(v) ? v.map(sorted) : v && typeof v === 'object'
  ? Object.fromEntries(Object.keys(v).filter(k => v[k] !== undefined).sort().map(k => [k, sorted(v[k])])) : v;

let fail = 0;
for (let c = 1; c <= cases; c++) {
  const R = rng(c), pick = a => a[Math.floor(R() * a.length)];
  const s = newState();
  s.time = R() < 0.5 ? 10 * 60 : 22 * 60;   // day and night (solar)
  // belts/arms/splitters are the common case: weight them up so items actually travel
  const pool = [...TYPES, 'belt', 'belt', 'belt', 'belt', 'arm', 'arm', 'splitter', 'tunnel', 'tunnel', 'autosell', 'pole'];
  for (let i = 0; i < 25 + R() * 40; i++) {
    const type = pick(pool), x = Math.floor(R() * 10), y = Math.floor(R() * 10), n = MACHINES[type].size;
    let free = true;
    for (let dy = 0; dy < n; dy++) for (let dx = 0; dx < n; dx++) if (machineAt(s, x + dx, y + dy)) free = false;
    if (!free) continue;
    const m = { id: i, type, x, y, dir: pick(DIRS), input: {}, out: [], fuel: 0, progress: 0, cur: null, item: null };
    if (MACHINES[type].fuel) m.fuel = Math.floor(R() * 4);
    if (MACHINES[type].recipes) for (const id of Object.keys(MACHINES[type].recipes)) if (R() < 0.5) m.input[id] = 1 + Math.floor(R() * 3);
    if (type === 'belt' || type === 'splitter' || type === 'tunnel') m.item = R() < 0.6 ? pick(ITEMS) : null;
    if (type === 'chest' && R() < 0.5) m.out = [pick(ITEMS), pick(ITEMS)];
    s.machines.push(m);
  }
  const file = `build/factest-${c}.json`;
  writeFileSync(file, JSON.stringify(s));
  const js = structuredClone(s), rand = rng(42), t0 = currentTick();   // JS tick is module-global
  let sold = 0, items = 0;
  for (let i = 0; i < steps; i++) { const r = step(js, rand); sold += r.sold; items += r.items; }
  js._sold = sold; js._items = items;
  for (const m of js.machines) if (m.at > t0) m.at -= t0;
  const cs = JSON.parse(execFileSync('build/pf', ['--factory', file, String(steps), '42'], { maxBuffer: 1 << 26 }));
  delete cs._tick;
  const a = JSON.stringify(sorted(js)), b = JSON.stringify(sorted(cs));
  if (a !== b) {
    fail++;
    let i = 0; while (a[i] === b[i]) i++;
    console.log(`case ${c} differs (${file}):\n  js: …${a.slice(Math.max(0, i - 120), i + 80)}\n  c:  …${b.slice(Math.max(0, i - 120), i + 80)}`);
    if (fail >= 3) break;
  }
}
console.log(fail ? `${fail} failing` : `ok: ${cases} factories × ${steps} steps identical`);
process.exit(fail ? 1 : 0);
