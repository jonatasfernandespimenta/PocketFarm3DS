// Factory simulation (factory.js): discrete steps on a tile grid. Machines are the state's JSON objects.
// Belts carry one item per tile into chests / sell boxes / other belts. Machines are fed by robotic arms
// (or by hand) and push their output out of their `dir` side. Some machines need power from generators.
#include <stdio.h>
#include "game.h"

#define CAP 20        // max items queued inside a machine
#define FUEL_CAP 50   // coal a machine can stock
#define HOARD 99      // a machine out of fuel keeps taking ore until coal shows up (no deadlock on a mixed lane)
#define POLE_RANGE 6
#define TUNNEL_RANGE 5
#define STUCK 6       // steps an item may sit still before it counts as stuck
#define VINE_GROW 7
#define VINE_YIELD 6
#define VINE_REST 3
#define DRONE_LOAD 4
#define DRONE_SPEED 0.5

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

// Footprint: `size` tiles wide and `rows` tall (rows defaults to size; low pieces like counters are 1 row).
int rows(const char *type) {
    J *d = jget(table("MACHINES"), type), *r = jget(d, "rows");
    return cJSON_IsNumber(r) ? r->valuedouble : num(d, "size");
}
static int rowsOf(J *m) { return rows(str(m, "type")); }
int cells(J *m, Tile *out, int max) {
    int n = size(m), h = rowsOf(m), k = 0;
    for (int dy = 0; dy < h; dy++) for (int dx = 0; dx < n; dx++) if (k < max) out[k++] = (Tile){ (int)num(m, "x") + dx, (int)num(m, "y") + dy };
    return k;
}
bool machineCovers(J *m, int x, int y) {
    int n = size(m), h = rowsOf(m), mx = num(m, "x"), my = num(m, "y");
    return x >= mx && x < mx + n && y >= my && y < my + h;
}
J *machineAt(J *s, int x, int y) { JFOR(m, jget(s, "machines")) if (machineCovers(m, x, y)) return m; return NULL; }

// isFree(x, y) → tile is walkable ground with nothing on it (checked by the caller against the map)
bool canPlace(J *s, const char *type, int x, int y, bool (*isFree)(int, int)) {
    int n = num(jget(table("MACHINES"), type), "size"), h = rows(type);
    for (int dy = 0; dy < h; dy++) for (int dx = 0; dx < n; dx++)
        if (!isFree(x + dx, y + dy) || machineAt(s, x + dx, y + dy) || soilAt(s, x + dx, y + dy)) return false;
    return true;
}

J *place(J *s, const char *type, int x, int y, int dir, double nowMs) {
    J *ms = childarr(s, "machines"), *m = cJSON_Parse("{\"input\":{},\"out\":[],\"fuel\":0,\"progress\":0,\"cur\":null,\"item\":null}");
    J *def = jget(table("MACHINES"), type);
    cJSON *id = cJSON_CreateNumber(fmod(nowMs, 1e9) + jlen(ms));
    cJSON_AddItemToObject(m, "id", id);
    setstr(m, "type", type); setnum(m, "x", x); setnum(m, "y", y); setstr(m, "dir", DIR_NAME[dir]);
    if (has(def, "store")) child(m, "stock");
    if (has(def, "dock")) cJSON_AddItemToArray(childarr(m, "drones"), newDrone(m));
    if (has(def, "station")) child(m, "st");
    // a vine that was picked up comes back as grown as it was (the oldest first)
    if (has(def, "vine")) {
        J *ages = jget(s, "vineAges"), *st = child(m, "st");
        double age = 0;
        if (jlen(ages)) { J *a = cJSON_DetachItemFromArray(ages, 0); age = numv(a); cJSON_Delete(a); }
        setnum(st, "age", age); setbool(st, "fruit", false); setnum(st, "wait", 0);
    }
    cJSON_AddItemToArray(ms, m);
    return m;
}

static int cmpDesc(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? 1 : x > y ? -1 : 0; }

