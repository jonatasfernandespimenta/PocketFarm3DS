// Wardrobe catalog (Cute_Fantasy/Player art): every look choice as static tables, plus index <-> look JSON.
// Style x color rows walk all colors of a style before the next style, colors in file order.
#include <stdio.h>
#include <string.h>
#include "game.h"

typedef struct Style { const char *label, *prefix; const char *const *colors; int n; const char *redPrefix; } Style;
typedef struct Row { const char *key; const Style *styles; int n; bool showStyle; } Row;

#define COLORS(name, ...) static const char *const name[] = { __VA_ARGS__ }; enum { name##_N = sizeof name / sizeof *name }
#define STYLE(label, prefix, c) { label, prefix, c, c##_N, NULL }
COLORS(HAIR_C, "Black", "Blonde", "Brown", "Ginger", "Grey");
COLORS(FARMER_C, "Black", "Blue", "Green", "Orange", "Pink", "Purple", "Red", "White_and_Brown");
COLORS(LUMBER_C, "Black", "Blue", "Brown", "Green", "Orange", "Pink", "Purple", "Red", "White");
COLORS(PLAIN_C, "Black", "Blue", "Brown", "Green", "Orange", "Pink", "Purple", "Red");
COLORS(ROYAL_C, "Black", "Blue", "Green", "Orange", "Purple", "Red", "White");
COLORS(PLATE_C, "Blue", "Bronze", "Gold", "Green", "Iron", "Orange", "Purple", "Red");
COLORS(SHOES_C, "Black", "Blue", "Brown", "Green", "Orange", "Pink", "Purple", "Red", "White");

static const Style HAIR[] = {
    STYLE("Style 1", "Head/Hair_1/Hair_1_", HAIR_C), STYLE("Style 2", "Head/Hair_2/Hair_2_", HAIR_C),
    STYLE("Style 3", "Head/Hair_3/Hair_3_", HAIR_C), STYLE("Style 4", "Head/Hair_4/Hair_4_", HAIR_C),
    STYLE("Style 5", "Head/Hair_5/Hair_5_", HAIR_C), STYLE("Style 6", "Head/Hair_6/Hair_6_", HAIR_C),
};
static const Style SHIRT[] = {
    STYLE("Farmer", "Chest/Farmer_Shirt/Farmer_Shirt_1_", FARMER_C),
    STYLE("Lumberjack", "Chest/Lumberjack_Shirt/Lumberjack_Shirt_1_", LUMBER_C),
    STYLE("Plain", "Chest/OG_Shirt/Shirt_1_", PLAIN_C),
    STYLE("Royal", "Chest/Royal_Shirt/Royal_Shirt_1_", ROYAL_C),
    STYLE("Plate", "Chest/Plate_Chest/Plate_Chest_", PLATE_C),
};
static const Style PANTS[] = {
    STYLE("Farmer", "Legs/Farmer_Pants/Farmer_Pants_1_", FARMER_C),
    STYLE("Plain", "Legs/OG_Pants/Pants_1_", PLAIN_C),
    { "Royal", "Legs/Royal_Pants/Royal_Pants_1_", ROYAL_C, ROYAL_C_N, "Legs/Royal_Pants/Royal_Pantst_1_" },   // the art ships the red one misspelled
    STYLE("Plate", "Legs/Plate_Legs/Plate_Legs_", PLATE_C),
};
static const Style SHOES[] = { STYLE("Shoes", "Feet/Shoes_1_", SHOES_C) };

#define NROWS(a) (int)(sizeof a / sizeof *a)
static const Row ROWS[LOOK_ROWS] = {
    [LOOK_HAIR] = { "hair", HAIR, NROWS(HAIR), true },
    [LOOK_SHIRT] = { "chest", SHIRT, NROWS(SHIRT), true },
    [LOOK_PANTS] = { "legs", PANTS, NROWS(PANTS), true },
    [LOOK_SHOES] = { "feet", SHOES, NROWS(SHOES), false },
};
#define HAT_PATH "Accessories/Farmer_Hat_1.png"

static int styleRowCount(const Row *r) {
    int n = 0;
    for (int s = 0; s < r->n; s++) n += r->styles[s].n;
    return n;
}

// Resolves a flat index (already in range) to its style and color.
static void entry(const Row *r, int index, const Style **st, const char **color) {
    int s = 0;
    while (index >= r->styles[s].n) index -= r->styles[s++].n;
    *st = &r->styles[s]; *color = r->styles[s].colors[index];
}

static void entryPath(const Style *st, const char *color, char *out, size_t cap) {
    snprintf(out, cap, "%s%s.png", !strcmp(color, "Red") && st->redPrefix ? st->redPrefix : st->prefix, color);
}

int lookCount(int row) {
    return row == LOOK_SKIN ? 5 : row == LOOK_HAT ? 2 : styleRowCount(&ROWS[row]);
}

