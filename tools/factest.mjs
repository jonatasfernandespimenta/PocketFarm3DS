// Differential test of the factory sim: random factories run through the web's factory.js and through
// `build/pf --factory`, final states must match. node tools/factest.mjs [cases=200] [steps=400]
import { execFileSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
const web = n => import(pathToFileURL(`../PocketFarmWeb/src/${n}.js`).href);
const { MACHINES, CROPS } = await web('data');
const { newState } = await web('state');
const { step, machineAt, currentTick, rows, AGING, storeFor, newDrone } = await web('factory');
const { rng } = await web('systems');

const cases = +(process.argv[2] || 200), steps = +(process.argv[3] || 400);
const TYPES = Object.keys(MACHINES), DIRS = ['up', 'down', 'left', 'right'];
const ITEMS = ['stone', 'coal', 'copper_ore', 'iron_ore', 'gold_ore', 'milk', 'egg', 'wheat', 'carrot',
  // warehouse / food-line cargo: one or more for each store (silo, cold, cellar, shed) and the cellar's aging
  'flour', 'corn', 'seed_carrot', 'grape', 'fish_lambari', 'cheese', 'wine', 'bottled_must', 'fresh_cheese', 'kneaded', 'feed', 'wool', 'tomato'];
const CROP_IDS = Object.keys(CROPS);
const BIN = process.env.PF || 'build/pf';   // a harness built elsewhere (e.g. while game.c is mid-edit)
const sorted = v => Array.isArray(v) ? v.map(sorted) : v && typeof v === 'object'
  ? Object.fromEntries(Object.keys(v).filter(k => v[k] !== undefined).sort().map(k => [k, sorted(v[k])])) : v;

let fail = 0;
for (let c = 1; c <= cases; c++) {
  const R = rng(c), pick = a => a[Math.floor(R() * a.length)];
  const s = newState();
  s.time = R() < 0.5 ? 10 * 60 : 22 * 60;   // day and night (solar)
  // belts/arms/splitters are the common case: weight them up so items actually travel
  const pool = [...TYPES, 'belt', 'belt', 'belt', 'belt', 'arm', 'arm', 'splitter', 'tunnel', 'tunnel', 'autosell', 'pole'];
  // half the cases use a bigger grid with the industrial pieces weighted up (warehouses, sorter, dock, vines…)
  const big = R() < 0.5, W = big ? 16 : 10;
  if (big) pool.push('silo', 'cold', 'cellar', 'shed', 'sorter', 'sorter', 'sorter', 'dock', 'dock', 'vine', 'feeder', 'collector', 'solar', 'emill', 'bottler', 'cheesemaker');
  // motifs: small hand-shaped lines so the new rules actually fire (random fill rarely lines up a warehouse mouth)
  let uid = 1000;
  const put = (type, x, y, dir, extra = {}) => {
    for (let dy = 0; dy < rows(type); dy++) for (let dx = 0; dx < MACHINES[type].size; dx++) if (x + dx < 0 || y + dy < 0 || machineAt(s, x + dx, y + dy)) return null;
    const m = { id: uid++, type, x, y, dir, input: {}, out: [], fuel: 0, progress: 0, cur: null, item: null, ...extra };
    s.machines.push(m);
    return m;
  };
  const STORES = ['silo', 'cold', 'cellar', 'shed'];
  for (let k = big ? 2 + Math.floor(R() * 5) : 0; k > 0; k--) {
    const x = Math.floor(R() * (W - 4)) + 1, y = Math.floor(R() * (W - 4)) + 1, kind = Math.floor(R() * 5);
    if (kind === 0) {   // sorter with a feed lane, both exits and straight-on
      const f = pick(ITEMS);
      put('belt', x - 1, y, 'right', { item: R() < 0.5 ? f : pick(ITEMS) });
      put('sorter', x, y, 'right', { item: R() < 0.5 ? f : pick(ITEMS), filter: R() < 0.9 ? f : undefined, side: pick(['left', 'right', undefined]) });
      put('belt', x, y - 1, 'up'); put('belt', x, y + 1, 'down'); put('belt', x + 1, y, 'right');
      put(pick(['chest', 'autosell']), x, y - 2, 'up'); put('chest', x, y + 2, 'up');
    } else if (kind === 1) {   // warehouse: belt into its mouth (or a wrong tile), arm out of its grey port into a chest
      const T = pick(STORES), n = MACHINES[T].size, h = rows(T), fit = ITEMS.filter(i => storeFor(i) === T);
      const w = put(T, x + 1, y, 'down', { stock: R() < 0.5 ? { [pick(fit.length ? fit : ITEMS)]: 2 } : {}, outFilter: R() < 0.3 ? pick(ITEMS) : undefined });
      if (!w) continue;
      const my = R() < 0.8 ? y + h - 1 : y;
      put('belt', x, my, 'right', { item: fit.length && R() < 0.8 ? pick(fit) : pick(ITEMS) });
      put('arm', x - 1, my, 'right', { item: fit.length ? pick(fit) : null });
      put('chest', x - 2, my, 'right', { out: fit.length ? [pick(fit), pick(fit)] : [] });
      put('arm', x + 1 + n, y + h - 1, 'right'); put(pick(['chest', 'belt', 'autosell']), x + 2 + n, y + h - 1, 'right');
    } else if (kind === 2) {   // feeder fed by belt and by arm
      put('belt', x - 1, y, 'right', { item: 'feed' }); put('feeder', x, y, 'right', { feed: Math.floor(R() * 62) });
      put('chest', x - 1, y + 1, 'right', { out: ['feed', 'feed', pick(ITEMS)] }); put('arm', x, y + 1, 'right'); put('feeder', x + 1, y + 1, 'right');
    } else if (kind === 3) {   // powered dock + vines and ripe crops
      put('solar', x - 1, y - 1, 'down'); put('steam', x + 1, y - 2, 'down', { fuel: 3 });
      const d = put('dock', x, y + 1, pick(DIRS));
      if (d) {
        d.drones = [newDrone(d), newDrone(d)];
        d.drones[0].target = 'grape'; d.drones[1].target = pick(CROP_IDS);
        put('belt', ...(d.dir === 'right' ? [x + 2, y + 2] : d.dir === 'down' ? [x + 1, y + 3] : d.dir === 'left' ? [x - 1, y + 2] : [x, y]), d.dir);
      }
      put('vine', x + 3, y + 3, 'down', { st: { age: 9, fruit: R() < 0.8, wait: 0 } });
      put('vine', x - 2, y + 4, 'down', { st: { age: 9, fruit: true, wait: 0 } });
    } else {   // collector into a belt; cellar fed with bottles
      put('collector', x, y, 'right', { out: [pick(ITEMS), 'egg', 'milk'] }); put('belt', x + 1, y, 'right');
      const c = put('cellar', x, y + 1, 'down', { stock: {}, aging: [{ id: 'bottled_must', nights: 2 }] });
      if (c) put('belt', x - 1, y + 4, 'right', { item: pick(['bottled_must', 'fresh_cheese', 'cheese']) });
    }
  }
  for (const m of s.machines) for (const k of Object.keys(m)) if (m[k] === undefined) delete m[k];
  for (let i = 0; i < (big ? 50 + R() * 50 : 25 + R() * 40); i++) {
    const type = pick(pool), x = Math.floor(R() * W), y = Math.floor(R() * W), n = MACHINES[type].size, h = rows(type);
    let free = true;
    for (let dy = 0; dy < h; dy++) for (let dx = 0; dx < n; dx++) if (machineAt(s, x + dx, y + dy)) free = false;
    if (!free) continue;
    const m = { id: i, type, x, y, dir: pick(DIRS), input: {}, out: [], fuel: 0, progress: 0, cur: null, item: null };
    if (MACHINES[type].fuel) m.fuel = Math.floor(R() * 4);
    if (MACHINES[type].recipes) {
      for (const id of Object.keys(MACHINES[type].recipes)) if (R() < 0.5) m.input[id] = 1 + Math.floor(R() * 3);
      // stale recipe (moved to another machine): handed back. Never the fuel id: insert() never queues fuel as input,
      // and the JS would then start it as a recipe and throw.
      if (R() < 0.1) { const id = pick(ITEMS); if (id !== MACHINES[type].fuel) m.input[id] = 1 + Math.floor(R() * 2); }
      if (R() < 0.1) { m.cur = pick(ITEMS); m.progress = 1; }
    }
    if (type === 'belt' || type === 'splitter' || type === 'tunnel' || type === 'sorter') m.item = R() < 0.6 ? pick(ITEMS) : null;
    if (type === 'sorter') { if (R() < 0.8) m.filter = pick(ITEMS); const sd = R(); if (sd < 0.4) m.side = 'right'; else if (sd < 0.7) m.side = 'left'; }
    if (type === 'chest' && R() < 0.5) m.out = [pick(ITEMS), pick(ITEMS)];
    if (MACHINES[type].store) {
      m.stock = {};
      for (let k = Math.floor(R() * 4); k > 0; k--) { const id = pick(ITEMS); m.stock[id] = (m.stock[id] || 0) + 1 + Math.floor(R() * 5); }
      if (R() < 0.15) m.stock[pick(ITEMS)] = MACHINES[type].cap;   // a full one
      if (R() < 0.3) m.outFilter = pick(ITEMS);
      if (type === 'cellar' && R() < 0.6) m.aging = Array.from({ length: 1 + Math.floor(R() * 3) }, () => ({ id: pick(Object.keys(AGING)), nights: Math.floor(R() * 3) }));
    }
    if (type === 'feeder') m.feed = Math.floor(R() * 62);
    if (type === 'collector') m.out = Array.from({ length: Math.floor(R() * 5) }, () => pick(ITEMS));
    if (type === 'vine') m.st = { age: Math.floor(R() * 10), fruit: R() < 0.5, wait: Math.floor(R() * 4) };
    if (MACHINES[type].station && R() < 0.5) m.st = { queue: { [pick(ITEMS)]: 1 + Math.floor(R() * 3) } };
    if (MACHINES[type].dock && R() < 0.9) {
      m.drones = Array.from({ length: 1 + Math.floor(R() * 3) }, () => {
        const d = { target: R() < 0.8 ? pick([...CROP_IDS, 'grape', 'grape']) : null, x: x + Math.floor(R() * 3) / 2, y: y + Math.floor(R() * 3) / 2,
          carry: Array.from({ length: R() < 0.3 ? Math.floor(R() * 5) : 0 }, () => pick(CROP_IDS)), state: pick(['home', 'home', 'out', 'return']), claim: null };
        if (R() < 0.1) d.claim = '1,1';            // an old save's claim without coordinates
        return d;
      });
      if (R() < 0.15) m.out = Array.from({ length: 40 }, () => 'carrot');   // full dock: drones wait
    }
    s.machines.push(m);
  }
  // crops for the drones: some ripe, some not, a few under pieces (drones fly over everything)
  for (let k = Math.floor(R() * (big ? 30 : 6)); k > 0; k--) {
    const crop = pick(CROP_IDS);
    s.soil[`${Math.floor(R() * W)},${Math.floor(R() * W)}`] = { wet: R() < 0.5, crop, age: R() < 0.7 ? CROPS[crop].days + Math.floor(R() * 2) : Math.floor(R() * CROPS[crop].days) };
  }
  const file = `build/factest-${c}.json`;
  writeFileSync(file, JSON.stringify(s));
  const js = structuredClone(s), rand = rng(42), t0 = currentTick();   // JS tick is module-global
  let sold = 0, items = 0;
  for (let i = 0; i < steps; i++) { const r = step(js, rand); sold += r.sold; items += r.items; }
  js._sold = sold; js._items = items;
  for (const m of js.machines) if (m.at > t0) m.at -= t0;
  const cs = JSON.parse(execFileSync(BIN, ['--factory', file, String(steps), '42'], { maxBuffer: 1 << 26 }));
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
