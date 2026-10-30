// Writes every exported table of the web game as JSON (functions dropped), for the C port to read at runtime.
//   node tools/dump.mjs <PocketFarmWeb> <out.json>
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
const [root, out] = process.argv.slice(2);
const mods = ['data', 'world', 'quests', 'tutorial', 'workshop', 'factory', 'trade', 'cooking'];
const all = {};
for (const f of mods) {
  const m = await import(resolve(root, 'src', f + '.js'));
  for (const [k, v] of Object.entries(m)) if (typeof v !== 'function') all[k] = v;
}
// sprites.js keeps its pixel maps private: evaluate the file to get them
const spr = readFileSync(resolve(root, 'src', 'sprites.js'), 'utf8').replace(/export /g, '');
Object.assign(all, new Function(spr + '; return { SPRITE_PAL: PAL, SPRITES: MAPS };')());
// English build: swap display strings (values only, never keys) using tools/en.json
const en = JSON.parse(readFileSync(new URL('./en.json', import.meta.url), 'utf8'));
const tr = v => typeof v === 'string' ? (Object.hasOwn(en, v) ? en[v] : v)
  : Array.isArray(v) ? v.map(tr)
  : v && typeof v === 'object' ? Object.fromEntries(Object.entries(v).map(([k, x]) => [k, tr(x)])) : v;
for (const k in all) if (k !== 'SPRITES' && k !== 'SPRITE_PAL') all[k] = tr(all[k]);
writeFileSync(out, JSON.stringify(all));
console.log('dump:', Object.keys(all).length, 'tables');