int lookIndex(J *look, int row) {
    if (row == LOOK_SKIN) { int t = (int)num(look, "skin"); return t >= 1 && t <= 5 ? t - 1 : 0; }
    const char *cur = str(look, row == LOOK_HAT ? "hat" : ROWS[row].key);
    if (row == LOOK_HAT) return cur && !strcmp(cur, HAT_PATH);
    if (!cur) return 0;
    for (int i = 0, n = lookCount(row); i < n; i++) {
        const Style *st; const char *c; char p[96];
        entry(&ROWS[row], i, &st, &c); entryPath(st, c, p, sizeof p);
        if (!strcmp(p, cur)) return i;
    }
    return 0;
}

void lookSet(J *look, int row, int index) {
    int n = lookCount(row);
    index = ((index % n) + n) % n;
    if (row == LOOK_SKIN) { setnum(look, "skin", index + 1); return; }
    if (row == LOOK_HAT) { setstr(look, "hat", index ? HAT_PATH : NULL); return; }
    const Style *st; const char *c; char p[96];
    entry(&ROWS[row], index, &st, &c); entryPath(st, c, p, sizeof p);
    setstr(look, ROWS[row].key, p);
}

const char *lookLabel(J *look, int row) {
    static char buf[64];
    int i = lookIndex(look, row);
    if (row == LOOK_SKIN) { snprintf(buf, sizeof buf, "%d / 5", i + 1); return buf; }
    if (row == LOOK_HAT) return i ? "Farmer hat" : "None";
    const Style *st; const char *c;
    entry(&ROWS[row], i, &st, &c);
    char color[24]; snprintf(color, sizeof color, "%s", c);
    for (char *p = color; *p; p++) if (*p == '_') *p = ' ';
    if (ROWS[row].showStyle) snprintf(buf, sizeof buf, "%s \xc2\xb7 %s", st->label, color);
    else snprintf(buf, sizeof buf, "%s", color);
    return buf;
}

// ---------- the character screen (Layout B) ----------
// Bottom: one row per slot (◀ value ▶) + the confirm button; top: the farmer front, side and back at 3x.
// D-pad up/down picks a row (the button is the last one), left/right or the drawn arrows cycle, A/tap confirms,
// B reverts to the look it opened with (the wardrobe only: a new farmer has to pick).
#define SW 320
#define INK 0x3f2832ff
#define CREAM 0xfff3dcff
#define ROW_Y 32
#define ROW_H 26
static const char *const ROW_NAMES[LOOK_ROWS] = { "Skin", "Hair", "Shirt", "Pants", "Shoes", "Hat" };
// built: the look the player sheet was last built from; settle counts down after a press (the rebuild waits for it)
static struct { J *snapshot, *built; bool newFarmer, opening; int sel; double settle; } W;   // sel == LOOK_ROWS: the confirm button
#define SETTLE 0.15
void hud_nine(const char *src, int x, int y, int w, int h, int l, int t, int r, int b);   // hud.c

static bool tapped(int x, int y, int w, int h) { return in.touchDown && in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h; }
// The fonts have no ◀ ▶: a 6x11 triangle pointing left (dir < 0) or right.
static void arrow(int x, int y, int dir, uint32_t c) {
    for (int r = 0; r < 11; r++) {
        int w = 6 - abs(r - 5);
        cv_fill(dir < 0 ? x + 6 - w : x, y + r, w, 1, c);
    }
}

// Rebuild the player sheet only if the look differs from the one it was built from (a rebuild is 7 PNG decodes
// and a full-sheet upload: hundreds of ms on an original 3DS).
static void rebuildIfStale(void) {
    J *look = child(state, "look");
    W.settle = 0;
    if (W.built && cJSON_Compare(W.built, look, true)) return;
    rebuildPlayerSprites();
    cJSON_Delete(W.built);
    W.built = cJSON_Duplicate(look, true);
}

// The label changes right away; the sheet waits until the selection has been still for SETTLE seconds.
static void cycle(int row, int d) {
    J *look = child(state, "look");
    lookSet(look, row, lookIndex(look, row) + d);
    W.settle = SETTLE;
    sfx("click");
}

static void finish(bool keep) {
    if (keep) {
        if (W.newFarmer) delkey(state, "lookPending");
    } else {
        delkey(state, "look");
        cJSON_AddItemToObject(state, "look", W.snapshot);   // the state owns it now
        W.snapshot = NULL;
    }
    rebuildIfStale();
    // the revert too: an autosave mid-browse may have written a look that was then cancelled
    changed();
    if (keep) persist();
    cJSON_Delete(W.snapshot); W.snapshot = NULL;
    cJSON_Delete(W.built); W.built = NULL;
}