// Pick a machine back up: its contents go to the backpack.
void pickUp(J *s, J *m) {
    J *def = mdef(m);
    if (has(def, "vine")) {
        J *ages = childarr(s, "vineAges");
        cJSON_AddItemToArray(ages, cJSON_CreateNumber(num(jget(m, "st"), "age")));
        int n = jlen(ages), i = 0;
        double *v = malloc(sizeof *v * n);
        JFOR(a, ages) v[i++] = numv(a);
        qsort(v, n, sizeof *v, cmpDesc);
        i = 0; JFOR(a, ages) cJSON_SetNumberValue(a, v[i++]);
        free(v);
    }
    if (has(jget(m, "st"), "cooking") && strcmp(cookState(s, m), "cooking")) collectCooked(s, m);   // done: take the bread/curd, not the dough
    J *sc = stationContents(m);
    JFOR(it, sc) give(s, it->string, numv(it));
    cJSON_Delete(sc);
    JFOR(it, jget(m, "stock")) give(s, it->string, numv(it));
    JFOR(a, jget(m, "aging")) give(s, str(a, "id"), 1);
    JFOR(d, jget(m, "drones")) JFOR(it, jget(d, "carry")) give(s, it->valuestring, 1);
    // extra drones (the dock comes with one) are refunded
    J *cost = droneCost();
    for (int i = 1; i < jlen(jget(m, "drones")); i++) {
        setnum(s, "money", num(s, "money") + num(cost, "money"));
        give(s, "iron_bar", num(cost, "iron_bar")); give(s, "copper_bar", num(cost, "copper_bar"));
    }
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
static bool carries(J *m) { const char *t = str(m, "type"); return !strcmp(t, "belt") || !strcmp(t, "splitter") || !strcmp(t, "tunnel") || !strcmp(t, "sorter"); }
static bool is(J *m, const char *type) { return !strcmp(str(m, "type"), type); }
static bool truthy(const char *v) { return v && *v; }
static void bump(J *s, const char *stat) { J *st = child(s, "stats"); setnum(st, stat, num(st, stat) + 1); }

// ---------- warehouses ----------
static bool inList(const char *item, const char *const *list) { for (; *list; list++) if (!strcmp(item, *list)) return true; return false; }
static const char *const GRAINS[] = { "wheat", "corn", "flour", NULL };
static const char *const PRESERVES[] = { "cheese", "mayo", "sauce", "bread", "yarn", "bottled_must", "wine", "curd", "fresh_cheese", NULL };
static const char *const FRESH[] = { "egg", "milk", "mushroom", "leek", "berries", "wild_grapes", "winter_root", "grape", NULL };
const char *storeFor(const char *item) {
    if (inList(item, GRAINS) || !strncmp(item, "seed_", 5)) return "silo";
    if (inList(item, PRESERVES)) return "cellar";
    if (inList(item, FRESH) || jget(table("CROPS"), item) || !strncmp(item, "fish_", 5)) return "cold";
    return "shed";
}
int stored(J *m) { int n = 0; JFOR(it, jget(m, "stock")) n += numv(it); return n + jlen(jget(m, "aging")); }
// AGING (dumped from factory.js): { item: [product, nights] } — what the cellar turns into what, and after how many nights
// Night: every bottle and cheese in a cellar ages one night; when ready it joins the stock as the product.
void ageCellars(J *s) {
    J *aging = table("AGING");
    JFOR(m, jget(s, "machines")) {
        J *ag = jget(m, "aging");
        if (!jlen(ag)) continue;
        JFOR(a, ag) setnum(a, "nights", num(a, "nights") + 1);
        JFOR(a, ag) {
            J *rule = jget(aging, str(a, "id"));
            if (num(a, "nights") >= numv(jat(rule, 1))) { J *st = child(m, "stock"); const char *p = jat(rule, 0)->valuestring; setnum(st, p, num(st, p) + 1); }
        }
        for (J *a = ag->child, *nx; a; a = nx) {
            nx = a->next;
            if (num(a, "nights") >= numv(jat(jget(aging, str(a, "id")), 1))) cJSON_Delete(cJSON_DetachItemViaPointer(ag, a));
        }
    }
}
bool canPickUp(J *m) { J *d = mdef(m); return !has(d, "fixed") && (!has(d, "store") || stored(m) == 0); }
static bool isFood(J *m) { static const char *const F[] = { "emill", "kneader", "ioven", "bottler", "cheesemaker", NULL }; return inList(str(m, "type"), F); }
// cargo only enters a warehouse from a piece entirely on its left
// ...and only through its mouth, the bottom-left tile, where the orange port is drawn
static bool fromLeft(J *src, J *t, int x, int y) {
    if (!has(mdef(t), "store")) return true;
    int tx = num(t, "x"), ty = num(t, "y");
    return num(src, "x") + size(src) - 1 < tx && x == tx && y == ty + rowsOf(t) - 1;
}
// what leaves through the grey port: the output filter's item, or the first stocked item (insertion order)
// (in "everything" mode, the first one the piece in front can take)
static const char *firstStock(J *m, J *front) {
    const char *f = str(m, "outFilter");
    if (truthy(f)) return num(jget(m, "stock"), f) > 0 ? f : NULL;
    JFOR(it, jget(m, "stock")) if (numv(it) > 0 && accepts(front, it->string)) return it->string;
    return NULL;
}
static void takeStock(J *m, const char *id) {
    J *st = jget(m, "stock");
    setnum(st, id, num(st, id) - 1);
    if (!num(st, id)) delkey(st, id);
}

bool accepts(J *m, const char *item) {
    J *def = mdef(m);
    if (carries(m)) return !str(m, "item");
    if (is(m, "chest") || is(m, "autosell")) return true;
    if (has(def, "store")) return !strcmp(storeFor(item), str(m, "type")) && stored(m) < num(def, "cap");
    if (is(m, "feeder")) return !strcmp(item, "feed") && num(m, "feed") < FEEDER_CAP;
    if (str(def, "fuel") && !strcmp(str(def, "fuel"), item)) return num(m, "fuel") < FUEL_CAP;
    return jget(jget(def, "recipes"), item) && queued(m) < (str(def, "fuel") && !num(m, "fuel") ? HOARD : CAP);
}

// Returns money earned (autosell), else 0.
int insert(J *s, J *m, const char *item) {
    J *def = mdef(m);
    if (carries(m)) { setstr(m, "item", item); return 0; }
    if (is(m, "chest")) { cJSON_AddItemToArray(childarr(m, "out"), cJSON_CreateString(item)); return 0; }
    if (is(m, "cellar") && jget(table("AGING"), item)) {
        J *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "id", item); cJSON_AddNumberToObject(a, "nights", 0);
        cJSON_AddItemToArray(childarr(m, "aging"), a);
        return 0;
    }
    if (has(def, "store")) { J *st = child(m, "stock"); setnum(st, item, num(st, item) + 1); bump(s, "stored"); return 0; }
    if (is(m, "feeder")) { setnum(m, "feed", num(m, "feed") + 1); return 0; }
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

// Generators that are on, plus the poles linked to them: power reaches SUPPLY_RANGE around each of these.
// Returns how many generators lead the list (the rest are poles).
static int nodesOf(J *s, Set *nodes) {
    nodes->n = 0;
    JFOR(m, jget(s, "machines")) {
        J *d = mdef(m);
        if (num(d, "generator") && (!is(m, "solar") || isDay(s)) && (!str(d, "fuel") || num(m, "fuel") > 0)) add(nodes, m);
    }
    int gens = nodes->n;
    for (int i = 0; i < nodes->n; i++)
        JFOR(p, jget(s, "machines")) if (is(p, "pole") && !inSet(nodes, p) && near(nodes->m[i], p, POLE_RANGE)) add(nodes, p);
    return gens;
}
int powerNodes(J *s, J **out, int max) {
    Set nodes;
    nodesOf(s, &nodes);
    int k = 0;
    for (int i = 0; i < nodes.n && k < max; i++) out[k++] = nodes.m[i];
    return k;
}

static void powered(J *s, Set *out) {
    Set nodes;
    int gens = nodesOf(s, &nodes);
    double capacity = 0;
    for (int i = 0; i < gens; i++) capacity += num(mdef(nodes.m[i]), "generator");
    // ponytail: one shared capacity for all connected generators; split into separate grids if players ask
    out->n = 0;
    JFOR(m, jget(s, "machines")) {
        if (!has(mdef(m), "power") || capacity <= 0) continue;
        for (int i = 0; i < nodes.n; i++) if (near(nodes.m[i], m, SUPPLY_RANGE + (size(m) - 1))) { add(out, m); capacity--; break; }
    }
}

// ---------- problem indicators ----------
// What's wrong with a piece, for the bubble over it and the line in its menu: the code (or NULL) and the reason in `text`.
// codes: nopower, nofuel, full, refused (the next piece won't take this item), blocked (nothing takes it), idle.
static const char *nameOf(const char *id) { const char *n = str(jget(table("ITEMS"), id), "name"); return n ? n : id; }
#define ISSUE(c, ...) do { if (text && n > 0) snprintf(text, n, __VA_ARGS__); return c; } while (0)
static const char *whyNot(J *s, J *src, J *t, const char *item, int x, int y, char *text, int n) {
    if (!t) ISSUE("blocked", "Nothing takes it here: the line ends at this point.");
    J *def = mdef(t);
    if (carries(t)) { if (str(t, "item")) return NULL; ISSUE("blocked", "The piece in front isn't taking it."); }
    if (!is(src, "arm") && (jget(def, "recipes") || jget(def, "produce") || num(def, "generator")))
        ISSUE("refused", "Belts don't feed machines: put an arm between the belt and the machine.");
    if (has(def, "store")) {
        const char *where = storeFor(item);
        if (strcmp(where, str(t, "type"))) {
            const char *to = !strcmp(where, "silo") ? "the Silo" : !strcmp(where, "cold") ? "the Cold Room" : !strcmp(where, "cellar") ? "the Cellar" : "the Shed";
            ISSUE("refused", "%s doesn't store %s (it goes to %s).", str(def, "name"), nameOf(item), to);
        }
        if (!fromLeft(src, t, x, y)) ISSUE("refused", "Cargo only goes in through the orange mouth (bottom left of the building).");
        if (stored(t) >= num(def, "cap")) ISSUE("full", "%s is full.", str(def, "name"));
    }
    if (!accepts(t, item)) ISSUE("refused", "%s doesn't take %s%s.", str(def, "name"), nameOf(item), jget(def, "recipes") ? " (or its queue is full)" : "");
    return NULL;
}
static bool stuck(J *m) { J *at = jget(m, "at"); return !cJSON_IsNumber(at) || tick - at->valuedouble > STUCK; }
const char *machineIssue(J *s, J *m, char *text, int n) {
    J *def = mdef(m);
    if (text && n > 0) *text = 0;
    if (has(def, "power")) {
        J *u = jget(m, "unpowered");
        bool off;
        if (u && !cJSON_IsNull(u)) off = cJSON_IsTrue(u);
        else { Set p; powered(s, &p); off = !inSet(&p, m); }
        if (off) ISSUE("nopower", "No power: needs a solar panel, boiler or pole within 3 tiles.");
    }
    if (is(m, "sorter") && !truthy(str(m, "filter"))) ISSUE("idle", "Pick the filter: without one, everything goes straight on.");
    if (carries(m) && str(m, "item") && stuck(m)) {
        if (is(m, "belt")) { Tile a = ahead(m, mdir(m), 1); return whyNot(s, m, machineAt(s, a.x, a.y), str(m, "item"), a.x, a.y, text, n); }
        ISSUE("blocked", "Items aren't leaving this piece.");
    }
    if (is(m, "arm") && str(m, "item") && stuck(m)) { Tile a = ahead(m, mdir(m), 1); return whyNot(s, m, machineAt(s, a.x, a.y), str(m, "item"), a.x, a.y, text, n); }
    if (has(def, "store") && stored(m) >= num(def, "cap")) ISSUE("full", "%s is full: take things out through the grey mouth or build another.", str(def, "name"));
    if ((jget(def, "recipes") || jget(def, "produce")) && jlen(jget(m, "out")) >= CAP) ISSUE("full", "The output is full: connect a belt, chest or arm in front.");
    if (jget(def, "recipes") && str(def, "fuel") && !num(m, "fuel") && jlen(jget(m, "input"))) ISSUE("nofuel", "No %s.", nameOf(str(def, "fuel")));
    if (num(def, "generator") && str(def, "fuel") && !num(m, "fuel")) ISSUE("nofuel", "No %s: it makes no power.", nameOf(str(def, "fuel")));
    if (has(def, "dock")) {
        if (jlen(jget(m, "out")) >= DOCK_CAP) ISSUE("full", "The dock is full: the drones wait. Connect a belt to its output.");
        bool any = false;
        JFOR(d, jget(m, "drones")) if (truthy(str(d, "target"))) any = true;
        if (!any) ISSUE("idle", "Pick what the drones harvest.");
    }
    if (is(m, "feeder") && !(num(m, "feed") > 0)) ISSUE("idle", "Feeder is empty: bring feed to it by belt.");
    if (is(m, "collector") && jlen(jget(m, "out")) >= COLLECTOR_CAP) ISSUE("full", "The collector is full: connect a belt in front.");
    return NULL;
}

// ---------- grape vines ----------
// Night (after the day changed): vines grow; grown vines fruit in the fall, resting VINE_REST nights after a harvest.
void growVines(J *s, bool isFall) {
    JFOR(m, jget(s, "machines")) {
        if (!has(mdef(m), "vine")) continue;
        J *st = jget(m, "st");
        if (!cJSON_IsObject(st)) { st = child(m, "st"); setnum(st, "age", 0); setbool(st, "fruit", false); setnum(st, "wait", 0); }
        setnum(st, "age", num(st, "age") + 1);
        if (num(st, "age") < VINE_GROW || !isFall || has(st, "fruit")) continue;
        setnum(st, "wait", num(st, "wait") - 1);
        if (num(st, "wait") <= 0) setbool(st, "fruit", true);
    }
}
// Harvest into `carry` (a drone's load) or, when NULL, the backpack.
static int harvestInto(J *s, J *m, J *carry) {
    J *st = jget(m, "st");
    if (!has(st, "fruit")) return 0;
    setbool(st, "fruit", false); setnum(st, "wait", VINE_REST);
    if (carry) for (int i = 0; i < VINE_YIELD; i++) cJSON_AddItemToArray(carry, cJSON_CreateString("grape"));
    else give(s, "grape", VINE_YIELD);
    return VINE_YIELD;
}
int harvestVine(J *s, J *m) { return harvestInto(s, m, NULL); }

// ---------- harvest drones ----------
// Each dock's drones fly (0.5 tile a step, over everything) to the nearest ripe crop of their target, pick up to
// DRONE_LOAD, and bring it home. A full dock (DOCK_CAP) makes them wait holding their load; no power, they hover.
J *droneCost(void) { return table("DRONE_COST"); }
J *newDrone(J *dock) {
    J *d = cJSON_CreateObject();
    cJSON_AddNullToObject(d, "target");
    cJSON_AddNumberToObject(d, "x", num(dock, "x")); cJSON_AddNumberToObject(d, "y", num(dock, "y"));
    cJSON_AddArrayToObject(d, "carry");
    cJSON_AddStringToObject(d, "state", "home");
    cJSON_AddNullToObject(d, "claim");
    return d;
}
static double toward(double a, double b) { double d = b - a; return a + (d > 0 ? 1 : d < 0 ? -1 : 0) * fmin(DRONE_SPEED, fabs(d)); }
static bool fly(J *d, double x, double y) {
    setnum(d, "x", toward(num(d, "x"), x)); setnum(d, "y", toward(num(d, "y"), y));
    return num(d, "x") == x && num(d, "y") == y;
}
static void vineKey(J *v, char *buf, int n) {
    double id = num(v, "id");
    if (id == floor(id) && fabs(id) < 1e15) snprintf(buf, n, "vine:%.0f", id); else snprintf(buf, n, "vine:%.17g", id);
}
typedef struct { char k[256][40]; int n; } Claims;
static bool claimed(Claims *c, const char *k) { for (int i = 0; i < c->n; i++) if (!strcmp(c->k[i], k)) return true; return false; }
static void claim(Claims *c, const char *k) { if (c->n < 256) snprintf(c->k[c->n++], 40, "%s", k); }
static bool isState(J *d, const char *v) { const char *s = str(d, "state"); return s && !strcmp(s, v); }

static void stepDrones(J *s, J *m, bool hasPower) {
    if (!hasPower) return;
    static Claims cl;
    cl.n = 0;
    JFOR(o, jget(s, "machines")) JFOR(d, jget(o, "drones")) if (truthy(str(d, "claim"))) claim(&cl, str(d, "claim"));
    double mx = num(m, "x"), my = num(m, "y");
    J *out = childarr(m, "out");
    JFOR(d, jget(m, "drones")) {
        J *carry = childarr(d, "carry");
        const char *target = str(d, "target");
        if (isState(d, "return") || (!truthy(target) && jlen(carry))) {
            setstr(d, "state", "return");
            if (!fly(d, mx, my)) continue;
            if (jlen(carry) && jlen(out) < DOCK_CAP) cJSON_AddItemToArray(out, cJSON_DetachItemFromArray(carry, 0));
            if (!jlen(carry)) setstr(d, "state", "home");
            continue;
        }
        if (!truthy(target)) continue;
        J *tx = jget(d, "tx");
        if (truthy(str(d, "claim")) && (!tx || cJSON_IsNull(tx))) setstr(d, "claim", NULL);   // saved before claims carried their coordinates
        if (!truthy(str(d, "claim"))) {
            // targets: ripe soil tiles of the crop ('x,y'), or fruiting vines for grapes ('vine:<id>')
            char best[40] = ""; double bx = 0, by = 0, bd = INFINITY; bool found = false;
            double dx = num(d, "x"), dy = num(d, "y");
#define CONSIDER(K, X, Y) do { \
                if (claimed(&cl, K)) break; \
                double dist = fabs((X) - dx) + fabs((Y) - dy); \
                if (dist < bd || (dist == bd && ((Y) < by || ((Y) == by && (X) < bx)))) { snprintf(best, sizeof best, "%s", K); bx = X; by = Y; bd = dist; found = true; } \
            } while (0)
            if (!strcmp(target, "grape")) {
                JFOR(v, jget(s, "machines")) if (has(mdef(v), "vine") && has(jget(v, "st"), "fruit")) {
                    char k[40]; vineKey(v, k, sizeof k);
                    CONSIDER(k, num(v, "x"), num(v, "y"));
                }
            } else {
                JFOR(t, jget(s, "soil")) {
                    const char *crop = str(t, "crop");
                    if (!crop || strcmp(crop, target) || !isReady(t)) continue;
                    int x = 0, y = 0; sscanf(t->string, "%d,%d", &x, &y);
                    CONSIDER(t->string, (double)x, (double)y);
                }
            }
#undef CONSIDER
            if (!found) { if (jlen(carry)) setstr(d, "state", "return"); else fly(d, mx, my); continue; }
            setstr(d, "claim", best); setnum(d, "tx", bx); setnum(d, "ty", by); claim(&cl, best);
        }
        setstr(d, "state", "out");
        if (!fly(d, num(d, "tx"), num(d, "ty"))) continue;
        char k[40]; snprintf(k, sizeof k, "%s", str(d, "claim"));
        if (!strncmp(k, "vine:", 5)) {
            JFOR(v, jget(s, "machines")) { char vk[40]; vineKey(v, vk, sizeof vk); if (!strcmp(vk, k)) { harvestInto(s, v, carry); break; } }
        } else {
            J *t = jget(jget(s, "soil"), k);
            if (isReady(t) && !strcmp(str(t, "crop"), target)) {
                cJSON_AddItemToArray(carry, cJSON_CreateString(str(t, "crop")));
                delkey(t, "crop"); delkey(t, "age");
                bump(s, "harvested"); bump(s, "droneHarvest");
            }
        }
        setstr(d, "claim", NULL);
        if (jlen(carry) >= DRONE_LOAD) setstr(d, "state", "return");
    }
}

