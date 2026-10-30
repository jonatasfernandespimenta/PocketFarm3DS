// Factory simulation (factory.js): discrete steps on a tile grid. Machines are the state's JSON objects.
// Belts carry one item per tile into chests / sell boxes / other belts. Machines are fed by robotic arms
// (or by hand) and push their output out of their `dir` side. Some machines need power from generators.
#include <stdio.h>
#include "game.h"

#define CAP 20        // max items queued inside a machine
#define FUEL_CAP 50   // coal a machine can stock
#define HOARD 99      // a machine out of fuel keeps taking ore until coal shows up (no deadlock on a mixed lane)
#define POLE_RANGE 6
#define SUPPLY_RANGE 3
#define TUNNEL_RANGE 5

// Step counter: every move records when it happened (`at`) and where the item came from (`from`), so the
// renderer can slide items between tiles instead of teleporting them.
static int tick;
int currentTick(void) { return tick; }
static void moved1(J *t, int fx, int fy) {
    if (abs(fx - (int)num(t, "x")) + abs(fy - (int)num(t, "y")) == 1) {
        int a[2] = { fx, fy };
        delkey(t, "from");
        cJSON_AddItemToObject(t, "from", cJSON_CreateIntArray(a, 2));
        setnum(t, "at", tick);
    } else setstr(t, "from", NULL);
}
static const int SIDES[4][2] = { [UP] = { LEFT, RIGHT }, [DOWN] = { RIGHT, LEFT }, [LEFT] = { DOWN, UP }, [RIGHT] = { UP, DOWN } };
static const int BACK[4] = { [UP] = DOWN, [DOWN] = UP, [LEFT] = RIGHT, [RIGHT] = LEFT };
const int ROTATE[4] = { [UP] = RIGHT, [RIGHT] = DOWN, [DOWN] = LEFT, [LEFT] = UP };

J *mdef(J *m) { return jget(table("MACHINES"), str(m, "type")); }
static int size(J *m) { return num(mdef(m), "size"); }
static int mdir(J *m) { return dirFrom(str(m, "dir")); }

// Which factory stage the player has reached (Mike's quest chain).
int factoryTier(J *s) {
    J *d = jget(jget(s, "quests"), "done");
    return has(d, "power") ? 4 : has(d, "line") ? 3 : has(d, "machine") ? 2 : has(d, "coop") ? 1 : 0;
}

bool machineCovers(J *m, int x, int y) {
    int n = size(m), mx = num(m, "x"), my = num(m, "y");
    return x >= mx && x < mx + n && y >= my && y < my + n;
}
J *machineAt(J *s, int x, int y) { JFOR(m, jget(s, "machines")) if (machineCovers(m, x, y)) return m; return NULL; }

// isFree(x, y) → tile is walkable ground with nothing on it (checked by the caller against the map)
bool canPlace(J *s, const char *type, int x, int y, bool (*isFree)(int, int)) {
    int n = num(jget(table("MACHINES"), type), "size");
    for (int dy = 0; dy < n; dy++) for (int dx = 0; dx < n; dx++)
        if (!isFree(x + dx, y + dy) || machineAt(s, x + dx, y + dy) || soilAt(s, x + dx, y + dy)) return false;
    return true;
}

J *place(J *s, const char *type, int x, int y, int dir, double nowMs) {
    J *ms = childarr(s, "machines"), *m = cJSON_Parse("{\"input\":{},\"out\":[],\"fuel\":0,\"progress\":0,\"cur\":null,\"item\":null}");
    cJSON *id = cJSON_CreateNumber(fmod(nowMs, 1e9) + jlen(ms));
    cJSON_AddItemToObject(m, "id", id);
    setstr(m, "type", type); setnum(m, "x", x); setnum(m, "y", y); setstr(m, "dir", DIR_NAME[dir]);
    cJSON_AddItemToArray(ms, m);
    return m;
}

// Pick a machine back up: its contents go to the backpack.
void pickUp(J *s, J *m) {
    JFOR(it, jget(m, "input")) give(s, it->string, numv(it));
    JFOR(it, jget(m, "out")) give(s, it->valuestring, 1);
    if (str(m, "item")) give(s, str(m, "item"), 1);
    if (str(m, "cur")) give(s, str(m, "cur"), 1);
    if (num(m, "fuel")) give(s, "coal", num(m, "fuel"));
    char k[48]; snprintf(k, sizeof k, "machine_%s", str(m, "type"));
    cJSON_Delete(cJSON_DetachItemViaPointer(jget(s, "machines"), m));
    give(s, k, 1);
}

