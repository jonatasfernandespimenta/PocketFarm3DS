# Port: industrialização (web → 3DS)

The web game (`../PocketFarmWeb`, a git repo) gained phases 1–6 of `plan/sistemas/industrializacao.md` in
commits `399e038..HEAD`. The 3DS C port mirrors the web JS function by function, on the same JSON state
(`J *` = cJSON). This file is the contract for porting it in parallel. Read the web diff with
`git -C ../PocketFarmWeb diff 399e038 HEAD -- src/<file>.js`.

## Rules

- Translate the JS faithfully: same names (camelCase), same state keys, same numbers, same rule order. Where JS
  uses `Math.random`, use the existing C equivalent the file already uses.
- Stay in the files you own. Add your declarations to `src/game.h` **only inside your marked section**
  (create it at the end of the file: `// ---- port industrial: <owner> ----`).
- Don't commit. Don't touch the web repo. The lead commits.
- Builds:
  - PC harness: `export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM; make build/pf`;
  - 3DS: `export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:$PATH; make CC=arm-none-eabi-gcc`.

  Both must compile without new warnings in your files. Other owners' work in progress may break the build
  briefly; if so, add a temporary stub in your own file and mention it in your report.
- Display strings in C are English (the 3DS build is English). Data-table strings are translated by
  `tools/en.json` at dump time (`tools/dump.mjs`).

## Owners

### A — factory + cooking + inventory (`src/factory.c`, new `src/cooking.c`, `src/state.c` inventory helpers, `tools/dump.mjs`, `tools/factest.mjs`, `Makefile` GAME list)

Port everything new in `factory.js` and `cooking.js`, plus the inventory helpers of `state.js`:

- **state.c** (mirror `state.js`):
  - `BAG_SLOTS 24`, `STACK 99`;
  - `int slotsUsed(J *s)`, `int room(J *s, const char *id)`;
  - `give` → `int give(J *s, const char *id, int n)`, returns overflow into `s.overflow` and sets `s.overflowNew`;
  - `int countAll(J *s, const char *id)`, `bool takeAll(J *s, const char *id, int n)`;
  - `canAfford`/`pay` use `countAll`/`takeAll`;
  - `newState` defaults `bag: 24, overflow: {}, farmTech: {}, industry: 0`;
  - and the existing `load` merge.
- **factory.c**:
  - `int rows(const char *type)`; `cells`/`machineCovers`/`canPlace` use rows;
  - `const char *storeFor(const char *item)`, `int stored(J *m)`, `bool canPickUp(J *m)`;
  - store ports (`fromLeft` with the bottom-left mouth tile), `firstStock(m, front)` honouring `m.outFilter`;
  - `AGING` + `void ageCellars(J *s)`;
  - feeder/collector, the sorter, stale recipe hand-back, sandbox init of `stock`/`drones`;
  - drones: `J *newDrone(J *dock)`, `stepDrones`, `DRONE_COST` (as a JSON object getter `J *droneCost(void)`), `MAX_DRONES`, `DOCK_CAP`;
  - vines: `void growVines(J *s, bool isFall)`, `int harvestVine(J *s, J *m)` (gives 6 grapes to the backpack; drones use an internal variant);
  - `pickUp` returns stock/aging/drone cargo, refunds extra drones, collects a ready oven, returns station contents, pushes vine ages to `s.vineAges`, and `place` pops them;
  - stats counters (`stored`, `droneHarvest`, `sorted`, `foodMade`);
  - `const char *machineIssue(J *s, J *m, char *text, int n)` returns the code (`"nopower"`, …) or NULL and writes the English reason into `text`;
  - `powerNodes(J *s, J **out, int max) → int`, `SUPPLY_RANGE` exposed.
