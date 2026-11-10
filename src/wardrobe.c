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
