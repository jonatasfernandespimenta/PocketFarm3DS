// Writes a mid-game save (summer, ripe field, animals, a small factory) for screenshots / the store icon.
// node tools/showcase.mjs <out.json> [tileX tileY hour]
import { writeFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
const web = n => import(pathToFileURL(`../PocketFarmWeb/src/${n}.js`).href);
const { newState } = await web('state');
const map = (await import(pathToFileURL('../PocketFarmWeb/assets/maps/farm.json').href, { with: { type: 'json' } })).default;

const s = newState();
Object.assign(s, { day: 20, time: (+process.argv[5] || 10.5) * 60, money: 12840, energy: 60, tutorialDone: true, px: (+process.argv[3] || 24) * 16 + 8, py: (+process.argv[4] || 19) * 16 + 8, map: 'farm',
  tickets: 2, tokens: 7, farmName: 'Sunny Acres' });
s.tools = { hoe: 2, can: 2, axe: 2, pickaxe: 2, rod: 1 };
// every quest done but the festival, so no coach/guide popups get in the way
for (const id of ['intro', 'carrots', 'fish', 'wood', 'decorate', 'project', 'mine', 'smelt', 'friend', 'coop', 'machine', 'line', 'power', 'logistics', 'trail', 'forest']) s.quests.done[id] = true;
s.buildings = { coop: true, barn: true };
s.animals = [
  { id: 1, kind: 'chicken', name: 'Pip', happy: 90, fed: true, product: false, age: 9, petDay: 35 },
  { id: 2, kind: 'chicken', name: 'Nugget', happy: 80, fed: true, product: false, age: 6, petDay: 0 },
  { id: 3, kind: 'cow', name: 'Daisy', happy: 85, fed: true, product: false, age: 12, petDay: 0 },
  { id: 4, kind: 'sheep', name: 'Cloud', happy: 70, fed: true, product: false, age: 4, petDay: 0 },
];
s.critters = { 1: 1, 2: 2, 5: 1, 9: 1, 14: 1 }; s.mascot = 5;
s.inv = { seed_tomato: 12, seed_corn: 8, wheat: 14, tomato: 9, egg: 6, milk: 3, wood: 64, stone: 48, copper_bar: 6, iron_bar: 4, coal: 20 };
// field: ripe summer crops, half watered
const crops = ['tomato', 'corn', 'wheat'], field = { x0: 21, y0: 16, w: 8, h: 3 };
for (let y = field.y0; y < field.y0 + field.h; y++) for (let x = field.x0; x < field.x0 + field.w; x++)
  s.soil[`${x},${y}`] = { wet: (x + y) % 2 === 0, crop: crops[y - field.y0], age: 9 };
// clear the decoration props under the field and the factory
const clear = (x0, y0, w, h) => map.props.filter(p => p.x >= x0 && p.x < x0 + w && p.y >= y0 && p.y < y0 + h).map(p => p.id);
s.removed.farm = [...clear(field.x0, field.y0, field.w, field.h), ...clear(11, 19, 9, 5)];
// factory: solar → miner → belts → auto-sell box
const M = (id, type, x, y, dir, extra = {}) => ({ id, type, x, y, dir, input: {}, out: [], fuel: 0, progress: 0, cur: null, item: null, ...extra });
s.machines = [
  M(101, 'solar', 12, 19, 'right'),
  M(102, 'miner', 12, 21, 'right', { progress: 6 }),
  M(103, 'belt', 14, 22, 'right', { item: 'stone' }),
  M(104, 'belt', 15, 22, 'right'),
  M(105, 'belt', 16, 22, 'right', { item: 'coal' }),
  M(106, 'autosell', 17, 22, 'right'),
  M(107, 'sprinkler', 25, 19, 'down'),
];
writeFileSync(process.argv[2], JSON.stringify(s));
