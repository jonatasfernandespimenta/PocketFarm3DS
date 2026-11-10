# Character Customization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Players pick skin, hair, shirt, pants, shoes and hat on a "New farmer" screen when a fresh farm starts, and change them anytime at a wardrobe in their house (3DS only).

**Architecture:** The save gets a `look` object holding the chosen layer paths and a skin tone. `src/player.c` composites those layers into the existing single `body` sheet, in place, then recolors skin. A new `src/wardrobe.c` is a full-screen overlay (same pattern as `src/bench.c`): rows on the bottom screen, a front/side/back preview on the top screen. The wardrobe is a 3DS-only `FURNITURE` entry injected by `tools/dump.mjs`.

**Tech Stack:** C99, devkitARM/libctru/citro2d (3DS), the desktop harness `build/pf` (`make pc`), cJSON, Node (`tools/dump.mjs`).

**Spec:** `docs/superpowers/specs/2026-11-10-character-customization-design.md`

## Global Constraints

- 3DS only. Do not touch `../PocketFarmWeb`.
- Never hold two full player sheets (576×3584 RGBA, about 8 MB) at once. Rebuild `body` in place.
- No extra per-frame player draw cost: the player stays one combined sheet.
- Must run on the original 3DS (about 21 MB heap headroom) at 60 fps.
- Skin recolor (exact RGB, alpha untouched), applied after all layers are combined. Source `246,202,159` is light, `233,188,149` is mid (target = per-channel integer average of the target's light and shadow), `210,159,112` is shadow. Targets (light / shadow): 1 `246,202,159 / 210,159,112`; 2 `226,168,118 / 184,126,82`; 3 `196,134,88 / 156,100,62`; 4 `156,102,66 / 120,76,48`; 5 `120,78,52 / 90,56,38`.
- Layer draw order: base, feet, legs, chest, hands (`Hands/Hands_1_Bare.png`), hair, hat. Then the recolor.
- Default look = today's look: skin 1, `Feet/Shoes_1_Brown.png`, `Legs/Farmer_Pants/Farmer_Pants_1_Blue.png`, `Chest/Farmer_Shirt/Farmer_Shirt_1_White_and_Brown.png`, `Head/Hair_1/Hair_1_Brown.png`, no hat.
- UI copy: titles "New farmer" and "Wardrobe"; confirm buttons "Start farming" and "Done"; row labels Skin, Hair, Shirt, Pants, Shoes, Hat.
- All options are free and unlocked. Plate helmets are excluded.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

- **A save whose `look` names a file that doesn't exist** (hand-edited, or art renamed). Expected: that layer is skipped, no crash. Tested in Task 1 (`missing layer`).
- **Switching to a slot with a different look** (Saves menu → `game_reload`). Expected: the player immediately shows that slot's look. Tested in Task 1 (harness run after `game_reload`).
- **An old save on first load.** Expected: it looks exactly as before, isn't shown "New farmer", and has one wardrobe in its furniture. Tested in Task 4.
- **Leaving the wardrobe with B.** Expected: the look reverts to what it was when the screen opened, including the sheet. Tested in Task 3.
- **Memory peak while rebuilding.** Expected: no second 8 MB sheet; peak memory footprint within 1 MB of a run without changes. Tested in Task 3 (`/usr/bin/time -l`).

---

### Task 1: The look in the save, and the sheet built from it

**Files:**
- Modify: `src/state.c` (`newState()`: add the default `look`)
- Modify: `src/player.c` (replace the fixed `LAYERS[]` with the look; in-place rebuild; skin recolor)
- Modify: `src/game.h` (declarations)
- Modify: `src/game.c:2194` (`game_reload`: rebuild after `loadState()`)
- Modify: `src/plat_pc.c` (add `pf --look-test`)

**Interfaces:**
- Produces:
  - `look` JSON: `{"skin":1,"feet":"Feet/Shoes_1_Brown.png","legs":"Legs/Farmer_Pants/Farmer_Pants_1_Blue.png","chest":"Chest/Farmer_Shirt/Farmer_Shirt_1_White_and_Brown.png","hair":"Head/Hair_1/Hair_1_Brown.png","hat":null}`. Paths are relative to `assets/Cute_Fantasy/Player/`. `hat` is `null` or `"Accessories/Farmer_Hat_1.png"`.
  - `void recolorSkin(Img *im, int tone)`: tone 1..5; out-of-range is treated as 1.
  - `void buildPlayerSheet(Img *dst, J *look)`: clears `dst->px`, composites the layers in order, recolors, and sets `dst->dirty = true`. A layer that fails to decode is skipped.
  - `void rebuildPlayerSprites(void)`: `buildPlayerSheet(body, jget(state, "look"))`, using the existing `body`.
  - `loadPlayerSprites()` keeps its name and allocates `body` once (576×3584), then calls `rebuildPlayerSprites()`.

- [ ] **Step 1: Write the failing self-test** as `static int lookTest(void)` in `src/plat_pc.c`, run by `pf --look-test`. It loads `DATA` and `state = newState()`, then asserts:

```c
// recolor: tone 3 maps light, mid, shadow; leaves other colors and alpha alone
Img *t = img_new(4, 1);
uint8_t src[16] = { 246,202,159,255,  233,188,149,128,  210,159,112,255,  14,7,27,255 };
memcpy(t->px, src, 16);
recolorSkin(t, 3);
assert(t->px[0] == 196 && t->px[1] == 134 && t->px[2] == 88);
assert(t->px[4] == (196+156)/2 && t->px[5] == (134+100)/2 && t->px[6] == (88+62)/2 && t->px[7] == 128);
assert(t->px[8] == 156 && t->px[9] == 100 && t->px[10] == 62);
assert(t->px[12] == 14 && t->px[13] == 7 && t->px[14] == 27);
// default look reproduces today's sheet byte-for-byte (build/romfs layers composited in the old fixed order)
Img *a = img_new(576, 3584), *b = /* the old 6-layer composite, built inline in the test */;
buildPlayerSheet(a, jget(state, "look"));
assert(!memcmp(a->px, b->px, 576 * 3584 * 4));
// missing layer: skipped, no crash
J *bad = cJSON_Duplicate(jget(state, "look"), true); setstr(bad, "hair", "Head/Nope.png");
buildPlayerSheet(a, bad);
puts("look-test ok");
```

- [ ] **Step 2: Run it to verify it fails**

Run: `make build/pf && ./build/pf --look-test`
Expected: compile error, `recolorSkin` / `buildPlayerSheet` undeclared.

- [ ] **Step 3: Implement** the default `look` in `newState()`, plus `recolorSkin`, `buildPlayerSheet`, `rebuildPlayerSprites` and the new `loadPlayerSprites` in `src/player.c`. Clear with `memset(dst->px, 0, …)`, decode each layer with `img_decode` (uncached) and free it right after, as today. In `game_reload()`, call `rebuildPlayerSprites()` right after `state = loadState();`.

- [ ] **Step 4: Run the self-test and an unchanged harness run**

Run: `make build/pf && ./build/pf --look-test && ./build/pf 120 60`
Expected: `look-test ok`. `build/shots/top-60.ppm` shows the farmer looking exactly as before.

- [ ] **Step 5: Build for 3DS**

Run: `make`
Expected: `PocketFarm.3dsx` builds with no new warnings.

- [ ] **Step 6: Commit**

```bash
git add src/state.c src/player.c src/game.h src/game.c src/plat_pc.c
git commit -m "player: sprite sheet built from the save's look, skin tones"
```

### Task 2: The options catalog and cycling

**Files:**
- Create: `src/wardrobe.c` (catalog part; the screen is Task 3)
- Modify: `src/game.h` (declarations)
- Modify: `src/plat_pc.c` (`--look-test` gains catalog asserts)

**Interfaces:**
- Consumes: the `look` JSON from Task 1.
- Produces:
  - `enum { LOOK_SKIN, LOOK_HAIR, LOOK_SHIRT, LOOK_PANTS, LOOK_SHOES, LOOK_HAT, LOOK_ROWS };`
  - `int lookCount(int row)`: number of choices in the row.
  - `int lookIndex(J *look, int row)`: the current choice's index; 0 when the stored value isn't in the catalog.
  - `void lookSet(J *look, int row, int index)`: writes the choice (index wraps modulo `lookCount`).
  - `const char *lookLabel(J *look, int row)`: static buffer, e.g. `"Lumberjack · Red"`, `"Style 3 · Black"`, `"3 / 5"`, `"Brown"`, `"None"`, `"Farmer hat"`.

Catalog, in cycle order. Style × color rows walk all colors of a style before the next style, colors in alphabetical file order:

| Row | Styles (label → folder / file prefix) |
|---|---|
| Skin | tones 1..5 |
| Hair | `Style 1`..`Style 6` → `Head/Hair_N/Hair_N_<C>.png`, C ∈ Black, Blonde, Brown, Ginger, Grey |
| Shirt | `Farmer` → `Chest/Farmer_Shirt/Farmer_Shirt_1_*`; `Lumberjack` → `Chest/Lumberjack_Shirt/Lumberjack_Shirt_1_*`; `Plain` → `Chest/OG_Shirt/Shirt_1_*`; `Royal` → `Chest/Royal_Shirt/Royal_Shirt_1_*`; `Plate` → `Chest/Plate_Chest/Plate_Chest_*` |
| Pants | `Farmer` → `Legs/Farmer_Pants/Farmer_Pants_1_*`; `Plain` → `Legs/OG_Pants/Pants_1_*`; `Royal` → `Legs/Royal_Pants/Royal_Pants_1_*` plus `Royal_Pantst_1_Red.png` (the art's typo); `Plate` → `Legs/Plate_Legs/Plate_Legs_*` |
| Shoes | `Feet/Shoes_1_<C>.png`, 9 colors |
| Hat | none, `Accessories/Farmer_Hat_1.png` |

Write the catalog as static string tables, not a directory scan. Color labels are the file's color part with `_` → space ("White_and_Brown" → "White and Brown").

- [ ] **Step 1: Add the failing asserts to `lookTest`**

```c
J *l = jget(state, "look");
assert(lookCount(LOOK_SKIN) == 5 && lookCount(LOOK_HAIR) == 30 && lookCount(LOOK_SHOES) == 9 && lookCount(LOOK_HAT) == 2);
assert(lookCount(LOOK_SHIRT) == 8 + 9 + 8 + 7 + 8 && lookCount(LOOK_PANTS) == 8 + 8 + 7 + 8);
assert(!strcmp(lookLabel(l, LOOK_SHIRT), "Farmer · White and Brown"));
for (int r = 0; r < LOOK_ROWS; r++) for (int i = 0; i < lookCount(r); i++) {   // every choice is real art and round-trips
    lookSet(l, r, i); assert(lookIndex(l, r) == i);
    const char *p = r == LOOK_SKIN ? NULL : str(l, (const char *[]){ 0, "hair", "chest", "legs", "feet", "hat" }[r]);
    if (p) { char f[160]; snprintf(f, sizeof f, "romfs:/assets/Cute_Fantasy/Player/%s", p); assert(readFile(f, NULL)); }
}
lookSet(l, LOOK_HAIR, -1); assert(lookIndex(l, LOOK_HAIR) == 29);   // wraps backwards
```

- [ ] **Step 2: Run and see it fail**

Run: `make build/pf && ./build/pf --look-test`
Expected: compile error, `lookCount` undeclared.

- [ ] **Step 3: Implement the catalog functions in `src/wardrobe.c`.** `src/*.c` is globbed by the Makefile, so no build change is needed.

- [ ] **Step 4: Run and see it pass**

Run: `make build/pf && ./build/pf --look-test`
Expected: `look-test ok`.

- [ ] **Step 5: Commit**

```bash
git add src/wardrobe.c src/game.h src/plat_pc.c
git commit -m "wardrobe: catalog of looks (hair, shirts, pants, shoes, hat, skin)"
```

### Task 3: The character screen

**Files:**
- Modify: `src/wardrobe.c` (the overlay)
- Modify: `src/game.h`

**Interfaces:**
- Consumes: Task 1 `rebuildPlayerSprites()`, Task 2 catalog, `hud_setOverlay` (`src/hud.h:57`), `Input in` (`src/game.h:300`).
- Produces: `void wardrobe_open(bool newFarmer)`. It snapshots `look` (a `cJSON_Duplicate`), closes menus and sets the overlay.

Behavior (spec "Layout B"):
- Bottom screen: the title ("New farmer" / "Wardrobe"), six rows showing `◀ value ▶`, and the confirm button ("Start farming" / "Done"). Up/down moves the selected row (the confirm button counts as a row). Left/right calls `lookSet(…, index ± 1)` then `rebuildPlayerSprites()`. Tapping the ◀/▶ on a row does the same. A, or tapping the button, confirms.
- Confirm: `changed()`/`persist()` as other screens do. For `newFarmer`, also delete `state.lookPending` (Task 5).
- B: in the wardrobe, restore the snapshot, `rebuildPlayerSprites()`, close. In "New farmer", B does nothing.
- Top screen: draw over the world the way `src/title.c:93-110` does (`cv_target(cv_screen(0))` … back to `cv_screen(1)`). Show the idle frames of rows 0, 1, 2 (front, side, back: frame 0, 64×64 each) from the player sheet, drawn ×3 and nearest-neighbor. Expose the sheet through a `Img *playerSheet(void)` getter in `player.c`. Draw style follows `src/bench.c` (its `button()`/`tapped()` helpers at lines 120-125) and the HUD's paper frames.
- Each change rebuilds the 8 MB sheet. It's acceptable for a menu if it stays under about 100 ms per press on hardware. If the 3DS log shows more, debounce (rebuild only when the selection has been still for 150 ms). This goes in the step's commit message.

- [ ] **Step 1: Harness script that drives the screen.** Temporarily, `PF_WARDROBE=1` in `src/plat_pc.c` calls `wardrobe_open(false)` at frame 60, like the existing `PF_GACHA` hook at `src/plat_pc.c` (frame 60). Inputs: frame 70 Down (Hair row), frame 80 Right ×3, frame 100 Down, Down (Pants), frame 120 Right, then frame 140 B.

Run: `PF_WARDROBE=1 ./build/pf 160 10 70:<DOWN>:1 80:<RIGHT>:1 84:<RIGHT>:1 88:<RIGHT>:1 100:<DOWN>:1 104:<DOWN>:1 120:<RIGHT>:1 140:<B>:1`, using the `BTN_*` hex masks from `src/game.h`.
Expected before implementing: link error, `wardrobe_open` undefined.

- [ ] **Step 2: Implement `wardrobe_open` and the overlay frame.**

- [ ] **Step 3: Check the screens and the B revert**

Run the Step 1 command.
Expected: `build/shots/bot-90.ppm` shows the Hair row selected with "Style 2 · Black". The default Brown is index 2 of Style 1's alphabetical colors, and three steps right goes Ginger → Grey → Style 2 Black. `top-130.ppm` shows the new pants on all three previews. `top-160.ppm` shows the farmer back in the default look.

- [ ] **Step 4: Memory check**

Run: `/usr/bin/time -l ./build/pf 160 0 2>&1 | grep 'peak memory'` vs `PF_WARDROBE=1 /usr/bin/time -l ./build/pf 160 0 … (same inputs) 2>&1 | grep 'peak memory'`
Expected: within 1 MB of each other.

- [ ] **Step 5: `make`** (3DS build is clean), then commit:

```bash
git add src/wardrobe.c src/player.c src/game.h src/plat_pc.c
git commit -m "wardrobe: the character screen (rows, live 3-view preview, B reverts)"
```

### Task 4: The wardrobe furniture

**Files:**
- Create: `romfs-3ds/assets/Cute_Fantasy/Buildings/House_Decor/Furniture_Other.png`, copied from `Cute_Fantasy/Buildings/House_Decor/Furniture_Other.png` in `~/Downloads/Cute_Fantasy.zip`
- Modify: `Makefile` (`romfs` target: after the web `rsync`, `rsync -a romfs-3ds/ build/romfs/`)
- Modify: `tools/dump.mjs` (inject the wardrobe after the tables load, before `tr`)
- Modify: `src/house.c:47` (kind `"wardrobe"`)
- Modify: `src/game.c:410` (A on it → `wardrobe_open(false)`)
- Modify: `src/state.c` (`newState()` places it; `loadState()` gives old saves one)

**Interfaces:**
- Consumes: `wardrobe_open(bool)` from Task 3.
- Produces: the `FURNITURE.wardrobe` item and prop kind `"wardrobe"`.

Values:
- `FURNITURE.wardrobe = { name: 'Wardrobe', src: 'assets/Cute_Fantasy/Buildings/House_Decor/Furniture_Other.png', rect: [32, 80, 32, 32], price: 300, comfort: 2, wardrobe: true }`. Price and comfort are my call (it also shows in the furniture shop, which is fine).
- `house.c:47`: `has(f, "bed") ? "bed" : has(f, "wardrobe") ? "wardrobe" : "furniture"`.
- New saves: `{"uid":3,"item":"wardrobe","x":7,"y":4}` added to `house.placed` in `newState()`. Check it in a harness shot and nudge `x`/`y` if it overlaps the bed, the table or the door.
- Old saves: in `loadState()`, take `bool legacy = s && !jget(s, "look");` before merging. After building the result, if `legacy`, increment `decor.wardrobe` by 1. This runs once, because the next save has `look`. There's no flag, and a flag in `newState()` would be copied into every old save by `merge`.

- [ ] **Step 1: Failing asserts in `lookTest`**

```c
J *fresh = newState();
int found = 0; JFOR(p, jget(jget(fresh, "house"), "placed")) found += eq(str(p, "item"), "wardrobe");
assert(found == 1 && jget(T("FURNITURE"), "wardrobe"));
// an old save (no "look") gets one wardrobe in its furniture and keeps the default look
writeKey("pocketfarm.v1", /* newState() with "look" deleted */);
J *old = loadState();
assert(num(jget(old, "decor"), "wardrobe") == 1 && !strcmp(str(jget(old, "look"), "hair"), "Head/Hair_1/Hair_1_Brown.png"));
// loading it again doesn't give a second one
saveState(old, 0); assert(num(jget(loadState(), "decor"), "wardrobe") == 1);
```

Point the test at a scratch save directory, not `build/sdmc`. `writeKey` is static in `state.c`, so expose a test hook or write the file with `writeFile(keyPath…)`, implementer's choice.

- [ ] **Step 2: Run, see it fail:** `make romfs && make build/pf && ./build/pf --look-test`. Expected: assert on `found == 1`.

- [ ] **Step 3: Implement the five changes above.**

- [ ] **Step 4: Run, see it pass.** Then a harness shot inside the house on a fresh save (`PF_WARDROBE` hook off). Walk to the wardrobe and press A. Expected: `look-test ok`; the shot shows the wardrobe on the floor and the character screen titled "Wardrobe".

- [ ] **Step 5: `make` and commit**

```bash
git add romfs-3ds Makefile tools/dump.mjs src/house.c src/game.c src/state.c src/plat_pc.c
git commit -m "house: wardrobe furniture opens the character screen"
```

### Task 5: "New farmer" on fresh farms, and the docs

**Files:**
- Modify: `src/state.c` (`loadState()` no-file fallback, `useSlot()` empty slot, `resetSlot()`: set `"lookPending": true` on the fresh state)
- Modify: `src/hud.c:800` (`hud_frame`: after the title, open the screen when pending)
- Modify: `PORT.md` (line 23 and the "Known differences" list)

**Interfaces:**
- Consumes: `wardrobe_open(true)` (Task 3 deletes `lookPending` on confirm).

Rules (from the spec):
- Set `lookPending` only in those three paths, never in `newState()`. `merge(newState(), s)` would copy it into every old save.
- Imported saves never get it.
- `hud_frame`: right after the `titleUp` line, `if (!overlay && !menuOpen && has(state, "lookPending")) wardrobe_open(true);`. This covers first launch (after "Play") and slot switches (`game_reload`).

- [ ] **Step 1: Failing asserts in `lookTest`**

```c
remove(/* scratch pocketfarm.v1 */);
assert(has(loadState(), "lookPending"));                 // first launch
J *o = newState(); assert(!has(o, "lookPending"));        // never in the template
```

- [ ] **Step 2: Run, see it fail.** `./build/pf --look-test`. Expected: assert on `has(loadState(), "lookPending")`.

- [ ] **Step 3: Implement it, then update `PORT.md`.**
  - Line 23: the save shape is the web's plus `look`. 3DS saves with a wardrobe no longer load in the web game.
  - "Known differences": add "character customization and the wardrobe (3DS only)".

- [ ] **Step 4: End-to-end in the harness.** Use a scratch `build/sdmc` copy with no `pocketfarm.v1.json`. Run until past the title ("Play" tap), then confirm with A. Expected: the "New farmer" screen appears after the title; after A, the game runs and the save has no `lookPending`. A second run of the same save doesn't show the screen. Restore `build/sdmc` afterwards.

- [ ] **Step 5: `make` and `make cia`** (both build), then commit:

```bash
git add src/state.c src/hud.c PORT.md src/plat_pc.c
git commit -m "new farmer: fresh farms start on the character screen"
```

### Task 6: On hardware

- [ ] **Step 1:** Install the CIA on the original 3DS and the New 3DS if available. Start a new save: the "New farmer" screen shows, cycling is responsive, and the farm starts with the chosen look.
- [ ] **Step 2:** Use the wardrobe: change the look and press B (it reverts), then change it and press Done (it sticks after a restart).
- [ ] **Step 3:** Check `sdmc:/3ds/PocketFarm/log.txt`. `heap used` after several wardrobe uses is within 1 MB of the boot value, and there are no `tex alloc failed` lines.
