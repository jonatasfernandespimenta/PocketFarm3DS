# PocketFarm 3DS: native C port

Target: New 3DS only. The web game (../PocketFarmWeb) is the spec. Everything should come out 1:1, except that the
3DS build is in English: `tools/en.json` translates data.json at dump time, and the C UI strings are English.

## Layout

- `src/`: native game (C99). One `.c` per web module (`main.js` → `game.c`, `map.js` → `map.c`, …), so a JS
  change maps to one C file.
- `src/cv.h`: a Canvas2D-shaped API (save/restore/translate/scale/rotate, globalAlpha, composite op,
  drawImage, fillRect). Backends: `cv_soft.c` (CPU, used on PC and to bake offscreen images on 3DS) and
  `cv_3ds.c` (citro2d, screens only).
- `src/plat_3ds.c` / `src/plat_pc.c`: main loop, input, paths. `make pc` builds `build/pf` (headless macOS,
  scripted buttons, PPM shots in `build/shots/`).
- `tools/dump.mjs`: imports the web modules in node and writes `romfs/data.json` (every exported table,
  functions dropped). Tables are read at runtime with cJSON, so they stay 1:1 with the web build.
  Function-valued fields (GOALS.check, QUESTS.progress, tutorial STEPS) are ported by hand, keyed by id.
- `oracle/`: the QuickJS build that runs the untouched web JS (`make oracle` → `build/oracle`). Too slow
  for the 3DS, but its PPM output is the reference for pixel comparisons.

## State

`state` stays a cJSON tree with the exact JSON shape of `newState()` (save files stay compatible with the
web build). Runtime-only things (player, world, props, NPCs, fx) are C structs.

## Screens

Top 400×240 = the world at 2× (`ZOOM` in game.c; the web's rule would give 1× on this screen, too far away). Bottom 320×240 = the DOM
UI (HUD, hotbar, menus, album), touch.

## Status

| module | status |
| --- | --- |
| cv (soft + citro2d), platforms, assets, sprites, fonts (tools/font.py) | done |
| data.json, state, terrain (baked), map, player, world, house, farm, systems, factory, fx | done |
| game.c (main.js) | done, pixel-matches the oracle on the farm |
| hud.c (HUD, hotbar, menus, toasts, floaters, fade) | done |
| shops.c + scene.c (all menus, machineMenu, Ponder) | done |
| gacha.c, capsules.c, album.c, title.c | done |
| quests.c, tutorial.c, trade.c (codes byte-compatible with web), workshop.c, bench.c | done |
| sfx.c, music.c, audio.c (software synth, ndsp on a second core) | done |

Known differences from the web (by design): no QR/camera/clipboard (codes shown as text, typed with the keyboard); saves export/import via sdmc:/3ds/PocketFarm/*.json (import reads import.json); no tap-to-walk (top screen isn't touchable); decorating uses the tile in
front of the player; water reflections lack the 2px sine wobble; buttons: A act, B menu/back, Y/X/L/R tools 1-4,
START+SELECT quits.

Testing: `node tools/factest.mjs` diffs the factory sim against factory.js (random factories). `make pc && ./build/pf 120 60 [frame:mask:hold…]` → build/shots. `make oracle && ./build/oracle 120 60` →
build/oracle-shots (the web JS). 3DS: Azahar, touch sdmc:/3ds/PocketFarm/debug for per-frame timings in log.txt.
Big arrays live on the stack: __stacksize__ is 512 KB (the 32 KB default crashed with PC=0).
