// Mike's Workshop (workshop.js): factory puzzles on a small free grid. Pieces are free (nothing leaves the
// backpack); the line runs on the real rules (factory.c step). Pure logic; the screen is in bench.c.
// A puzzle is the PUZZLES JSON shape: { id, name, tier, w, h, hint, fixed: [[type, x, y, dir, role]], allow, par, goalMoney? }.
// A layout is a JSON array of [type, x, y, dir].
#include <stdio.h>
#include <time.h>
#include "api.h"
#include "workshop.h"

static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

J *puzzle(const char *id) { JFOR(p, table("PUZZLES")) if (eq(str(p, "id"), id)) return p; return NULL; }

// sources start showing their first item on top, so you can see what comes out
static J *piece(J *p, int i, bool fixed) {
    J *m = cJSON_CreateObject(), *role = jat(p, 4);
    cJSON_AddNumberToObject(m, "id", (fixed ? -100 : -200) - i);
    cJSON_AddStringToObject(m, "type", jat(p, 0)->valuestring);
    cJSON_AddNumberToObject(m, "x", numv(jat(p, 1)));
    cJSON_AddNumberToObject(m, "y", numv(jat(p, 2)));
    cJSON_AddStringToObject(m, "dir", jat(p, 3)->valuestring);
    if (role) cJSON_AddItemToObject(m, "role", cJSON_Duplicate(role, true));
    cJSON_AddBoolToObject(m, "fixed", fixed);
    cJSON_AddItemToObject(m, "input", cJSON_CreateObject());
    J *out = cJSON_CreateArray();
    if (cJSON_IsArray(role) && jlen(role)) cJSON_AddItemToArray(out, cJSON_Duplicate(jat(role, 0), true));
    cJSON_AddItemToObject(m, "out", out);
    cJSON_AddNumberToObject(m, "fed", 0);
    cJSON_AddNumberToObject(m, "fuel", 0);
    cJSON_AddNumberToObject(m, "progress", 0);
    cJSON_AddNullToObject(m, "cur");
    cJSON_AddNullToObject(m, "item");
    return m;
}

// A fresh sandbox state: fixed pieces + the player's layout. Caller cJSON_Delete()s it.
J *ws_build(J *pz, J *layout) {
    J *s = cJSON_Parse("{\"time\":720,\"stats\":{},\"inv\":{},\"critters\":{},\"money\":0}");
    J *ms = cJSON_AddArrayToObject(s, "machines");
    int i = 0;
    JFOR(p, jget(pz, "fixed")) cJSON_AddItemToArray(ms, piece(p, i++, true));
    i = 0;
    JFOR(p, layout) cJSON_AddItemToArray(ms, piece(p, i++, false));
    return s;
}

// One step: sources refill, then the real simulation.
void ws_tick(J *s) {
    JFOR(m, jget(s, "machines")) {
        J *role = jget(m, "role");
        if (cJSON_IsArray(role) && jlen(jget(m, "out")) < 2) {
            int fed = num(m, "fed") + 1;
            setnum(m, "fed", fed);
            cJSON_AddItemToArray(jget(m, "out"), cJSON_Duplicate(jat(role, fed % jlen(role)), true));
        }
    }
    step(s, NULL, NULL, NULL);
}

// Goals: { item, have, need } each; returns true when all are met.
int ws_progress(J *pz, J *s, WsGoal *out, int max, bool *done) {
    int n = 0;
    *done = true;
    if (num(pz, "goalMoney") && n < max) {
        int need = num(pz, "goalMoney");
        out[n++] = (WsGoal){ "money", fmin(num(s, "money"), need), need };
    }
    JFOR(m, jget(s, "machines")) {
        J *role = jget(m, "role");
        if (!cJSON_IsObject(role)) continue;
        JFOR(g, role) {
            int have = 0;
            JFOR(o, jget(m, "out")) have += cJSON_IsString(o) && eq(o->valuestring, g->string);
            if (n < max) out[n++] = (WsGoal){ g->string, fmin(numv(g), have), numv(g) };
        }
    }
    for (int i = 0; i < n; i++) if (out[i].have < out[i].need) *done = false;
    return n;
}
static bool isDone(J *pz, J *s) { WsGoal g[16]; bool d; ws_progress(pz, s, g, 16, &d); return d; }

int ws_used(J *layout, const char *type) { int n = 0; JFOR(p, layout) n += eq(jat(p, 0)->valuestring, type); return n; }