// returns false when the screen closed
static bool wardrobeInput(int bx, int by, int bw) {
    if (in.down & BTN_UP) { W.sel = (W.sel + LOOK_ROWS) % (LOOK_ROWS + 1); sfx("click"); }
    if (in.down & BTN_DOWN) { W.sel = (W.sel + 1) % (LOOK_ROWS + 1); sfx("click"); }
    if (W.sel < LOOK_ROWS && in.down & BTN_LEFT) cycle(W.sel, -1);
    if (W.sel < LOOK_ROWS && in.down & BTN_RIGHT) cycle(W.sel, 1);
    if (in.down & BTN_B && !W.newFarmer) { sfx("click"); finish(false); return false; }
    if ((in.down & BTN_A) || tapped(bx, by, bw, 24)) { sfx("click"); finish(true); return false; }
    for (int i = 0; i < LOOK_ROWS; i++) {
        int y = ROW_Y + i * ROW_H;
        if (tapped(100, y, 40, ROW_H - 2)) { W.sel = i; cycle(i, -1); }
        else if (tapped(SW - 48, y, 40, ROW_H - 2)) { W.sel = i; cycle(i, 1); }
        else if (tapped(8, y, SW - 16, ROW_H - 2)) W.sel = i;
    }
    return true;
}

static bool wardrobeFrame(double dt) {
    if (W.settle > 0 && (W.settle -= dt) <= 0) rebuildIfStale();
    J *look = child(state, "look");
    int bw = 120, bx = SW - 8 - bw, by = 208;
    if (W.opening) W.opening = false;   // the A that opened the wardrobe is still down this frame: it would confirm right away
    else if (!wardrobeInput(bx, by, bw)) return false;

    // top screen: the farmer turning, over the dimmed world (hud_frame calls this after its own top-screen pass)
    cv_target(cv_screen(0));
    const char *title = W.newFarmer ? "New farmer" : "Wardrobe";
    cv_fill(0, 0, 400, 240, 0x19141ebb);
    hud_nine("assets/ui/btn.png", 8, 8, text_width("px", title) + 16, 24, 5, 5, 5, 8);
    text_draw("px", title, 16, 11, INK, 0);
    Img *sheet = playerSheet();
    for (int r = 0; r < 3; r++) {   // idle rows: front, side, back (frame 0, 64x64), 3x nearest-neighbor
        int cx = 200 + (r - 1) * 120;   // the farmer fills about x 26-38, y 20-40 of the frame: centered on (cx, 120)
        hud_nine("assets/ui/paper-small.png", cx - 48, 64, 96, 112, 4, 4, 4, 4);
        cv_image(sheet, 0, r * 64, 64, 64, cx - 96, 30, 192, 192);
    }
    cv_target(cv_screen(1));

    // bottom screen
    cv_fill(0, 0, SW, 240, 0x193c3eff);
    for (int y = 0; y < 240; y += 8) for (int x = (y / 8) % 2 * 8; x < SW; x += 16) cv_fill(x, y, 8, 8, 0x1d4446ff);
    hud_nine("assets/ui/btn.png", 4, 2, SW - 8, 24, 5, 5, 5, 8);
    text_draw("px", title, (SW - text_width("px", title)) / 2, 5, INK, 0);
    for (int i = 0; i < LOOK_ROWS; i++) {
        int y = ROW_Y + i * ROW_H, h = ROW_H - 2;
        if (i == W.sel) hud_nine("assets/ui/btn-gold-down.png", 8, y, SW - 16, h, 5, 5, 5, 5);
        else hud_nine("assets/ui/paper-small.png", 8, y, SW - 16, h, 4, 4, 4, 4);
        text_draw("px", ROW_NAMES[i], 16, y + 3, INK, 0);
        arrow(112, y + 6, -1, INK);
        arrow(SW - 30, y + 6, 1, INK);
        const char *v = lookLabel(look, i);
        int cx = (124 + SW - 36) / 2;
        text_draw("px", v, cx - text_width("px", v) / 2, y + 3, INK, 0);
    }
    const char *ok = W.newFarmer ? "Start farming" : "Done";
    if (W.sel == LOOK_ROWS) hud_nine("assets/ui/btn-gold-down.png", bx - 3, by - 3, bw + 6, 30, 5, 5, 5, 5);
    hud_nine("assets/ui/btn-red.png", bx, by, bw, 24, 5, 5, 5, 8);
    text_draw("px", ok, bx + (bw - text_width("px", ok)) / 2, by + 3, CREAM, 0);
    if (!W.newFarmer) text_draw("small", "B: cancel", 12, by + 6, 0xb9a58aff, 0);
    return true;
}

void wardrobe_open(bool newFarmer) {
    cJSON_Delete(W.snapshot);
    W.snapshot = cJSON_Duplicate(child(state, "look"), true);
    cJSON_Delete(W.built);
    W.built = cJSON_Duplicate(W.snapshot, true);   // the sheet is always built from the state's look outside this screen
    W.settle = 0;
    W.newFarmer = newFarmer;
    W.sel = 0;
    W.opening = true;
    hud_closeMenu();
    hud_setOverlay(wardrobeFrame);
}