static int queued(J *m) { int n = 0; JFOR(it, jget(m, "input")) n += numv(it); return n; }
static bool carries(J *m) { const char *t = str(m, "type"); return !strcmp(t, "belt") || !strcmp(t, "splitter") || !strcmp(t, "tunnel"); }
static bool is(J *m, const char *type) { return !strcmp(str(m, "type"), type); }

bool accepts(J *m, const char *item) {
    J *def = mdef(m);
    if (carries(m)) return !str(m, "item");
    if (is(m, "chest") || is(m, "autosell")) return true;
    if (str(def, "fuel") && !strcmp(str(def, "fuel"), item)) return num(m, "fuel") < FUEL_CAP;
    return jget(jget(def, "recipes"), item) && queued(m) < (str(def, "fuel") && !num(m, "fuel") ? HOARD : CAP);
}

// Returns money earned (autosell), else 0.
int insert(J *s, J *m, const char *item) {
    J *def = mdef(m);
    if (carries(m)) { setstr(m, "item", item); return 0; }
    if (is(m, "chest")) { cJSON_AddItemToArray(childarr(m, "out"), cJSON_CreateString(item)); return 0; }
    if (is(m, "autosell")) {
        double sell = num(jget(table("ITEMS"), item), "sell");
        int price = jround((sell ? sell : 1) * sellMult(s));
        earn(s, price);
        return price;
    }
    if (str(def, "fuel") && !strcmp(str(def, "fuel"), item)) setnum(m, "fuel", num(m, "fuel") + 1);
    else setnum(child(m, "input"), item, num(jget(m, "input"), item) + 1);
    return 0;
}

// Tile a machine pushes its output into.
Tile outputCell(J *m) { return outputCellOf(str(m, "type"), num(m, "x"), num(m, "y"), mdir(m)); }
Tile outputCellOf(const char *type, int x, int y, int dir) {
    int n = num(jget(table("MACHINES"), type), "size");
    if (dir == RIGHT) return (Tile){ x + n, y + n - 1 };
    if (dir == LEFT) return (Tile){ x - 1, y + n - 1 };
    if (dir == DOWN) return (Tile){ x + n - 1, y + n };
    return (Tile){ x, y - 1 };
}

static Tile ahead(J *m, int dir, int k) { return (Tile){ (int)num(m, "x") + DV[dir][0] * k, (int)num(m, "y") + DV[dir][1] * k }; }
static J *at(J *s, Tile t) { return machineAt(s, t.x, t.y); }

static const char *weighted(J *list, Rng *r) {
    double total = 0;
    JFOR(e, list) total += numv(jat(e, 1));
    double x = (r ? rngNext(r) : frand()) * total;
    JFOR(e, list) if ((x -= numv(jat(e, 1))) < 0) return jat(e, 0)->valuestring;
    return jat(jat(list, 0), 0)->valuestring;
}

// ---------- power ----------
// Generators (producing) and poles form a network when within POLE_RANGE of each other.
// A power-hungry machine within SUPPLY_RANGE of the network gets power while the network has capacity left.
typedef struct { J *m[256]; int n; } Set;
static bool inSet(Set *s, J *m) { for (int i = 0; i < s->n; i++) if (s->m[i] == m) return true; return false; }
static void add(Set *s, J *m) { if (s->n < 256) s->m[s->n++] = m; }
static bool near(J *a, J *b, double r) {
    double ax = num(a, "x") + (size(a) - 1) / 2.0, ay = num(a, "y") + (size(a) - 1) / 2.0;
    double bx = num(b, "x") + (size(b) - 1) / 2.0, by = num(b, "y") + (size(b) - 1) / 2.0;
    return fmax(fabs(ax - bx), fabs(ay - by)) <= r;
}
static bool isDay(J *s) { J *t = jget(s, "time"); return (cJSON_IsNumber(t) ? t->valuedouble : num(DATA, "DAY_START")) < 18 * 60; }