// Can `type` go at x,y? Inside the grid, not on another piece, and some left to place.
bool ws_canPut(J *pz, J *s, J *layout, const char *type, int x, int y) {
    if (num(jget(pz, "allow"), type) <= ws_used(layout, type)) return false;
    int n = num(jget(table("MACHINES"), type), "size");
    for (int dy = 0; dy < n; dy++) for (int dx = 0; dx < n; dx++) {
        int cx = x + dx, cy = y + dy;
        if (cx < 0 || cy < 0 || cx >= num(pz, "w") || cy >= num(pz, "h") || machineAt(s, cx, cy)) return false;
    }
    return true;
}

int ws_stars(J *pz, int pieces, int steps) {
    J *par = jget(pz, "par");
    return 1 + (pieces <= num(par, "pieces")) + (steps <= num(par, "steps"));
}

// Headless run: steps until the goal, or 0 if it never gets there.
int ws_solve(J *pz, J *layout, int max) {
    J *s = ws_build(pz, layout);
    int r = 0;
    for (int n = 1; n <= max && !r; n++) { ws_tick(s); if (isDone(pz, s)) r = n; }
    cJSON_Delete(s);
    return r;
}

// ---------- daily challenge ----------
void today(char out[11]) {
    time_t t = time(NULL);
    struct tm *d = localtime(&t);
    snprintf(out, 11, "%04d-%02d-%02d", d->tm_year + 1900, d->tm_mon + 1, d->tm_mday);
}