// ---------- step ----------

// Move one item from `src` toward the tile in front of it. Returns false if it can't, else adds to *sold.
static bool push(J *s, J *src, Tile tl, Set *moved, int *sold) {
    J *t = at(s, tl);
    if (!t || t == src || !str(src, "item")) return false;
    J *td = mdef(t);
    // belts don't feed processing machines: that's the robotic arm's job
    if (jget(td, "recipes") || jget(td, "produce") || num(td, "generator")) return false;
    if (!accepts(t, str(src, "item")) || !fromLeft(src, t, tl.x, tl.y)) return false;
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
        if (has(def, "store")) child(m, "stock");   // blueprint previews and sandboxes build bare machine objects
        if (has(def, "dock")) childarr(m, "drones");
        int dir = mdir(m);
        if (is(m, "belt") && str(m, "item")) push(s, m, ahead(m, dir, 1), &moved, &sold);
        else if (is(m, "sorter") && str(m, "item")) {
            // the filtered item turns to its side (left by default, relative to the way it faces); the rest goes on
            const char *f = str(m, "filter"), *side = str(m, "side");
            bool turn = truthy(f) && !strcmp(str(m, "item"), f);
            int d = turn ? SIDES[dir][side && !strcmp(side, "right") ? 1 : 0] : dir;
            if (push(s, m, ahead(m, d, 1), &moved, &sold) && turn) { bump(s, "routed"); bump(s, "sorted"); }
        } else if (is(m, "splitter") && str(m, "item")) {
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
            Tile fa = ahead(m, dir, 1);
            J *back = at(s, ahead(m, BACK[dir], 1)), *front = at(s, fa);
            if (!str(m, "item") && back && front && back != front) {
                bool store = has(mdef(back), "store");
                // a warehouse only hands cargo to an arm at its grey port (right of its bottom row)
                const char *next = store ? (num(m, "x") >= num(back, "x") + size(back) && num(m, "y") == num(back, "y") + rowsOf(back) - 1 ? firstStock(back, front) : NULL)
                    : carries(back) ? str(back, "item") : !is(back, "autosell") ? firstOut(back) : NULL;
                if (next && accepts(front, next) && fromLeft(m, front, fa.x, fa.y)) {
                    char nx[32]; snprintf(nx, sizeof nx, "%s", next);
                    if (store) takeStock(back, nx); else if (carries(back)) setstr(back, "item", NULL); else shiftOut(back);
                    setstr(m, "item", nx); setnum(m, "at", tick);
                }
            } else if (str(m, "item") && front && accepts(front, str(m, "item")) && fromLeft(m, front, fa.x, fa.y)) {
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
        } else if (has(def, "dock")) {
            stepDrones(s, m, inSet(&power, m));
        } else if (jget(def, "recipes")) {
            J *recipes = jget(def, "recipes"), *input = child(m, "input"), *out = childarr(m, "out");
            const char *fuel = str(def, "fuel");
            bool hasPower = !has(def, "power") || inSet(&power, m);
            // recipes that moved to another machine (old saves): hand the ingredient back out
            if (str(m, "cur") && !jget(recipes, str(m, "cur"))) { cJSON_AddItemToArray(out, cJSON_CreateString(str(m, "cur"))); setstr(m, "cur", NULL); }
            for (J *it = input->child, *nx; it; it = nx) {
                nx = it->next;
                if (jget(recipes, it->string) || (fuel && !strcmp(it->string, fuel))) continue;
                for (int i = 0; i < numv(it); i++) cJSON_AddItemToArray(out, cJSON_CreateString(it->string));
                cJSON_Delete(cJSON_DetachItemViaPointer(input, it));
            }
            if (!str(m, "cur") && hasPower) {
                J *next = NULL;
                JFOR(it, input) if (numv(it) > 0) { next = it; break; }
                if (next && (!fuel || num(m, "fuel") > 0) && jlen(out) < CAP) {
                    char id[32]; snprintf(id, sizeof id, "%s", next->string);
                    setstr(m, "cur", id);
                    setnum(m, "progress", 0);
                    setnum(input, id, numv(next) - 1);
                    if (!num(input, id)) delkey(input, id);
                    if (fuel) setnum(m, "fuel", num(m, "fuel") - 1);
                }
            }
            if (str(m, "cur") && hasPower) {
                setnum(m, "progress", num(m, "progress") + STEP);
                J *rec = jget(recipes, str(m, "cur"));
                if (num(m, "progress") >= numv(jat(rec, 1))) {
                    cJSON_AddItemToArray(out, cJSON_CreateString(jat(rec, 0)->valuestring));
                    setstr(m, "cur", NULL); items++;
                    if (has(def, "power")) bump(s, "poweredWork");
                    if (isFood(m)) bump(s, "foodMade");
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
        if (jlen(jget(m, "out")) && (jget(def, "recipes") || jget(def, "produce") || has(def, "dock") || is(m, "collector"))) {
            Tile oc = outputCell(m);
            J *t = at(s, oc);
            if (t && t != m && accepts(t, firstOut(m)) && fromLeft(m, t, oc.x, oc.y)) {
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