static void powered(J *s, Set *out) {
    Set nodes = { 0 };
    double capacity = 0;
    JFOR(m, jget(s, "machines")) {
        J *d = mdef(m);
        if (num(d, "generator") && (!is(m, "solar") || isDay(s)) && (!str(d, "fuel") || num(m, "fuel") > 0)) { add(&nodes, m); capacity += num(d, "generator"); }
    }
    for (int i = 0; i < nodes.n; i++)
        JFOR(p, jget(s, "machines")) if (is(p, "pole") && !inSet(&nodes, p) && near(nodes.m[i], p, POLE_RANGE)) add(&nodes, p);
    // ponytail: one shared capacity for all connected generators; split into separate grids if players ask
    out->n = 0;
    JFOR(m, jget(s, "machines")) {
        if (!has(mdef(m), "power") || capacity <= 0) continue;
        for (int i = 0; i < nodes.n; i++) if (near(nodes.m[i], m, SUPPLY_RANGE + (size(m) - 1))) { add(out, m); capacity--; break; }
    }
}

// ---------- step ----------
static void bump(J *s, const char *stat) { J *st = child(s, "stats"); setnum(st, stat, num(st, stat) + 1); }

// Move one item from `src` toward the tile in front of it. Returns false if it can't, else adds to *sold.
static bool push(J *s, J *src, Tile tl, Set *moved, int *sold) {
    J *t = at(s, tl);
    if (!t || t == src || !str(src, "item")) return false;
    J *td = mdef(t);
    // belts don't feed processing machines: that's the robotic arm's job
    if (jget(td, "recipes") || jget(td, "produce") || num(td, "generator")) return false;
    if (!accepts(t, str(src, "item"))) return false;
    *sold += insert(s, t, str(src, "item"));
    setstr(src, "item", NULL);
    if (carries(t)) { add(moved, t); moved1(t, num(src, "x"), num(src, "y")); }
    return true;
}

static const char *firstOut(J *m) { J *o = jat(jget(m, "out"), 0); return o ? o->valuestring : NULL; }
static void shiftOut(J *m) { cJSON_DeleteItemFromArray(jget(m, "out"), 0); }