// Same puzzle for everyone on a given date: one of 4 molds with random sizes and items. Each mold also builds its
// reference layout, so the stars' par comes from actually solving it (pieces = reference, steps = reference + 2).
static Rng R;
static int r(int n) { return floor(rngNext(&R) * n); }
static J *P(const char *type, int x, int y, const char *dir, J *role) {
    J *a = cJSON_CreateArray();
    cJSON_AddItemToArray(a, cJSON_CreateString(type));
    cJSON_AddItemToArray(a, cJSON_CreateNumber(x)); cJSON_AddItemToArray(a, cJSON_CreateNumber(y));
    cJSON_AddItemToArray(a, cJSON_CreateString(dir));
    if (role) cJSON_AddItemToArray(a, role);
    return a;
}
static J *src(const char *a, const char *b) { J *l = cJSON_CreateArray(); cJSON_AddItemToArray(l, cJSON_CreateString(a)); if (b) cJSON_AddItemToArray(l, cJSON_CreateString(b)); return l; }
static J *goal(const char *item, int n) { J *g = cJSON_CreateObject(); cJSON_AddNumberToObject(g, item, n); return g; }
static J *mold(int which) {
    J *pz = cJSON_CreateObject(), *fixed = cJSON_CreateArray(), *allow = cJSON_CreateObject(), *ref = cJSON_CreateArray();
    const char *name, *hint; int w, h;
    if (which == 0) {   // furnace line, belts up to the goal
        int k = r(3); const char *ore = r(3) ? "copper_ore" : "iron_ore", *bar = ore[0] == 'c' ? "copper_bar" : "iron_bar"; int n = 2 + r(2);
        name = "Furnace"; w = 5 + k; h = 2; hint = "The arm feeds the furnace; the bar comes out the front.";
        cJSON_AddItemToArray(fixed, P("chest", 0, 1, "right", src(ore, "coal")));
        cJSON_AddItemToArray(fixed, P("chest", 4 + k, 1, "right", goal(bar, n)));
        cJSON_AddNumberToObject(allow, "arm", 2); cJSON_AddNumberToObject(allow, "furnace", 2); cJSON_AddNumberToObject(allow, "belt", k + 3);
        cJSON_AddItemToArray(ref, P("arm", 1, 1, "right", NULL)); cJSON_AddItemToArray(ref, P("furnace", 2, 0, "right", NULL));
        for (int i = 0; i < k; i++) cJSON_AddItemToArray(ref, P("belt", 4 + i, 1, "right", NULL));
    } else if (which == 1) {   // powered processor
        static const char *IO[4][2] = { { "egg", "mayo" }, { "milk", "cheese" }, { "wheat", "bread" }, { "tomato", "sauce" } };
        int k = r(2), io = r(4);
        name = "Processor"; w = 5 + k > 6 ? 5 + k : 6; h = 3; hint = "The processor needs a solar panel within 3 tiles.";
        cJSON_AddItemToArray(fixed, P("chest", 0, 2, "right", src(IO[io][0], NULL)));
        cJSON_AddItemToArray(fixed, P("chest", 4 + k, 2, "right", goal(IO[io][1], 2)));
        cJSON_AddNumberToObject(allow, "arm", 1); cJSON_AddNumberToObject(allow, "processor", 1); cJSON_AddNumberToObject(allow, "solar", 1); cJSON_AddNumberToObject(allow, "belt", k + 2);
        cJSON_AddItemToArray(ref, P("arm", 1, 2, "right", NULL)); cJSON_AddItemToArray(ref, P("processor", 2, 1, "right", NULL)); cJSON_AddItemToArray(ref, P("solar", 4, 0, "right", NULL));
        for (int i = 0; i < k; i++) cJSON_AddItemToArray(ref, P("belt", 4 + i, 2, "right", NULL));
    } else if (which == 2) {   // splitter feeding two goals
        static const char *IT[4] = { "copper_bar", "stone", "wood", "egg" };
        int k = 1 + r(2); const char *item = IT[r(4)]; int n = 2 + r(2);
        name = "Splitter"; w = 3 + k; h = 3; hint = "The splitter sends items to each side, alternating.";
        cJSON_AddItemToArray(fixed, P("chest", 0, 1, "right", src(item, NULL)));
        cJSON_AddItemToArray(fixed, P("chest", 2 + k, 0, "right", goal(item, n)));
        cJSON_AddItemToArray(fixed, P("chest", 2 + k, 2, "right", goal(item, n)));
        cJSON_AddNumberToObject(allow, "arm", 1); cJSON_AddNumberToObject(allow, "belt", k + 3); cJSON_AddNumberToObject(allow, "splitter", 1);
        cJSON_AddItemToArray(ref, P("arm", 1, 1, "right", NULL));
        for (int i = 0; i < k; i++) cJSON_AddItemToArray(ref, P("belt", 2 + i, 1, "right", NULL));
        cJSON_AddItemToArray(ref, P("splitter", 2 + k, 1, "right", NULL));
    } else {   // two lines crossing
        int cx = 3 + r(2); w = cx + 3 + r(2);
        const char *a = r(2) ? "egg" : "copper_bar", *b = r(2) ? "wood" : "stone";
        name = "Crossing"; h = 5; hint = "The underground belt passes under the other line.";
        cJSON_AddItemToArray(fixed, P("chest", 0, 2, "right", src(a, NULL)));
        cJSON_AddItemToArray(fixed, P("chest", w - 1, 2, "right", goal(a, 3)));
        cJSON_AddItemToArray(fixed, P("chest", cx, 0, "right", src(b, NULL)));
        cJSON_AddItemToArray(fixed, P("chest", cx, 4, "right", goal(b, 3)));
        cJSON_AddNumberToObject(allow, "arm", 2); cJSON_AddNumberToObject(allow, "belt", cx - 3 + 4); cJSON_AddNumberToObject(allow, "tunnel", 2);
        cJSON_AddItemToArray(ref, P("arm", 1, 2, "right", NULL));
        for (int i = 0; i < cx - 3; i++) cJSON_AddItemToArray(ref, P("belt", 2 + i, 2, "right", NULL));
        cJSON_AddItemToArray(ref, P("tunnel", cx - 1, 2, "right", NULL)); cJSON_AddItemToArray(ref, P("tunnel", w - 2, 2, "right", NULL));
        cJSON_AddItemToArray(ref, P("arm", cx, 1, "down", NULL)); cJSON_AddItemToArray(ref, P("belt", cx, 2, "down", NULL)); cJSON_AddItemToArray(ref, P("belt", cx, 3, "down", NULL));
    }
    cJSON_AddStringToObject(pz, "name", name);
    cJSON_AddNumberToObject(pz, "w", w); cJSON_AddNumberToObject(pz, "h", h);
    cJSON_AddStringToObject(pz, "hint", hint);
    cJSON_AddItemToObject(pz, "fixed", fixed);
    cJSON_AddItemToObject(pz, "allow", allow);
    cJSON_AddItemToObject(pz, "ref", ref);
    return pz;
}

// The daily puzzle for a date ("YYYY-MM-DD"). Caller cJSON_Delete()s it.
J *ws_daily(const char *date) {
    uint32_t h = 2166136261u;   // FNV-1a of the date: nearby dates must not give nearby seeds
    for (const char *c = date; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    R = rng(h);
    rngNext(&R);
    int which = r(4);
    J *pz = mold(which);
    cJSON_AddStringToObject(pz, "id", "daily");
    cJSON_AddStringToObject(pz, "date", date);
    cJSON_AddNumberToObject(pz, "tier", 1);
    J *par = cJSON_AddObjectToObject(pz, "par");
    cJSON_AddNumberToObject(par, "pieces", jlen(jget(pz, "ref")));
    cJSON_AddNumberToObject(par, "steps", ws_solve(pz, jget(pz, "ref"), 2000) + 2);
    return pz;
}

// Daily scores { s, t, p }: more stars, then fewer steps, then fewer pieces.
bool better(J *a, J *b) {
    if (!b || num(a, "s") != num(b, "s")) return num(a, "s") > (b ? num(b, "s") : 0);
    if (num(a, "t") != num(b, "t")) return num(a, "t") < num(b, "t");
    return num(a, "p") < num(b, "p");
}