- **cooking.c** (mirror `cooking.js`):
  - `STATIONS` read from the dumped table (add `'cooking'` to `mods` in `tools/dump.mjs`);
  - `int addInput(J *s, J *m, const char *id)`, `int stationQueued(J *m)`;
  - `const char *work(J *s, J *m, double *progress)` (returns the product when done);
  - `bool mix(J *s, J *m)`, `const char *startCook(J *s, J *m, const char *id)` (error or NULL);
  - `const char *cookState(J *s, J *m)`, `int minutesLeft(J *s, J *m)`, `const char *collectCooked(J *s, J *m)`;
  - `bool bottle(J *s, J *m)`; `stats.baked`.
- **factest.mjs**: extend the random generator with every new piece type and field:
  - silo/cold/cellar/shed with `stock` and `outFilter`, cellar `aging`;
  - sorter `filter`/`side`, feeder `feed`, collector `out`;
  - dock with `drones` (targets) plus random ripe `soil`, and vines with `st` (age, fruit).

  `node tools/factest.mjs 300 400` must print `ok`. This is the proof the sim matches the web.

### B — systems + quests + data (`src/systems.c`, `src/quests.c`, `tools/en.json`)

- **systems.c** (mirror `systems.js`):
  - `const char *animalMap(J *s, J *a)` (`"farm"` or the interior id; granja always inside; 6:00–19:00 out);
  - `bool isGranja(J *s, const char *home)`, `int capacity(J *s, const char *home)`;
  - `const char *installTech(J *s, const char *home, const char *tech, bool paid)` (error or NULL; refuses a taken port tile);
  - `nightAnimals` with feeder/collector/confinement/big chance/low-happiness rule;
  - `const char *eat(J *s, const char *id)`;
  - `double industryTarget(J *s)`, `int industryBand(J *s)`, `double priceMult(J *s)`, `int animalPrice(J *s, const char *kind)`;
  - `buyAnimal` uses `capacity` and `animalPrice`; `deliverContract` uses `takeAll`;
  - `newDay` calls `ageCellars`, the industry drift (max +3 a night), and `growVines` after the day advances (calls into owner A's functions; stub them in systems.c only if A's aren't there yet, and say so).
- **quests.c**: `bool questOpen(J *s, const char *id)`, plus check/progress/coach for the new quests. The data
  comes from the dumped `QUESTS` table; the C file implements the functions by quest id, the way existing
  quests are done: `bread`, `storage`, `wine`, `sorting`, `drones`, `auto`, `granja`.
- **tools/en.json**: English for every new Portuguese string that reaches the dump. Find them by running
  `node tools/dump.mjs ../PocketFarmWeb /tmp/x.json` and listing values that still contain Portuguese:
  - names/descs in `ITEMS`, `MACHINES`, `FARM_TECH`, `BUILDINGS`, `MAPS`;
  - `QUESTS` (title/text/goal/coach labels), `MOOD_LINES`, `MACHINE_GROUPS` names.

  Keep the existing glossary: coins, Backpack, Journal, Notice Board, Conveyor Belt, Furnace, Pocket Town,
  "Ms. Mira". New terms:
  - warehouses: Silo, Cold Room, Cellar, Shed;
  - Drone Dock, Sorter, Workbench, Kitchen Counter, Oven, Stove, Cheese Press, Tub;
  - cooking machines: Electric Mill, Kneader, Industrial Oven, Bottler, Cheese Maker;
  - granja: Automatic Feeder, Belt Collector, Water Trough;
  - Inbox, Grape Vine.

### Lead — UI, drawing, maps (`src/game.c`, `src/shops.c`, `src/hud.c`, `src/scene.c`, `src/world.c`, `src/house.c`, `src/map.c`)

Interiors (coop/barn/`store:<id>`), inbox, menus (store, filter, stations, dock/drones, sorter, granja,
backpack eat, bag upgrade, gated shop rows, categories), drawing (stores + ports + filter icon, stations,
vines, drones, issue bubbles, belt curves, see-through, power range, `rows` footprints), unstick, bucket water,
mood lines, guide cards.