// One simulation step. sold/items produced this step.
void step(J *s, Rng *r, int *soldOut, int *itemsOut) {
    int sold = 0, items = 0;
    tick++;
    Set moved = { 0 }, power;
    powered(s, &power);
    child(s, "stats");
    JFOR(m, jget(s, "machines")) {
        J *def = mdef(m);
        if (inSet(&moved, m)) continue;
        int dir = mdir(m);
        if (is(m, "belt") && str(m, "item")) push(s, m, ahead(m, dir, 1), &moved, &sold);
        else if (is(m, "splitter") && str(m, "item")) {
            // alternate sides; if one is blocked, try the other
            bool flip = !has(m, "flip");
            setbool(m, "flip", flip);
            for (int i = 0; i < 2; i++) {
                int side = SIDES[dir][flip ? i : 1 - i];
                if (push(s, m, ahead(m, side, 1), &moved, &sold)) { bump(s, "routed"); break; }
            }
        } else if (is(m, "tunnel") && str(m, "item")) {
            // jump to the next tunnel ahead facing the same way; the last one acts like a belt
            J *exit = NULL;
            for (int k = 1; k <= TUNNEL_RANGE && !exit; k++) {
                J *t = at(s, ahead(m, dir, k));
                if (t && is(t, "tunnel") && mdir(t) == dir) exit = t;
            }
            if (exit) {
                if (!str(exit, "item")) { setstr(exit, "item", str(m, "item")); setstr(m, "item", NULL); add(&moved, exit); setstr(exit, "from", NULL); bump(s, "routed"); }
            } else push(s, m, ahead(m, dir, 1), &moved, &sold);
        } else if (is(m, "arm")) {
            // pick up from behind, drop in front. It only grabs what the front can take right now,
            // so it never gets stuck holding something useless.
            J *back = at(s, ahead(m, BACK[dir], 1)), *front = at(s, ahead(m, dir, 1));
            if (!str(m, "item") && back && front && back != front) {
                const char *next = carries(back) ? str(back, "item") : !is(back, "autosell") ? firstOut(back) : NULL;
                if (next && accepts(front, next)) {
                    char nx[32]; snprintf(nx, sizeof nx, "%s", next);
                    if (carries(back)) setstr(back, "item", NULL); else shiftOut(back);
                    setstr(m, "item", nx); setnum(m, "at", tick);
                }
            } else if (str(m, "item") && front && accepts(front, str(m, "item"))) {
                sold += insert(s, front, str(m, "item"));
                if (carries(front)) { add(&moved, front); moved1(front, num(m, "x"), num(m, "y")); }
                setstr(m, "item", NULL); setnum(m, "at", tick);
                bump(s, "armMoves");
            }
            setnum(m, "swing", str(m, "item") ? 1 : 0);
        } else if (num(def, "generator") && str(def, "fuel")) {
            // steam engine burns 1 coal every 40 steps while it powers something
            if (num(m, "fuel") > 0 && power.n) {
                setnum(m, "burn", num(m, "burn") + 1);
                if (num(m, "burn") >= 40) { setnum(m, "burn", 0); setnum(m, "fuel", num(m, "fuel") - 1); }
            }
        } else if (jget(def, "recipes")) {
            bool hasPower = !has(def, "power") || inSet(&power, m);
            if (!str(m, "cur") && hasPower) {
                J *next = NULL;
                JFOR(it, jget(m, "input")) if (numv(it) > 0) { next = it; break; }
                if (next && (!str(def, "fuel") || num(m, "fuel") > 0) && jlen(jget(m, "out")) < CAP) {
                    char id[32]; snprintf(id, sizeof id, "%s", next->string);
                    setstr(m, "cur", id);
                    setnum(m, "progress", 0);
                    setnum(jget(m, "input"), id, numv(next) - 1);
                    if (!num(jget(m, "input"), id)) delkey(jget(m, "input"), id);
                    if (str(def, "fuel")) setnum(m, "fuel", num(m, "fuel") - 1);
                }
            }
            if (str(m, "cur") && hasPower) {
                setnum(m, "progress", num(m, "progress") + STEP);
                J *rec = jget(jget(def, "recipes"), str(m, "cur"));
                if (num(m, "progress") >= numv(jat(rec, 1))) {
                    cJSON_AddItemToArray(childarr(m, "out"), cJSON_CreateString(jat(rec, 0)->valuestring));
                    setstr(m, "cur", NULL); items++;
                    if (has(def, "power")) bump(s, "poweredWork");
                }
            }
        } else if (jget(def, "produce") && jlen(jget(m, "out")) < CAP && inSet(&power, m)) {
            setnum(m, "progress", num(m, "progress") + STEP);
            if (num(m, "progress") >= num(def, "every")) {
                setnum(m, "progress", 0);
                cJSON_AddItemToArray(childarr(m, "out"), cJSON_CreateString(weighted(jget(def, "produce"), r)));
                items++; bump(s, "poweredWork");
            }
        }
        setbool(m, "unpowered", has(def, "power") && !inSet(&power, m));

        // machines push one finished item out of their output side
        if (jlen(jget(m, "out")) && (jget(def, "recipes") || jget(def, "produce"))) {
            J *t = at(s, outputCell(m));
            if (t && t != m && accepts(t, firstOut(m))) {
                char it[32]; snprintf(it, sizeof it, "%s", firstOut(m));
                shiftOut(m);
                sold += insert(s, t, it);
                if (carries(t)) { add(&moved, t); moved1(t, num(t, "x") - DV[dir][0], num(t, "y") - DV[dir][1]); }
            }
        }
    }
    if (soldOut) *soldOut = sold;
    if (itemsOut) *itemsOut = items;
}

bool isRunning(J *m) {
    J *d = mdef(m);
    return str(m, "cur") || (jget(d, "produce") && !has(m, "unpowered") && jlen(jget(m, "out")) < CAP)
        || (num(d, "generator") && !has(m, "unpowered") && (!str(d, "fuel") || num(m, "fuel") > 0));
}

// Overnight: sprinklers water around them, machines run a long stretch (solar panels are off at night).
void nightFactory(J *s, int *sold, int *items) {
    JFOR(m, jget(s, "machines")) {
        if (!is(m, "sprinkler")) continue;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            J *t = soilAt(s, num(m, "x") + dx, num(m, "y") + dy);
            if (t) setbool(t, "wet", true);
        }
    }
    double time = num(s, "time");
    setnum(s, "time", 23 * 60);
    *sold = *items = 0;
    for (int i = 0; i < 240; i++) { int a, b; step(s, NULL, &a, &b); *sold += a; *items += b; }
    setnum(s, "time", time);
}
