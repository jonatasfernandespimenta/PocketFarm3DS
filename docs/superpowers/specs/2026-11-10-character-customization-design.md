# Character customization (3DS only)

## Why

A player on Reddit asked for it when the developer asked what they'd like in the next update. Players should be able to
make their farmer look like them, choosing their look at the start of a new save and changing it later at a wardrobe
in their house.

## What it is

One **character screen**, opened in two places:

1. **New save:** right after an empty save slot is picked, before the player reaches the farm. Title: "New farmer",
   confirm button: "Start farming".
2. **Wardrobe:** a furniture piece in the player's house. Pressing A on it (the same way the bed asks to sleep) opens
   the same screen. Title: "Wardrobe", confirm button: "Done". Cancelling (B) leaves the look unchanged.

### Options (all free and unlocked from the start)

| Row | Choices | Source art (`assets/Cute_Fantasy/Player/`) |
|---|---|---|
| Skin | 5 tones | recolor of the combined sprite (below) |
| Hair | 6 styles × 5 colors (black, blonde, brown, ginger, grey) | `Head/Hair_N/Hair_N_<Color>.png` |
| Shirt | style × color: Farmer (8), Lumberjack (9), Plain (8), Royal (7), Plate (8) | `Chest/...` |
| Pants | style × color: Farmer (8), Plain (8), Royal (7), Plate (8) | `Legs/...` (note the art's typo `Royal_Pantst_1_Red.png`) |
| Shoes | 9 colors | `Feet/Shoes_1_<Color>.png` |
| Hat | none, farmer hat | `Accessories/Farmer_Hat_1.png` |

Hands stay `Hands/Hands_1_Bare.png`. Plate helmets aren't included; they're armor, not a look.

### Layout ("B", chosen from mockups)

- **Bottom screen:** one row per option (Skin, Hair, Shirt, Pants, Shoes, Hat) and the confirm button. Up/down
  selects a row; left/right cycles it. Each row shows `◀ value ▶` (e.g. `◀ Lumberjack · Red ▶`), and tapping
  either arrow cycles too. A or tapping the button confirms. For rows with style × color, left/right walks every
  combination in order (all colors of style 1, then style 2, and so on).
- **Top screen:** the character drawn front, side and back (idle rows 0, 1, 2 of the sheet), large and nearest-neighbor
  scaled, updating live as rows change.

Mockup files: `.superpowers/brainstorm/61196-1794318826/content/creator-layout.html` (option B) and
`skin-tones-v2.html`.

## Skin tones (approved)

Skin colors aren't only on the base body. The hands layer uses a third shade, and every hairstyle draws its hairline
shadow in the skin-shadow color. So the recolor runs **after all layers are combined**, replacing these exact RGB
values (alpha untouched):

| Source color | Role |
|---|---|
| `246,202,159` | light |
| `233,188,149` | mid (hands only) → average of the target's light and shadow |
| `210,159,112` | shadow |

Approved targets (light / shadow):

| Tone | Light | Shadow |
|---|---|---|
| 1 (original) | 246,202,159 | 210,159,112 |
| 2 | 226,168,118 | 184,126,82 |
| 3 | 196,134,88 | 156,100,62 |
| 4 | 156,102,66 | 120,76,48 |
| 5 | 120,78,52 | 90,56,38 |

Every chest, legs, feet and accessory file was checked: none uses these three colors, so the post-composite recolor
only touches skin. Reference script that produced the approved previews:
`/private/tmp/claude-501/-Users-jhonny-Desktop-projects-PocketFarm-PocketFarm3DS/9a5c0b64-0ee1-469b-a996-d2c280deac20/scratchpad/scripts/preview.py`
(may be cleaned up; the tables above are the source of truth).

## How it fits the engine

- `src/player.c` already composites a fixed `LAYERS[]` list into one sheet (`body`, 576×3584 RGBA, about 8 MB) once at
  boot. Customization makes that list come from the save, then recolors skin after compositing.
- **Rebuild in place.** When the look changes, clear and redraw the existing `body` image instead of freeing it and
  allocating a new one. Two 8 MB sheets alive at once is not acceptable on the original 3DS, which has about 21 MB of
  heap headroom and currently has a memory leak (see Constraints). `cv_3ds.c` `upload()` already reuses the GPU
  texture when the size is unchanged (`same` path), so marking `dirty` re-uploads without a new texture.
- **Preview on the character screen** must not composite a second full 8 MB sheet. Build a small preview (just the
  three idle frames needed, 3 × 64×64) from the same layer list and recolor, decoding each layer's needed rows only,
  or rebuild `body` itself and draw from it. Builder's choice, within the memory rule.
- **Saved state:** add a `look` object to the save, e.g.
  `{"skin":1,"hair":[1,"Brown"],"shirt":["Farmer","White_and_Brown"],"pants":["Farmer","Blue"],"shoes":"Brown","hat":false}`.
  The exact shape is the builder's call. `newState()` in `src/state.c` sets the default to today's look. Old saves get
  it via the existing `merge(newState(), s)` on load, so **existing saves keep exactly today's look**.
- **Wardrobe furniture:** furniture comes from the web game's `FURNITURE` table via `tools/dump.mjs`. Because this is
  3DS-only, add the wardrobe on the 3DS side (inject it in `dump.mjs` after the web tables load, or register it in C),
  not in `../PocketFarmWeb`. Its art is one of the tall double-door wardrobes in
  `Buildings/House_Decor/Furniture_Other.png`. That sheet isn't in `build/romfs` yet; it's in
  `~/Downloads/Cute_Fantasy.zip`. Copy it into the romfs assets on the 3DS side, since `make romfs` rsyncs from the
  web repo with `--delete`. Give it a kind like `"wardrobe"`, alongside `"bed"` (`src/house.c:47`), and handle it next
  to the bed's interaction in `src/game.c:410`.
- **Who gets a wardrobe:** new saves start with it placed in the house (add to `house.placed` in `newState()`).
  Existing saves get one wardrobe added to their furniture inventory on first load, so they can place it where they
  like. Do this once, guarded by a flag.
- **New-save flow (my call, from the builder check):** the "New farmer" screen opens whenever a *fresh* farm starts:
  first launch with no save file (`loadState()` → `newState()`), picking an empty slot in the Saves menu
  (`slotRow` → `switchSlot` → `useSlot`), and "New game" (`resetSlot`, `src/state.c:106`). It does **not** open
  for an imported save. Trigger: those three paths set an explicit "needs look" flag on the fresh state. Don't put
  the flag in `newState()`: `merge(newState(), s)` would copy it into every old save and show them the screen. On the
  "New farmer" screen, B does nothing. The default look is preselected, so "Start farming" is always one press
  away, and backing out would leave a half-written slot.

- **Switching save slots** (`game_reload`) must rebuild the player sheet from the newly loaded save's `look`.
  Today `loadPlayerSprites()` only runs once at boot (`src/game.c:2172`).
- Draw order: base, shoes, pants, shirt, hands, hair, hat. The hat goes after the hair (matching the Aseprite layer
  order). Skin recolor runs last.

## Goals

- New saves start on the character screen.
- The wardrobe opens the same screen anytime.
- All options listed above, 5 approved skin tones, live preview.
- The look persists in the save; old saves are unchanged until the player changes them.

## Non-goals

- The web game. This is 3DS-only by decision; `PORT.md`'s 1:1 rule is knowingly broken here, and that should be noted
  in `PORT.md`.
- Save compatibility with the web game (decided: drop it). The wardrobe is ordinary movable furniture in
  `house.placed` and the furniture inventory, and the web's `house.js` (`FURNITURE[item]`) would crash on it. Update
  `PORT.md` (line 23, "save files stay compatible with the web build") to say 3DS saves no longer load in the web
  game. No stripping on export.
- Unlocks, purchases or progression tied to clothes.
- New art beyond the skin recolor and the existing wardrobe sprite.
- Naming the character.

## Anti-goals (would be a failure even if it works)

- Skin tone missing from hands or the hairline (the bug the first mockup had).
- Any extra per-frame draw cost for the player: it must stay one combined sheet.
- A new peak-memory spike that brings back the out-of-memory crash on the original 3DS.

## Constraints

- Must run on the original 3DS (`bd01e4e`: about 21 MB heap headroom on the endgame farm) and keep 60 fps (`1b91e09`).
- A memory leak is being fixed separately: the desktop harness grows about 14 MB per 90 s idle, and that leak caused
  the out-of-memory crash in `paintTerrain` (`src/terrain.c:132`). The customization work must not depend on
  that fix, and must not add to memory use.
- Verify with the desktop harness (`make pc`, `build/pf <frames> <every> <inputs…>`, shots in `build/shots`).

## Left to the builder

- Exact `look` save format and the cycling order within each row.
- How the preview is built, within the memory rule above.
- Which of the wardrobe variants in `Furniture_Other.png` to use and its exact sprite rect.
- Fonts, colors and spacing on the screen, matching the existing HUD (`src/hud.c` frames like `PAPER`/`FRAME`).
