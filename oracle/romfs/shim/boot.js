// Fakes enough of a browser for the untouched PocketFarmWeb modules, then starts the game.
import './browser.js';

try {
  const t0 = native.now();
  await import('romfs:/src/main.js');
  console.log(`main.js imported in ${(native.now() - t0).toFixed(0)}ms`);
} catch (e) {
  console.error('boot: ' + e + '\n' + (e?.stack || ''));
}
