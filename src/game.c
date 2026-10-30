// The game loop (main.js): camera, drawing, interaction, tools, fishing, maps.
// ponytail: tap-to-walk (onTap / findPath / stepWalk) isn't ported: the 3DS top screen isn't touchable.
// Decorating taps the tile in front of the player (A) instead of a tile under the finger.
#include <stdio.h>
#include "game.h"

// ponytail: the web picks k = round(screen short side / 176) = 1 here; the 3DS screen is small, so 2x (integer
// like the web: no shimmer). The world view is 200x120.
#ifndef ZOOM
#define ZOOM 2
#endif
#define VW (400 / ZOOM)
#define VH (240 / ZOOM)
#define T(name) table(name)
static J *ITEM(const char *id) { return jget(T("ITEMS"), id); }
static const char *itemName(const char *id) { return str(ITEM(id), "name"); }
static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static bool starts(const char *s, const char *p) { return s && !strncmp(s, p, strlen(p)); }

J *state;
double gtime;   // seconds since boot (main.js `time`)

// ---------- world ----------
typedef struct Ent {
    J *d; bool owned;          // definition (ENTITIES row or one made here)
    const char *type;
    int x, y, w, h;
    bool hasWander; Rect wander;
    double px, py, t, wait, tx, ty; int dir; bool moving, hasTarget;
    bool hasCol; Rect col;
} Ent;
typedef struct World {
    char id[16]; J *def; Map *map;
    Ent ents[64]; int nents;
    Node nodes[40]; int nnodes;
    bool hasLadder; Tile ladder;
} World;
static World W_, *world;
static Img *WATER[16], *SOILDRY[16], *SOILWET[16];
typedef struct { bool on; char phase[8]; double t, wait; Tile tile; Fish result; } Fishing;
static Fishing fishing;
static double simAcc, warpLock, stepDist;
static bool transitioning, dirty, leaving;
typedef struct { double x, y, t; const char *sprite; } Bubble;
static Bubble bubbles[16]; static int nbubbles;
static struct { double x, y; int dir; } mascot = { 0, 0, LEFT };
static struct { int x, y; } cam;
static struct { bool on; char item[40]; bool carrying; char carried[40]; J *saved; } decor;
typedef void (*Popup)(void *);
static struct { Popup fn; char arg[64]; } popups[16]; static int npopups;
static void popup(Popup fn, const void *arg, size_t n) { if (npopups < 16) { popups[npopups].fn = fn; memcpy(popups[npopups].arg, arg, n); npopups++; } }

double nowMs(void);   // platform clock (Date.now())
double profNow(void); // ms, for the debug timings
double prof[4];

// ---------- save ----------
void persist(void) {
    if (leaving) return;
    setnum(state, "px", player.x); setnum(state, "py", player.y);
    saveState(state, nowMs());
    dirty = false;
}

static void syncHotbar(bool force);
static const char *mapName(void) {
    static char b[32];
    if (!world) return "";
    if (eq(world->id, "mine")) { snprintf(b, sizeof b, "Mine %d", (int)num(state, "mineLevel")); return b; }
    return str(world->def, "name");
}
const char *game_mapName(void) { return mapName(); }

static void questDonePopup(void *arg) { shops_questDone(*(J **)arg); }
void changed(void) {
    setMuted(has(state, "muted"));
    setMusicEnabled(!has(state, "musicOff"));
    J *goals[16]; int n = checkGoals(state, goals, 16);
    for (int i = 0; i < n; i++) { toast(":trophy: Goal: %s! +%d :ticket:", str(goals[i], "name"), (int)num(goals[i], "tickets")); sfx("catch"); }
    J *done[16]; n = checkQuests(state, done, 16);
    for (int i = 0; i < n; i++) popup(questDonePopup, &done[i], sizeof(J *));
    if (world) syncHotbar(false);
    hud_render();
    dirty = true;
}

static bool spend(int n) {
    if (num(state, "energy") < n) { toast("No energy… sleep at home or eat at the Inn."); sfx("miss"); return false; }
    setnum(state, "energy", num(state, "energy") - n);
    return true;
}

// ---------- tools ----------
static int ownedMachines(const char **out, int max) {
    int n = 0;
    JFOR(k, jget(state, "inv")) if (starts(k->string, "machine_") && numv(k) > 0 && n < max) out[n++] = k->string + 8;
    return n;
}
static int ownedSeeds(const char **out, int max) {
    int n = 0;
    JFOR(c, T("CROPS")) { char k[40]; snprintf(k, sizeof k, "seed_%s", c->string); if (count(state, k) > 0 && n < max) out[n++] = c->string; }
    return n;
}

// Seeds and the build tool only show while you have something to use; an item that runs out leaves the bar.
int toolList(const char **out) {
    const char *tmp[32]; int n = 0;
    JFOR(t, T("TOOL_ORDER")) {
        const char *id = t->valuestring;
        bool show = eq(id, "rod") ? num(jget(state, "tools"), "rod") > 0 : eq(id, "build") ? ownedMachines(tmp, 32) > 0 : eq(id, "seeds") ? ownedSeeds(tmp, 32) > 0 : true;
        if (show) out[n++] = id;
    }
    return n;
}
static bool inToolList(const char *id) { const char *l[16]; int n = toolList(l); for (int i = 0; i < n; i++) if (eq(l[i], id)) return true; return false; }

static char hotbarKey[256];
static void syncHotbar(bool force) {
    char k[48];
    snprintf(k, sizeof k, "seed_%s", str(state, "seed"));
    const char *l[32];
    if (!count(state, k)) { int n = ownedSeeds(l, 32); if (n) setstr(state, "seed", l[0]); }
    snprintf(k, sizeof k, "machine_%s", str(state, "build") ? str(state, "build") : "");
    if (!str(state, "build") || !count(state, k)) { int n = ownedMachines(l, 32); setstr(state, "build", n ? l[0] : NULL); }
    if (!inToolList(str(state, "tool"))) setstr(state, "tool", "hoe");
    char key[256] = "";
    int n = toolList(l);
    for (int i = 0; i < n; i++) { strncat(key, l[i], 200); strcat(key, ","); }
    char tail[64]; snprintf(tail, sizeof tail, "%s%s", str(state, "seed"), str(state, "build") ? str(state, "build") : "null");
    strncat(key, tail, 40);
    JFOR(t, jget(state, "tools")) { snprintf(tail, sizeof tail, "%d,", (int)numv(t)); strncat(key, tail, 8); }
    if (!force && !strcmp(key, hotbarKey)) return;
    snprintf(hotbarKey, sizeof hotbarKey, "%s", key);
    hud_buildHotbar(l, n);
}

void refreshTools(void) { syncHotbar(true); changed(); }

// Put a seed packet or a machine in the hand (from the picker or the backpack).
void equip(const char *id) {
    if (starts(id, "seed_")) { setstr(state, "seed", id + 5); setstr(state, "tool", "seeds"); }
    else if (starts(id, "machine_")) { setstr(state, "build", id + 8); setstr(state, "tool", "build"); setstr(state, "blueprint", NULL); }
    else if (starts(id, "bp:")) { setnum(state, "blueprint", atoi(id + 3)); setstr(state, "tool", "build"); }
    hud_closeMenu();
    refreshTools();
}

static bool pickEquipRow(void *arg) { equip((const char *)arg); return false; }
// Tapping the active seeds / build slot again opens a picker with what you own.
static void pickEquip(const char *kind) {
    bool seeds = eq(kind, "seeds");
    menu_begin(seeds ? ":bag: Pick a seed" : ":wrench: Pick a machine");
    const char *l[32]; int n = seeds ? ownedSeeds(l, 32) : ownedMachines(l, 32);
    for (int i = 0; i < n; i++) {
        char id[48]; snprintf(id, sizeof id, seeds ? "seed_%s" : "machine_%s", l[i]);
        char name[96], info[160];
        snprintf(name, sizeof name, "%s ×%d", itemName(id), count(state, id));
        if (seeds) snprintf(info, sizeof info, "%d days · %s", (int)num(jget(T("CROPS"), l[i]), "days"), inSeason(state, l[i]) ? "grows this season" : "out of season");
        else snprintf(info, sizeof info, "%s", str(jget(T("MACHINES"), l[i]), "desc"));
        Row *r = menu_row(name, info, pickEquipRow, id, strlen(id) + 1);
        char ic[56]; snprintf(ic, sizeof ic, "item:%s", id); row_icon(r, ic);
    }
    if (!seeds) {
        int i = 0;
        JFOR(bp, jget(state, "blueprints")) {
            char id[16], info[200]; snprintf(id, sizeof id, "bp:%d", i++);
            snprintf(info, sizeof info, "Blueprint by %s: places every piece you have at once. The top-left corner goes on the tile in front.", str(bp, "from") ? str(bp, "from") : "you");
            Row *r = menu_row(str(bp, "name"), info, pickEquipRow, id, strlen(id) + 1);
            row_icon(r, ":book:");
        }
    }
    menu_end();
}

void selectTool(const char *id) {
    if (eq(id, str(state, "tool")) && (eq(id, "seeds") || eq(id, "build"))) { pickEquip(id); return; }
    setstr(state, "tool", id);
    refreshTools();
}

// Tiles affected by hoe / watering can at their level.
static int areaFor(const char *tool, Tile t, Tile *out) {
    int lvl = num(jget(state, "tools"), tool) ? num(jget(state, "tools"), tool) : 1, n = 0;
    if (lvl == 1) { out[0] = t; return 1; }
    if (lvl == 3) { for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) out[n++] = (Tile){ t.x + dx, t.y + dy }; return n; }
    for (int i = 0; i < 3; i++) out[n++] = (Tile){ t.x + DV[player.dir][0] * i, t.y + DV[player.dir][1] * i };
    return n;
}

static void act(const char *tool, void (*fn)(void *), const void *arg, size_t n, double dur) {
    J *d = jget(T("TOOLS"), tool);
    if (!d) d = jget(T("TOOLS"), "hand");
    ActOpts o = actDefaults();
    o.row = jget(d, "anim") ? num(d, "anim") : 41;
    if (jget(d, "overlay")) { o.overlay = 0; o.ovRow = num(d, "overlay"); }
    if (dur) o.dur = dur;
    Thunk th = { fn };
    if (arg) memcpy(th.arg, arg, n);
    startAction(&player, o, fn ? &th : NULL);
}

// ---------- world queries ----------
static bool inRectT(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

// Tile-sized things that block (buildings use pixel colliders instead, see addBuildingColliders).
static bool entitySolid(int x, int y) {
    for (int i = 0; i < world->nents; i++) {
        Ent *e = &world->ents[i];
        if ((eq(e->type, "sell") || eq(e->type, "gacha")) && e->x == x && e->y == y) return true;
        if ((eq(e->type, "board") || eq(e->type, "deco")) && y == e->y && x >= e->x && x < e->x + e->w) return true;
        if (eq(e->type, "npc") && !e->hasWander && e->x == x && e->y == y) return true;
    }
    if (eq(world->id, "farm")) { J *m = machineAt(state, x, y); if (m && !isFlat(m)) return true; }
    for (int i = 0; i < world->nnodes; i++) if (world->nodes[i].x == x && world->nodes[i].y == y) return true;
    return false;
}
bool isFlat(J *m) { const char *t = str(m, "type"); return eq(t, "belt") || eq(t, "splitter") || eq(t, "tunnel"); }
// Pathfinding grid (tile level) and movement test (pixel box).
static bool solid(int x, int y) { return isBlocked(world->map, x, y) || entitySolid(x, y); }
static bool blocked(Rect box) {
    if (boxBlocked(world->map, box)) return true;
    for (int y = floor(box.y / 16); y <= floor((box.y + box.h - 0.01) / 16); y++)
        for (int x = floor(box.x / 16); x <= floor((box.x + box.w - 0.01) / 16); x++) if (entitySolid(x, y)) return true;
    return false;
}
static bool freeForBuild(int x, int y) {
    Prop *p;
    return inBounds(world->map, x, y) && !solid(x, y) && !isSolid(world->map, x, y) && !((p = propAt(world->map, x, y)) && p->hasCol);
}

// Where a building image is drawn, and its collider from the opaque pixels.
typedef struct { int sx, sw, sh, dx, dy; } BRect;
static BRect buildingRect(J *e, Img *im) {
    int sw = num(e, "sw") ? num(e, "sw") : im->w, sh = num(e, "sh") ? num(e, "sh") : im->h;
    return (BRect){ num(e, "sx"), sw, sh, jround(num(e, "x") * 16 + (num(e, "w") * 16 - sw) / 2.0), (num(e, "y") + num(e, "h")) * 16 - sh };
}
static void buildingCollider(J *e, Rect *out) {
    Img *im = I(str(e, "img"));
    BRect r = buildingRect(e, im);
    int rect[4] = { r.sx, 0, r.sw, r.sh };
    maskCollider(str(e, "mask") ? str(e, "mask") : "building", opaqueBox(im, rect), r.dx, r.dy, out);
}
// Farm buildings (coop, barn) the player has built. ponytail: like the web build, these never get a stored
// collider (builtBuildings() makes fresh objects), so they aren't tappable and sort by their footprint.
static int builtBuildings(J **out) {
    int n = 0;
    if (!eq(world->id, "farm")) return 0;
    JFOR(b, T("BUILDINGS")) if (has(jget(state, "buildings"), b->string)) out[n++] = b;
    return n;
}
static void addBuildingColliders(void) {
    Map *m = world->map;
    m->nextra = 0;
    for (int i = 0; i < world->nents; i++) {
        Ent *e = &world->ents[i];
        if (!eq(e->type, "building")) continue;
        buildingCollider(e->d, &e->col);
        e->hasCol = true;
        if (m->nextra < 16) m->extra[m->nextra++] = e->col;
    }
    J *bs[8]; int n = builtBuildings(bs);
    for (int i = 0; i < n && m->nextra < 16; i++) buildingCollider(bs[i], &m->extra[m->nextra++]);
    rebuildSolid(m);
}

static Tile tileOf(double px, double py) { return (Tile){ floor(px / 16), floor((py - 3) / 16) }; }

// What's on a tile, for interaction.
enum { TH_NONE, TH_NPC, TH_NODE, TH_LADDER, TH_ANIMAL, TH_MACHINE, TH_FARMBUILDING, TH_BUILDING, TH_SELL, TH_GACHA, TH_BOARD, TH_WARP, TH_PROP, TH_OBJECT };
typedef struct { int kind; Ent *e; Node *n; J *j; Prop *p; } Thing;
static Thing thingAt(int x, int y) {
    Rect tile = { x * 16, y * 16, 16, 16 };
    for (int i = 0; i < world->nents; i++) { Ent *e = &world->ents[i]; Tile t = tileOf(e->px, e->py); if (eq(e->type, "npc") && t.x == x && t.y == y) return (Thing){ TH_NPC, e }; }
    for (int i = 0; i < world->nnodes; i++) if (world->nodes[i].x == x && world->nodes[i].y == y) return (Thing){ TH_NODE, .n = &world->nodes[i] };
    if (world->hasLadder && world->ladder.x == x && world->ladder.y == y) return (Thing){ TH_LADDER };
    if (eq(world->id, "farm")) {
        JFOR(a, jget(state, "animals")) if (cJSON_IsNumber(jget(a, "px"))) { Tile t = tileOf(num(a, "px"), num(a, "py")); if (t.x == x && t.y == y) return (Thing){ TH_ANIMAL, .j = a }; }
        J *m = machineAt(state, x, y);
        if (m) return (Thing){ TH_MACHINE, .j = m };
    }
    for (int i = 0; i < world->nents; i++) {
        Ent *e = &world->ents[i];
        if (eq(e->type, "building") && e->hasCol && rectsOverlap(e->col, tile)) return (Thing){ TH_BUILDING, e };
        if ((eq(e->type, "sell") || eq(e->type, "gacha")) && e->x == x && e->y == y) return (Thing){ eq(e->type, "sell") ? TH_SELL : TH_GACHA, e };
        if (eq(e->type, "board") && y == e->y && x >= e->x && x < e->x + e->w) return (Thing){ TH_BOARD, e };
        if (eq(e->type, "deco") && str(e->d, "action") && y == e->y && x >= e->x && x < e->x + e->w) return (Thing){ TH_BUILDING, e };
        if (eq(e->type, "warp") && has(e->d, "interact") && inRectT(x, y, e->x, e->y, e->w, e->h)) return (Thing){ TH_WARP, e };
    }
    Prop *p = propAt(world->map, x, y);
    if (p && !eq(p->kind, "flower")) return (Thing){ TH_PROP, .p = p };
    return (Thing){ TH_NONE };
}

static bool isActionableTile(Tile t);   // tap-to-walk only: not ported

// ---------- interaction ----------
static void goTo(const char *id, bool hasSpawn, double sx, double sy);
static void useTool(Tile t);
static void swingProp(const char *tool, Tile t, Prop *p);
static void swingNode(Tile t, Node *n);
static void pickForage(Prop *p, Tile t);
static void descend(void);
static void drinkWell(void);
static void askSleep(void);
static void farmBuildingMenu(J *b);
void machineMenu(J *m);
static void startFishing(Tile t);
static void shopFor(const char *action);

static const char *TOOL_NAME(const char *t) { return eq(t, "axe") ? "the axe" : eq(t, "pickaxe") ? "the pickaxe" : "the hoe"; }

static void npcShop(void *arg) { shopFor((const char *)arg); }
static void interact(Tile t) {
    if (player.acting) return;
    Thing th = thingAt(t.x, t.y);
    switch (th.kind) {
    case TH_NPC: {
        Ent *r = th.e;
        r->dir = r->px < player.x - 4 ? RIGHT : r->px > player.x + 4 ? LEFT : r->py < player.y ? DOWN : UP;
        const char *a = str(r->d, "action");
        shops_npcMenu(r->d, a ? npcShop : NULL, a);
        return;
    }
    case TH_BUILDING: {
        J *r = th.e->d;
        if (str(r, "interior")) { goTo(str(r, "interior"), false, 0, 0); return; }
        if (!str(r, "action")) { toast("%s", jat(jget(r, "lines"), 0) ? jat(jget(r, "lines"), 0)->valuestring : str(r, "name")); return; }
        if (eq(str(r, "action"), "well")) { drinkWell(); return; }
        if (eq(str(r, "action"), "blocked")) { toast("%s", jat(jget(r, "lines"), 0)->valuestring); return; }
        if (eq(str(r, "action"), "fountain")) { toast(":sparkle: Pocket Town's fountain. You rebuilt all of this!"); return; }
        sfx("door"); shopFor(str(r, "action"));
        return;
    }
    case TH_SELL: shops_sellMenu(); return;
    case TH_GACHA: gacha_open(str(th.e->d, "set")); return;
    case TH_BOARD: shops_boardMenu(); return;
    case TH_WARP: { J *sp = jget(th.e->d, "spawn"); goTo(str(th.e->d, "to"), sp != NULL, num(sp, "x"), num(sp, "y")); return; }
    case TH_LADDER: descend(); return;
    case TH_FARMBUILDING: farmBuildingMenu(th.j); return;
    case TH_ANIMAL: {
        J *a = th.j;
        faceTile(&player, t);
        toast("%s", touchAnimal(state, a));
        if (nbubbles < 16) bubbles[nbubbles++] = (Bubble){ num(a, "px"), num(a, "py") - 26, 1.2, "heart" };
        sfx("pop");
        changed();
        return;
    }
    case TH_MACHINE: machineMenu(th.j); return;
    case TH_NODE:
        if (!eq(str(state, "tool"), "pickaxe")) { toast("Use the pickaxe to break it."); return; }
        swingNode(t, th.n);
        return;
    case TH_PROP: {
        Prop *r = th.p;
        if (eq(r->kind, "house")) { goTo("house", false, 0, 0); return; }
        if (eq(r->kind, "bed")) { askSleep(); return; }
        if (eq(r->kind, "critter")) {
            if (nbubbles < 16) bubbles[nbubbles++] = (Bubble){ (r->x + 0.5) * 16, r->foot - 26, 1.2, "heart" };
            sfx("pop");
            const char *name = "undefined";
            JFOR(c, T("CRITTERS")) { char k[24]; snprintf(k, sizeof k, "critter:%d", (int)num(c, "id")); if (eq(k, r->item)) name = str(c, "name"); }
            toast("%s :heart:", name);
            return;
        }
        if (eq(r->kind, "furniture")) {
            const char *n = str(jget(T("FURNITURE"), r->item), "name");
            toast("%s%s", n ? n : "Furniture", world->map->isHouse ? " · tap :chair: Decorate to move it" : "");
            return;
        }
        if (eq(r->kind, "forage")) { pickForage(r, t); return; }
        const char *need = str(jget(T("OBJECT_KINDS"), r->kind), "tool");
        if (eq(r->kind, "grass")) break;   // grass: the hoe (below) clears it
        if (need && eq(str(state, "tool"), need)) { swingProp(need, t, r); return; }
        if (need) { toast("Use %s.", TOOL_NAME(need)); return; }
        return;
    }
    }
    useTool(t);
}

static void shopFor(const char *a) {
    if (eq(a, "inn")) shops_innMenu();
    else if (eq(a, "forge")) shops_forgeMenu();
    else if (eq(a, "carpenter")) shops_carpenterMenu();
    else if (eq(a, "fish")) shops_fishMenu();
    else if (eq(a, "market")) shops_marketMenu();
}

typedef struct { int x, y; char crop[24]; } HarvestArg;
static void flyItem(const char *id, double x, double y) { fx_fly(id, x, y); }
static void harvestHit(void *arg) {
    HarvestArg *a = arg;
    J *tile = soilAt(state, a->x, a->y);
    int n = hasBonus(state, "luck") && frand() < 0.1 ? 2 : 1;
    give(state, a->crop, n);
    floatText(a->x * 16 + 8, a->y * 16, "+%d %s", n, str(jget(T("CROPS"), a->crop), "name"));
    flyItem(a->crop, a->x * 16 + 8, a->y * 16 + 6);
    fx_sparkle(a->x * 16 + 8, a->y * 16 + 8, 5, 0xfee761ff);
    fx_puff(a->x * 16 + 8, a->y * 16 + 13, 0xc9a77aff, 3);
    J *st = jget(state, "stats"); setnum(st, "harvested", num(st, "harvested") + 1);
    delkey(tile, "crop"); delkey(tile, "age");
    sfx("pop"); changed();
}
static void tillTile(Tile a);
static void hoeHit(void *arg) {
    Tile t = *(Tile *)arg, area[9];
    int n = areaFor("hoe", t, area);
    for (int i = 0; i < n; i++) { tillTile(area[i]); fx_puff(area[i].x * 16 + 8, area[i].y * 16 + 11, 0xc9a77aff, 4); }
    fx_shake(1, 0.08); sfx("hoe"); changed();
}
typedef struct { Tile t[9]; int n; } Targets;
static void canHit(void *arg) {
    Targets *tg = arg;
    for (int i = 0; i < tg->n; i++) { setbool(soilAt(state, tg->t[i].x, tg->t[i].y), "wet", true); fx_splash(tg->t[i].x * 16 + 8, tg->t[i].y * 16 + 10, 7); }
    sfx("water"); changed();
}
typedef struct { Tile t; char id[40], seed[24]; } SowArg;
static void sowHit(void *arg) {
    SowArg *a = arg;
    J *tile = soilAt(state, a->t.x, a->t.y);
    take(state, a->id, 1); setstr(tile, "crop", a->seed); setnum(tile, "age", 0);
    fx_puff(a->t.x * 16 + 8, a->t.y * 16 + 11, 0xa07850ff, 2); sfx("pop"); changed();
}
static void airHit(void *arg) { sfx("hoe"); }
static void build(Tile t);

static void useTool(Tile t) {
    const char *tool = str(state, "tool");
    J *tile = eq(world->id, "farm") ? soilAt(state, t.x, t.y) : NULL;
    // Any tool harvests a ready crop: less tool juggling on a phone.
    if (isReady(tile) && !eq(tool, "build")) {
        HarvestArg a = { t.x, t.y };
        snprintf(a.crop, sizeof a.crop, "%s", str(tile, "crop"));
        act("hand", harvestHit, &a, sizeof a, 0.3);
        return;
    }
    if (eq(tool, "hoe")) {
        if (!eq(world->id, "farm")) { toast("You can only till on the farm."); return; }
        if (!spend(1)) return;
        act("hoe", hoeHit, &t, sizeof t, 0);
        return;
    }
    if (eq(tool, "can")) {
        Targets tg = { .n = 0 };
        if (eq(world->id, "farm")) { Tile area[9]; int n = areaFor("can", t, area); for (int i = 0; i < n; i++) if (soilAt(state, area[i].x, area[i].y)) tg.t[tg.n++] = area[i]; }
        if (!tg.n) { if (isWater(world->map, t.x, t.y)) toast("Watering can full!"); return; }
        bool free_ = hasBonus(state, "water") && num(state, "freeWaterDay") != num(state, "day");
        if (!free_ && !spend(1)) return;
        if (free_) setnum(state, "freeWaterDay", num(state, "day"));
        act("can", canHit, &tg, sizeof tg, 0);
        return;
    }
    if (eq(tool, "seeds")) {
        if (!tile || str(tile, "crop")) return;
        SowArg a = { t };
        snprintf(a.seed, sizeof a.seed, "%s", str(state, "seed"));
        snprintf(a.id, sizeof a.id, "seed_%s", a.seed);
        const char *cname = str(jget(T("CROPS"), a.seed), "name");
        if (!count(state, a.id)) { toast("No %s seeds. Buy some at the town Market.", cname); return; }
        if (!inSeason(state, a.seed)) { toast("%s doesn't grow in %s.", cname, str(season(state), "name")); return; }
        act("seeds", sowHit, &a, sizeof a, 0.3);
        return;
    }
    if (eq(tool, "rod")) { if (isWater(world->map, t.x, t.y)) startFishing(t); else toast("Tap the water to fish."); return; }
    if (eq(tool, "build")) { build(t); return; }
    if (eq(tool, "axe") || eq(tool, "pickaxe")) { act(tool, airHit, NULL, 0, 0); return; }   // swing at air: free
    if (tile && str(tile, "crop")) {
        J *c = jget(T("CROPS"), str(tile, "crop"));
        toast("%s: %d/%d days%s", str(c, "name"), (int)num(tile, "age"), (int)num(c, "days"), has(tile, "wet") ? " · watered" : " · needs water");
    }
}

static void breakProp(Prop *p, bool withDrops);
static void tillTile(Tile a) {
    if (!inBounds(world->map, a.x, a.y) || soilAt(state, a.x, a.y) || machineAt(state, a.x, a.y)) return;
    if (isSolid(world->map, a.x, a.y) || solid(a.x, a.y)) return;
    Prop *p = propAt(world->map, a.x, a.y);
    if (p && p->hasCol) return;
    if (p) breakProp(p, false);   // grass tufts / flowers make way
    char k[16]; snprintf(k, sizeof k, "%d,%d", a.x, a.y);
    J *t = cJSON_CreateObject(); setbool(t, "wet", false);
    cJSON_AddItemToObject(child(state, "soil"), k, t);
}

// Hit a breakable prop or mine node. Props are found again by id when the swing lands (the array may move).
typedef struct { char tool[12]; Tile t; char propId[24]; int node; } SwingArg;
static Prop *findProp(const char *id) { for (int i = 0; i < world->map->nprops; i++) if (eq(world->map->props[i].id, id)) return &world->map->props[i]; return NULL; }
static void breakNode(int idx);
static void collect(J *drops, double x, double y);
static void swingHit(void *arg) {
    SwingArg *a = arg;
    Prop *p = a->propId[0] ? findProp(a->propId) : NULL;
    Node *n = a->node >= 0 && a->node < world->nnodes ? &world->nodes[a->node] : NULL;
    if (!p && !n) return;
    int dmg = (num(jget(state, "tools"), a->tool) >= 2 ? 2 : 1) + (eq(a->tool, "pickaxe") && hasBonus(state, "mine") ? 1 : 0);
    double *hp = p ? &p->hp : &n->hp;
    *hp -= dmg;
    if (p) p->shake = 0.2; else n->shake = 0.2;
    bool wood = eq(a->tool, "axe");
    double cx = p && p->hasCol ? p->col.x + p->col.w / 2 : a->t.x * 16 + 8;
    double cy = p && p->hasCol ? p->col.y : a->t.y * 16 + 8;
    fx_chips(cx, cy, wood ? 0x8a5a32ff : 0x9aa0aaff, 6);
    if (wood) fx_chips(cx, cy, 0x63c74dff, 3);
    fx_shake(1, 0.1);
    sfx(wood ? "chop" : "pick");
    if (*hp > 0) return;
    sfx("break");
    fx_shake(2.5, 0.22);
    fx_puff(cx, cy + 6, wood ? 0xc9a77aff : 0xb4b8c4ff, 7);
    if (p) breakProp(p, true);
    else breakNode(a->node);
    changed();
}
static void swingProp(const char *tool, Tile t, Prop *p) {
    if (has(world->def, "noBreak")) { toast("The town's trees are protected."); return; }
    if (!spend(1)) return;
    faceTile(&player, t);
    SwingArg a = { .t = t, .node = -1 };
    snprintf(a.tool, sizeof a.tool, "%s", tool); snprintf(a.propId, sizeof a.propId, "%s", p->id);
    act(tool, swingHit, &a, sizeof a, 0);
}
static void swingNode(Tile t, Node *n) {
    if (!spend(1)) return;   // nodes have no `def`, so noBreak doesn't apply
    faceTile(&player, t);
    SwingArg a = { "pickaxe", t, "", (int)(n - world->nodes) };
    act("pickaxe", swingHit, &a, sizeof a, 0);
}

static void collect(J *drops, double x, double y) {
    int dy = 0;
    JFOR(d, drops) {
        int n = numv(d);
        give(state, d->string, n);
        for (int i = 0; i < n && i < 3; i++) flyItem(d->string, x, y - 4);
        floatText(x, y - dy, "+%d %s", n, itemName(d->string));
        dy += 10;
    }
}

// Trees leave a stump; everything else disappears for good.
static void breakProp(Prop *p, bool withDrops) {
    double x = p->hasCol ? p->col.x + p->col.w / 2 : (p->x + 0.5) * 16, y = p->foot - 8;
    if (withDrops) {
        J *drops = rollDrops(jget(jget(T("OBJECT_KINDS"), p->kind), "drops"), NULL);
        collect(drops, x, y);
        cJSON_Delete(drops);
        J *st = jget(state, "stats"); setnum(st, "cleared", num(st, "cleared") + 1);
    }
    if (eq(p->kind, "tree") && p->def->stump[0]) {
        setPropType(p, p->def->stump);
        cJSON_AddItemToArray(childarr(child(state, "stumps"), world->id), cJSON_CreateString(p->id));
        rebuildSolid(world->map);
        return;
    }
    char id[24]; snprintf(id, sizeof id, "%s", p->id);
    removeProp(world->map, p);
    if (id[0] == 'f') {
        J *f = jget(jget(state, "forage"), world->id), *items = jget(f, "items");
        int i = 0;
        JFOR(it, items) { if (eq(str(it, "id"), id)) { cJSON_DeleteItemFromArray(items, i); break; } i++; }
    } else cJSON_AddItemToArray(childarr(child(state, "removed"), world->id), cJSON_CreateString(id));
}

typedef struct { char propId[24]; } ForageArg;
static void forageHit(void *arg) {
    Prop *p = findProp(((ForageArg *)arg)->propId);
    if (!p) return;
    J *d = cJSON_CreateObject(); setnum(d, p->def->item, 1);
    collect(d, (p->x + 0.5) * 16, p->foot - 8);
    cJSON_Delete(d);
    J *st = jget(state, "stats"); setnum(st, "foraged", num(st, "foraged") + 1);
    breakProp(p, false);
    sfx("pop"); changed();
}
static void pickForage(Prop *p, Tile t) {
    faceTile(&player, t);
    ForageArg a; snprintf(a.propId, sizeof a.propId, "%s", p->id);
    act("hand", forageHit, &a, sizeof a, 0.3);
}

static void breakNode(int idx) {
    Node n = world->nodes[idx];
    memmove(&world->nodes[idx], &world->nodes[idx + 1], (world->nnodes - idx - 1) * sizeof(Node));
    world->nnodes--;
    J *drops = rollDrops(jget(jget(T("NODES"), n.node), "drops"), NULL);
    collect(drops, n.x * 16 + 8, n.y * 16);
    cJSON_Delete(drops);
    if (n.ladder) { world->hasLadder = true; world->ladder = (Tile){ n.x, n.y }; toast("A ladder down appeared!"); sfx("door"); }
}

static void descendDone(void) {
    world->nnodes = genMine(world->map, num(state, "mineLevel"), num(state, "day"), world->nodes, 40);
    world->hasLadder = false;
    J *sp = jget(jget(T("MAPS"), "mine"), "spawn");
    player.x = num(sp, "x") * 16; player.y = num(sp, "y") * 16;
    warpLock = 0.5; transitioning = false;
    toast("Mine · level %d", (int)num(state, "mineLevel"));
    changed();
}
static void descend(void) {
    if (transitioning) return;
    transitioning = true;
    setnum(state, "mineLevel", num(state, "mineLevel") + 1);
    setnum(state, "mineDeepest", fmax(num(state, "mineDeepest"), num(state, "mineLevel")));
    sfx("door");
    hud_fade(descendDone);
}

// Remove props under a new farm building and its pen so they fit.
static void clearArea(J *r) {
    Rect area = { num(r, "x") * 16, num(r, "y") * 16, num(r, "w") * 16, num(r, "h") * 16 };
    Map *m = world->map;
    for (int i = m->nprops - 1; i >= 0; i--) {   // ponytail: back to front so removals don't skip entries
        Prop *p = &m->props[i];
        if (eq(p->kind, "house")) continue;
        Rect foot = { p->box.x, p->foot - 4, p->box.w, 4 };
        if (rectsOverlap(foot, area)) breakProp(p, false);
    }
    J *soil = jget(state, "soil");
    for (J *k = soil ? soil->child : NULL; k;) {
        J *next = k->next;
        int x, y; sscanf(k->string, "%d,%d", &x, &y);
        if (inRectT(x, y, num(r, "x"), num(r, "y"), num(r, "w"), num(r, "h"))) cJSON_Delete(cJSON_DetachItemViaPointer(soil, k));
        k = next;
    }
    if (jget(r, "pen")) clearArea(jget(r, "pen"));
}

static void drinkWell(void) {
    if (num(state, "wellDay") == num(state, "day")) { toast("You already drank fresh water today."); return; }
    setnum(state, "wellDay", num(state, "day"));
    setnum(state, "energy", fmin(maxEnergy(state), num(state, "energy") + 6));
    floatText(player.x, player.y - 30, "+6 :energy:");
    sfx("water"); changed();
}

static char clockShown[16];
static void passOut(void);
static void tickClock(double dt) {
    setnum(state, "time", num(state, "time") + dt * num(DATA, "MINUTES_PER_SECOND"));
    double m = num(state, "time");
    char label[16]; snprintf(label, sizeof label, "%02d:%02d", (int)floor(m / 60) % 24, (int)floor(fmod(m, 60) / 10) * 10);
    if (strcmp(label, clockShown)) { strcpy(clockShown, label); hud_render(); }
    J *flags = child(state, "flags");
    if (m >= 24 * 60 && num(flags, "lateWarned") != num(state, "day")) { setnum(flags, "lateWarned", num(state, "day")); toast("It's midnight… better get to bed before 2 AM."); }
    if (m >= num(DATA, "DAY_END")) passOut();
}
const char *game_clock(void) { return clockShown; }

static void enterMap(const char *id, bool hasSpawn, double sx, double sy);
static int passFee;
static void passOutDone(void) {
    setnum(state, "money", num(state, "money") - passFee);
    J *log = cJSON_CreateArray();
    char first[160]; snprintf(first, sizeof first, "You passed out at 2 AM. Someone carried you home (and charged %d coins).", passFee);
    cJSON_AddItemToArray(log, cJSON_CreateString(first));
    J *rest = newDay(state);
    while (jlen(rest)) cJSON_AddItemToArray(log, cJSON_DetachItemFromArray(rest, 0));
    cJSON_Delete(rest);
    enterMap("house", false, 0, 0);
    transitioning = false;
    changed(); persist();
    J *goals[16]; int n = checkGoals(state, goals, 16);
    shops_morningReport(log, goals, n);
}
// 2:00 and still awake: you faint, wake up at home, someone charged you for carrying you there.
static void passOut(void) {
    transitioning = true;
    passFee = fmin(1000, floor(num(state, "money") * 0.1));
    hud_fade(passOutDone);
}

// Day/night tint with light sources: lit doors and lamp posts outdoors, a lantern around the player at
// night, and the mine is always dark except around the player.
static bool isNight(void) { double t = num(state, "time"); return t >= 19 * 60 || t < 6 * 60; }
static const char *ambientKind(void) { return eq(world->id, "mine") ? "mine" : world->map->indoor ? NULL : seasonId(state); }

static void drawLights(void) {
    if (world->map->indoor) return;
    bool mine = eq(world->id, "mine");
    int tint[3] = { 52, 44, 66 };
    if (!mine) fx_ambientColor(num(state, "time"), tint);
    if (tint[0] >= 254 && tint[1] >= 254 && tint[2] >= 254) return;
    Light lights[64]; int n = 0;
    double night = 1 - tint[2] / 255.0;
    lights[n++] = (Light){ player.x - cam.x, player.y - 12 - cam.y, mine ? 120 : 24 + night * 44, mine ? 0.85 : 0.5, mine };
    if (!mine) {
        for (int i = 0; i < world->nents && n < 64; i++) {
            Ent *e = &world->ents[i];
            const char *to = str(e->d, "to");
            if (eq(e->type, "building") && str(e->d, "interior")) lights[n++] = (Light){ (e->x + e->w / 2.0) * 16 - cam.x, (e->y + e->h) * 16 - 10 - cam.y, 48, 0, true };
            else if ((eq(e->type, "warp") && has(jget(T("MAPS"), to), "indoor")) || eq(to, "house")) lights[n++] = (Light){ e->x * 16 + 8 - cam.x, e->y * 16 + 4 - cam.y, 48, 0, true };
            else if (eq(e->type, "deco") && eq(str(e->d, "sprite"), "lamppost")) lights[n++] = (Light){ e->x * 16 + 8 - cam.x, (e->y + 1) * 16 - 24 - cam.y, 58, 0, true };
        }
    }
    if (eq(world->id, "farm")) JFOR(m, jget(state, "machines")) if (str(m, "cur") && str(mdef(m), "fuel") && n < 64) lights[n++] = (Light){ num(m, "x") * 16 + 8 - cam.x, num(m, "y") * 16 + 8 - cam.y, 40, 0, true };
    fx_lighting(VW, VH, tint, lights, n);
}

static bool sleepRow(void *a) { void sleepNow(void); sleepNow(); return false; }
static void askSleep(void) {
    menu_begin("House");
    menu_row("Sleep", "Ends the day: watered crops grow, animals eat, machines work", sleepRow, NULL, 0);
    menu_row("Stay up", NULL, NULL, NULL, 0);
    menu_end();
}

static void spawnAnimals(void);
static void spawnForage(void);
static void sleepDone(void) {
    J *log = newDay(state);
    J *goals[16]; int ng = checkGoals(state, goals, 16);
    if (!eq(world->id, "house")) { J *sp = jget(jget(T("MAPS"), "farm"), "spawn"); player.x = num(sp, "x") * 16; player.y = num(sp, "y") * 16; }
    player.dir = DOWN;
    if (eq(world->id, "farm")) { spawnAnimals(); spawnForage(); }
    transitioning = false;
    changed(); persist();
    char b[200];
    if (comfortBonus(state)) { snprintf(b, sizeof b, "Your cozy house gave you +%d max energy (comfort %d).", comfortBonus(state), comfort(state)); cJSON_AddItemToArray(log, cJSON_CreateString(b)); }
    J *goal = nextGoal(state, NULL, 0);
    if (goal) { snprintf(b, sizeof b, "Next town project: %s.", str(goal, "name")); cJSON_AddItemToArray(log, cJSON_CreateString(b)); }
    shops_morningReport(log, goals, ng);
}
void sleepNow(void) { transitioning = true; hud_fade(sleepDone); }

static void farmBuildingMenu(J *b) {
    menu_begin(str(b, "name"));
    menu_text("Feed in stock: %d (each animal eats 1 per night). Buy at the Market or the Carpenter.", count(state, "feed"));
    int n = 0;
    JFOR(a, jget(state, "animals")) {
        J *def = jget(T("ANIMALS"), str(a, "kind"));
        if (!eq(str(def, "home"), b->string)) continue;
        char label[96], sub[160];
        snprintf(label, sizeof label, "%s (%s)", str(a, "name"), str(def, "name"));
        snprintf(sub, sizeof sub, "Happiness %d%%%s%s", (int)num(a, "happy"), has(a, "product") ? " · product ready!" : "", num(a, "petDay") == num(state, "day") ? " · already petted today" : "");
        row_disabled(menu_row(label, sub, NULL, NULL, 0));
        n++;
    }
    if (!n) menu_text("No animals yet. Buy some at the town Carpenter.");
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_end();
}

// Seasonal forage (farm: a couple, forest: plenty), respawned every morning.
static void spawnForage(void) {
    int n = eq(world->id, "farm") ? 2 : num(world->def, "forage");
    Map *map = world->map;
    if (!n || !map->nterrain) return;
    if (cJSON_IsArray(jget(state, "forage"))) cJSON_ReplaceItemInObjectCaseSensitive(state, "forage", cJSON_CreateObject());
    J *f = jget(jget(state, "forage"), world->id);
    if (!f) { f = cJSON_Parse("{\"day\":0,\"items\":[]}"); cJSON_AddItemToObject(jget(state, "forage"), world->id, f); }
    const char *kinds[16]; int nk = 0;
    JFOR(it, T("ITEMS")) if (eq(str(it, "forage"), seasonId(state)) && nk < 16) kinds[nk++] = it->string;
    if (num(f, "day") != num(state, "day")) {
        J *items = cJSON_CreateArray();
        cJSON_ReplaceItemInObjectCaseSensitive(f, "items", items);
        for (int tries = 0; jlen(items) < n && tries < 400; tries++) {
            int x = 2 + floor(frand() * (map->w - 4)), y = 3 + floor(frand() * (map->h - 5));
            char t = map->terrain[y][x];
            if ((t != 'g' && t != 'G') || solid(x, y) || propAt(map, x, y) || soilAt(state, x, y)) continue;
            J *it = cJSON_CreateObject();
            char id[32]; snprintf(id, sizeof id, "f%d_%d", (int)num(state, "day"), tries);
            setstr(it, "id", id); setnum(it, "x", x); setnum(it, "y", y);
            setstr(it, "item", kinds[(int)floor(frand() * nk)]);
            cJSON_AddItemToArray(items, it);
        }
        setnum(f, "day", num(state, "day"));
    }
    for (int i = map->nprops - 1; i >= 0; i--) if (map->props[i].id[0] == 'f') { memmove(&map->props[i], &map->props[i + 1], (map->nprops - i - 1) * sizeof(Prop)); map->nprops--; }
    JFOR(it, jget(f, "items")) {
        Prop p = { .x = num(it, "x"), .y = num(it, "y") };
        snprintf(p.id, sizeof p.id, "%s", str(it, "id"));
        char type[48]; snprintf(type, sizeof type, eq(str(it, "item"), "mushroom") ? "mushroom" : "forage_%s", str(it, "item"));
        setPropType(&p, type);
        addProp(map, p);
    }
    // ponytail: map.js keeps `solid` stale here too (forage props are flat, no collider)
}

// ---------- factory ----------
static J *blueprint(void) {
    if (!eq(str(state, "tool"), "build") || !cJSON_IsNumber(jget(state, "blueprint"))) return NULL;
    return jat(jget(state, "blueprints"), num(state, "blueprint"));
}
typedef struct { Tile t; char type[24]; int dir; } BuildArg;
static void clearFlatUnder(const char *type, int x, int y) {
    int n = num(jget(T("MACHINES"), type), "size");
    for (int dy = 0; dy < n; dy++) for (int dx = 0; dx < n; dx++) { Prop *p = propAt(world->map, x + dx, y + dy); if (p && !p->hasCol) breakProp(p, false); }
}
static void buildHit(void *arg) {
    BuildArg *a = arg;
    clearFlatUnder(a->type, a->t.x, a->t.y);
    char k[48]; snprintf(k, sizeof k, "machine_%s", a->type);
    take(state, k, 1);
    place(state, a->type, a->t.x, a->t.y, player.dir, nowMs());
    sfx("hoe");
    refreshTools();
}
typedef struct { char type[24]; int x, y, dir; char st[8]; } PlanPiece;
// each piece: will it go down here? ok / missing (not in the backpack) / blocked
static int blueprintPlan(J *bp, Tile t, PlanPiece *out) {
    struct { char type[24]; int left; } left[16]; int nl = 0, n = 0;
    JFOR(pc, jget(bp, "pieces")) {
        if (n >= 64) break;
        PlanPiece *p = &out[n++];
        snprintf(p->type, sizeof p->type, "%s", jat(pc, 0)->valuestring);
        p->x = t.x + numv(jat(pc, 1)); p->y = t.y + numv(jat(pc, 2)); p->dir = dirFrom(jat(pc, 3)->valuestring);
        int li = -1;
        for (int i = 0; i < nl; i++) if (eq(left[i].type, p->type)) li = i;
        if (li < 0 && nl < 16) { li = nl++; snprintf(left[li].type, 24, "%s", p->type); char k[48]; snprintf(k, sizeof k, "machine_%s", p->type); left[li].left = count(state, k); }
        strcpy(p->st, !canPlace(state, p->type, p->x, p->y, freeForBuild) ? "blocked" : left[li].left-- > 0 ? "ok" : "missing");
    }
    return n;
}
typedef struct { Tile t; } BpArg;
static void blueprintHit(void *arg) {
    PlanPiece plan[64];
    int n = blueprintPlan(blueprint(), ((BpArg *)arg)->t, plan), ok = 0;
    for (int i = 0; i < n; i++) {
        if (!eq(plan[i].st, "ok")) continue;
        clearFlatUnder(plan[i].type, plan[i].x, plan[i].y);
        char k[48]; snprintf(k, sizeof k, "machine_%s", plan[i].type);
        take(state, k, 1);
        place(state, plan[i].type, plan[i].x, plan[i].y, plan[i].dir, nowMs());
        ok++;
    }
    char txt[256] = "";
    for (int i = 0; i < n; i++) {   // "2 Esteira, 1 Braço": counts per type, first-seen order
        if (!eq(plan[i].st, "missing")) continue;
        bool seen = false;
        for (int j = 0; j < i; j++) seen |= eq(plan[j].st, "missing") && eq(plan[j].type, plan[i].type);
        if (seen) continue;
        int c = 0;
        for (int j = i; j < n; j++) c += eq(plan[j].st, "missing") && eq(plan[j].type, plan[i].type);
        char part[64]; snprintf(part, sizeof part, "%s%d %s", txt[0] ? ", " : "", c, str(jget(T("MACHINES"), plan[i].type), "name"));
        strncat(txt, part, sizeof txt - strlen(txt) - 1);
    }
    toast("Placed %d pieces%s%s", ok, txt[0] ? " · missing: " : "", txt);
    sfx("hoe");
    refreshTools();
}
static void buildBlueprint(J *bp, Tile t) {
    PlanPiece plan[64];
    int n = blueprintPlan(bp, t, plan), ok = 0, missing = 0;
    for (int i = 0; i < n; i++) { ok += eq(plan[i].st, "ok"); missing += eq(plan[i].st, "missing"); }
    if (!ok) { toast(missing ? "Missing pieces: buy them at the Forge." : "Doesn't fit here."); return; }
    BpArg a = { t };
    act("hand", blueprintHit, &a, sizeof a, 0.3);
}
static void build(Tile t) {
    if (!eq(world->id, "farm")) { toast("Machines go on the farm only."); return; }
    if (blueprint()) { buildBlueprint(blueprint(), t); return; }
    const char *type = str(state, "build");
    char k[48]; snprintf(k, sizeof k, "machine_%s", type ? type : "");
    if (!type || !count(state, k)) { toast("Buy machines at the Forge."); return; }
    if (!canPlace(state, type, t.x, t.y, freeForBuild)) { toast("Doesn't fit here."); return; }
    BuildArg a = { t };
    snprintf(a.type, sizeof a.type, "%s", type);
    act("hand", buildHit, &a, sizeof a, 0.3);
}

// "Copiar linha": every machine connected to this one (touching sides), as pieces from the top-left corner.
J *copyLine(J *m0) {
    J *line[64]; int n = 0, todo[64], nt = 0;
    line[n++] = m0; todo[nt++] = 0;
    int maxPieces = num(DATA, "MAX_PIECES");
    while (nt) {
        J *m = line[todo[--nt]];
        int sz = num(mdef(m), "size");
        for (int cy = 0; cy < sz; cy++) for (int cx = 0; cx < sz; cx++) for (int d = 0; d < 4; d++) {
            J *nb = machineAt(state, num(m, "x") + cx + DV[d][0], num(m, "y") + cy + DV[d][1]);
            bool in = false;
            for (int i = 0; i < n; i++) in |= line[i] == nb;
            if (nb && !in && n < maxPieces && n < 64) { line[n] = nb; todo[nt++] = n++; }
        }
    }
    int x0 = 1 << 30, y0 = 1 << 30;
    for (int i = 0; i < n; i++) { x0 = fmin(x0, num(line[i], "x")); y0 = fmin(y0, num(line[i], "y")); }
    for (int i = 1; i < n; i++) for (int j = i; j > 0; j--) {   // stable sort by y, then x
        J *a = line[j - 1], *b = line[j];
        if (num(a, "y") > num(b, "y") || (num(a, "y") == num(b, "y") && num(a, "x") > num(b, "x"))) { line[j - 1] = b; line[j] = a; } else break;
    }
    J *bp = cJSON_CreateObject(), *pieces = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        J *p = cJSON_CreateArray();
        cJSON_AddItemToArray(p, cJSON_CreateString(str(line[i], "type")));
        cJSON_AddItemToArray(p, cJSON_CreateNumber(num(line[i], "x") - x0));
        cJSON_AddItemToArray(p, cJSON_CreateNumber(num(line[i], "y") - y0));
        cJSON_AddItemToArray(p, cJSON_CreateString(str(line[i], "dir")));
        cJSON_AddItemToArray(pieces, p);
    }
    static const char *MAIN[] = { "processor", "miner", "furnace", "splitter", "arm", "belt" };
    const char *mainType = str(line[0], "type");
    for (int k = 0; k < 6; k++) { bool any = false; for (int i = 0; i < n; i++) any |= eq(str(line[i], "type"), MAIN[k]); if (any) { mainType = MAIN[k]; break; } }
    char name[96], lower[64]; snprintf(lower, sizeof lower, "%s", str(jget(T("MACHINES"), mainType), "name"));
    for (char *c = lower; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;   // ponytail: ASCII toLowerCase (names start ASCII)
    snprintf(name, sizeof name, "%s line (%d pieces)", lower, n);
    setstr(bp, "name", name);
    cJSON_AddItemToObject(bp, "pieces", pieces);
    return bp;
}

// ---------- fishing ----------
static void startFishing(Tile t) {
    if (!num(jget(state, "tools"), "rod")) { toast("Buy a rod at the Fishing Shop."); return; }
    if (!spend(2)) return;
    faceTile(&player, t);
    fishing = (Fishing){ .on = true, .phase = "cast", .tile = t, .wait = 1 + frand() * 3 };
    ActOpts o = actDefaults();
    o.row = 44; o.frames = 9; o.dur = 0.7; o.overlay = 1; o.ovRow = 0; o.hold = true; o.hitAt = 2;
    startAction(&player, o, NULL);
    sfx("water");
}
// Where the rod sprite's line touches the water (last cast frame), relative to the player's feet.
static void lineEnd(double *x, double *y) {
    static const int O[4][2] = { [DOWN] = { 1, 16 }, [RIGHT] = { 23, 16 }, [LEFT] = { -23, 16 }, [UP] = { -1, -34 } };
    *x = player.x + O[player.dir][0]; *y = player.y + O[player.dir][1];
}
static void endFishing(void) { fishing.on = false; player.acting = false; }
static void updateFishing(double dt) {
    Fishing *f = &fishing;
    f->t += dt;
    if (eq(f->phase, "cast") && f->t > 0.7) { strcpy(f->phase, "wait"); f->t = 0; }
    else if (eq(f->phase, "wait") && f->t > f->wait) {
        strcpy(f->phase, "bite"); f->t = 0;
        f->result = rollFish(state, NULL, eq(world->id, "forest"));
        double x, y; lineEnd(&x, &y);
        fx_splash(x, y, 10);
        sfx("bite");
    } else if (eq(f->phase, "bite") && f->t > f->result.window) { toast("It got away…"); sfx("miss"); endFishing(); }
    else if (eq(f->phase, "reel") && f->t > 0.65) {
        give(state, f->result.id, 1);
        J *st = jget(state, "stats"); setnum(st, "fished", num(st, "fished") + 1);
        const char *name = itemName(f->result.id);
        floatText(player.x, player.y - 30, "%s!", name);
        toast("You caught %s!%s", name, eq(f->result.rarity, "legendary") ? " LEGENDARY!" : "");
        sfx("catch"); endFishing(); changed();
    }
}
static void fishTap(void) {
    if (eq(fishing.phase, "bite")) {
        strcpy(fishing.phase, "reel"); fishing.t = 0;
        ActOpts o = actDefaults();
        o.row = 47; o.frames = 8; o.dur = 0.65; o.overlay = 1; o.ovRow = 3; o.hold = true; o.hitAt = 2;
        startAction(&player, o, NULL);
    } else if (!eq(fishing.phase, "reel")) { toast("Reeled too early!"); endFishing(); }
}

// ---------- maps ----------
static void freeWorld(void) {
    if (!world) return;
    for (int i = 0; i < world->nents; i++) if (world->ents[i].owned) cJSON_Delete(world->ents[i].d);
    freeMap(world->map);
}
static Ent *addEnt(J *d, bool owned) {
    Ent *e = &world->ents[world->nents++];
    *e = (Ent){ .d = d, .owned = owned, .type = str(d, "type"), .x = num(d, "x"), .y = num(d, "y"), .w = num(d, "w"), .h = num(d, "h") };
    J *wd = jget(d, "wander");
    if (cJSON_IsObject(wd)) { e->hasWander = true; e->wander = (Rect){ num(wd, "x"), num(wd, "y"), num(wd, "w"), num(wd, "h") }; }
    return e;
}
static void setupFestival(void);
static void returnMisplacedMachines(Map *map);
static void setDecor(bool on);

static void enterMap(const char *id, bool hasSpawn, double sx, double sy) {
    J *def = jget(T("MAPS"), id);
    freeWorld();
    world = &W_;
    *world = (World){ .def = def };
    snprintf(world->id, sizeof world->id, "%s", id);
    J *interior = jget(T("INTERIORS"), id);
    if (eq(id, "house")) {
        world->map = buildHouseMap(state);
        J *w = cJSON_Parse("{\"type\":\"warp\",\"w\":1,\"h\":1,\"to\":\"farm\",\"spawn\":{\"x\":19.5,\"y\":11.8}}");
        setnum(w, "x", world->map->door.x); setnum(w, "y", world->map->door.y);
        addEnt(w, true);
        if (!hasSpawn) { sx = world->map->door.x + 0.5; sy = world->map->door.y - 0.1; }
    } else if (interior) {
        // town building: its room, the villager who works there (unless they're at the festival), door back out
        J *b = NULL, *npc = NULL;
        JFOR(e, jget(T("ENTITIES"), "town")) { if (eq(str(e, "interior"), id)) b = e; if (eq(str(e, "type"), "npc") && eq(str(e, "home"), id)) npc = e; }
        world->map = buildRoom(num(interior, "w"), num(interior, "d"), str(interior, "wall"), str(interior, "floor"), jget(interior, "placed"), false);
        J *w = cJSON_Parse("{\"type\":\"warp\",\"w\":1,\"h\":1,\"to\":\"town\"}"), *sp = cJSON_CreateObject();
        setnum(w, "x", world->map->door.x); setnum(w, "y", world->map->door.y);
        J *door = jget(b, "door");
        setnum(sp, "x", door ? num(door, "x") + 0.5 : num(b, "x") + num(b, "w") / 2);
        setnum(sp, "y", num(b, "y") + num(b, "h") + 0.7);
        cJSON_AddItemToObject(w, "spawn", sp);
        addEnt(w, true);
        J *at = jget(interior, "npc");
        if (npc && cJSON_IsObject(at) && !isFestival(state)) {
            J *n = cJSON_Duplicate(npc, true);
            setnum(n, "x", num(at, "x")); setnum(n, "y", num(at, "y"));
            delkey(n, "wander");
            addEnt(n, true);
        }
        if (!hasSpawn) { sx = world->map->door.x + 0.5; sy = world->map->door.y - 0.1; }
    } else {
        J *removed = jget(jget(state, "removed"), id), *stumps = jget(jget(state, "stumps"), id);
        world->map = loadMap(str(def, "url"), removed, stumps, seasonId(state));
        JFOR(e, jget(T("ENTITIES"), id)) {
            if (str(e, "needs") && !has(jget(state, "projects"), str(e, "needs"))) continue;
            if (str(e, "unless") && has(jget(state, "projects"), str(e, "unless"))) continue;
            if (str(e, "home") && !isFestival(state)) continue;
            addEnt(cJSON_Duplicate(e, true), true);
        }
        if (!hasSpawn) { sx = num(jget(def, "spawn"), "x"); sy = num(jget(def, "spawn"), "y"); }
        if (eq(id, "town") && isFestival(state)) setupFestival();
    }
    if (eq(id, "mine") && has(jget(state, "projects"), "elevator") && num(state, "mineLevel") < num(state, "mineDeepest")) {
        setnum(state, "mineLevel", num(state, "mineDeepest"));
        toast("Elevator: level %d", (int)num(state, "mineLevel"));
    }
    if (eq(id, "mine")) world->nnodes = genMine(world->map, num(state, "mineLevel"), num(state, "day"), world->nodes, 40);
    setDecor(false);
    for (int i = 0; i < world->nents; i++) {
        Ent *e = &world->ents[i];
        if (eq(e->type, "npc")) { e->px = (e->x + 0.5) * 16; e->py = (e->y + 1) * 16 - 3; e->dir = DOWN; e->t = frand() * 5; e->moving = false; }
    }
    addBuildingColliders();
    if (eq(id, "farm")) returnMisplacedMachines(world->map);
    setstr(state, "map", id);
    if (eq(id, "town")) setbool(child(state, "flags"), "town", true);
    player.x = sx * 16; player.y = sy * 16;
    J *dsp = jget(def, "spawn");
    if (cJSON_IsObject(dsp) && blocked(feetBox(player.x, player.y))) { player.x = num(dsp, "x") * 16; player.y = num(dsp, "y") * 16; }
    mascot.x = player.x; mascot.y = player.y;
    fishing.on = false; player.acting = false;
    if (eq(id, "farm")) spawnAnimals();
    spawnForage();
    warpLock = 0.5;
    changed();
}

static void misplacedToast(void *arg) { toast("%d machine(s) were in an invalid spot and went back to your Backpack.", *(int *)arg); }
// A machine standing on water, a tree, a building or outside the map goes back to the backpack.
static void returnMisplacedMachines(Map *map) {
    Rect cs[1024]; int nc = colliders(map, cs, 1024), bad = 0;
    J *ms = jget(state, "machines");
    for (J *m = ms ? ms->child : NULL; m;) {
        J *next = m->next;
        int sz = num(mdef(m), "size");
        bool wrong = false;
        for (int dy = 0; dy < sz; dy++) for (int dx = 0; dx < sz; dx++) {
            int x = num(m, "x") + dx, y = num(m, "y") + dy;
            Rect tile = { x * 16, y * 16, 16, 16 };
            if (isSolid(map, x, y)) wrong = true;
            for (int i = 0; i < nc && !wrong; i++) wrong = rectsOverlap(cs[i], tile);
        }
        if (wrong) { pickUp(state, m); bad++; }
        m = next;
    }
    if (bad) popup(misplacedToast, &bad, sizeof bad);
}

// Festival day: everyone gathers in the square, decorations go up, the season's capsule machine arrives.
static void setupFestival(void) {
    J *fest = jget(T("FESTIVALS"), seasonId(state));
    setbool(child(state, "flags"), "festival", true);
    for (int i = 0; i < world->nents; i++) {
        Ent *e = &world->ents[i];
        if (!eq(e->type, "npc")) continue;
        e->hasWander = true; e->wander = (Rect){ 16, 12, 8, 6 };
        e->x = 16 + floor(frand() * 8); e->y = 13 + floor(frand() * 4);
    }
    J *g = cJSON_Parse("{\"type\":\"gacha\",\"x\":23,\"y\":12}");
    setstr(g, "set", seasonId(state));
    addEnt(g, true);
    for (int i = 0; i < 2; i++) {
        J *b = cJSON_Parse("{\"type\":\"deco\",\"img\":\"assets/Cute_Fantasy/Outdoor decoration/Outdoor_Decor.png\",\"sx\":64,\"sy\":240,\"sw\":32,\"sh\":32,\"w\":2}");
        setnum(b, "x", i ? 22 : 15); setnum(b, "y", i ? 18 : 17);
        addEnt(b, true);
    }
    toast(":crown: %s! %s", str(fest, "name"), str(fest, "desc"));
}

typedef struct { char id[16]; bool hasSpawn; double x, y; } GoArg;
static GoArg goArg;
static void goDone(void) { enterMap(goArg.id, goArg.hasSpawn, goArg.x, goArg.y); transitioning = false; persist(); }
static void goTo(const char *id, bool hasSpawn, double sx, double sy) {
    if (transitioning) return;
    transitioning = true;
    sfx("door");
    goArg = (GoArg){ .hasSpawn = hasSpawn, .x = sx, .y = sy };
    snprintf(goArg.id, sizeof goArg.id, "%s", id);
    hud_fade(goDone);
}
void game_goTo(const char *id) { goTo(id, false, 0, 0); }
void game_goHere(void) { goTo(world->id, true, player.x / 16, player.y / 16); }
const char *game_worldId(void) { return world ? world->id : ""; }

static void spawnAnimals(void) {
    JFOR(a, jget(state, "animals")) {
        J *pen = jget(jget(T("BUILDINGS"), str(jget(T("ANIMALS"), str(a, "kind")), "home")), "pen");
        setnum(a, "px", (num(pen, "x") + 0.5 + frand() * (num(pen, "w") - 1)) * 16);
        setnum(a, "py", (num(pen, "y") + 0.8 + frand() * (num(pen, "h") - 1)) * 16);
        setnum(a, "tx", num(a, "px")); setnum(a, "ty", num(a, "py"));
        setstr(a, "dir", "left"); setnum(a, "wait", frand() * 3); setnum(a, "t", frand() * 4);
    }
}

// Building / project callbacks for the menus (main.js `g`).
void game_onBuilt(const char *id) {
    if (eq(world->id, "farm")) { clearArea(jget(T("BUILDINGS"), id)); addBuildingColliders(); }
    spawnAnimals();
}
void game_onProject(void) { if (eq(world->id, "town")) game_goHere(); }
void game_onHouse(void) { if (eq(world->id, "house")) goTo("house", false, 0, 0); }

// ---------- house decorating ----------
static void setDecor(bool on) {
    // a bed that was picked up and not put down goes back where it was
    if (!on && decor.carrying && has(jget(T("FURNITURE"), decor.carried), "bed")) {
        cJSON_AddItemToArray(jget(jget(state, "house"), "placed"), decor.saved);
        J *d = child(state, "decor"); setnum(d, decor.carried, num(d, decor.carried) - 1);
        decor.carrying = false;
        if (world && eq(world->id, "house")) { goTo("house", true, player.x / 16, player.y / 16); return; }
    }
    decor.on = on; decor.item[0] = 0; decor.carrying = false;
    hud_decor(on);
}
bool game_decorOn(void) { return decor.on; }
void game_setDecor(bool on) { setDecor(on); }
const char *game_decorItem(void) { return decor.item; }
void game_pickDecor(const char *item) { if (eq(decor.item, item)) decor.item[0] = 0; else snprintf(decor.item, sizeof decor.item, "%s", item); sfx("click"); hud_decor(true); }

static void decorTap(Tile t) {
    Map *map = world->map;
    if (decor.item[0]) {
        char item[40]; snprintf(item, sizeof item, "%s", decor.item);
        long uid = (long)fmod(nowMs(), 1e9);
        Rect fb = feetBox(player.x, player.y);
        Prop p;
        if (!tryPlace(map, item, t.x, t.y, uid, &fb, 1, &p)) { sfx("miss"); toast("Doesn't fit here."); return; }
        addProp(map, p);
        rebuildSolid(map);
        J *pl = cJSON_CreateObject();
        setnum(pl, "uid", uid); setstr(pl, "item", item); setnum(pl, "x", t.x); setnum(pl, "y", t.y);
        cJSON_AddItemToArray(jget(jget(state, "house"), "placed"), pl);
        bool critter = starts(item, "critter:");
        J *d = child(state, "decor");
        if (!critter) setnum(d, item, num(d, item) - 1);
        if (decor.carrying && eq(decor.carried, item)) { cJSON_Delete(decor.saved); decor.carrying = false; }
        if (!critter && num(d, item) > 0) snprintf(decor.item, sizeof decor.item, "%s", item); else decor.item[0] = 0;
        sfx("hoe");
    } else {
        // pick up what's on this tile (prefer solid furniture over rugs)
        Rect tile = { t.x * 16, t.y * 16, 16, 16 };
        Prop *p = NULL;
        for (int i = 0; i < map->nprops && !p; i++) if (map->props[i].movable && map->props[i].hasCol && rectsOverlap(map->props[i].col, tile)) p = &map->props[i];
        for (int i = 0; i < map->nprops && !p; i++) if (map->props[i].movable && rectsOverlap(map->props[i].box, tile)) p = &map->props[i];
        if (!p) return;
        J *placed = jget(jget(state, "house"), "placed"), *saved = NULL;
        for (J *x = placed->child; x; x = x->next) if ((long)num(x, "uid") == p->uid) { saved = cJSON_DetachItemViaPointer(placed, x); break; }
        char item[40]; snprintf(item, sizeof item, "%s", p->item);
        removeProp(map, p);
        if (!starts(item, "critter:")) { J *d = child(state, "decor"); setnum(d, item, num(d, item) + 1); }
        snprintf(decor.item, sizeof decor.item, "%s", item);
        if (decor.carrying) cJSON_Delete(decor.saved);
        decor.carrying = saved != NULL; decor.saved = saved;
        snprintf(decor.carried, sizeof decor.carried, "%s", item);
        sfx("pop");
    }
    changed(); hud_decor(true);
}

// ---------- update ----------
static void updateNpcs(double dt) {
    for (int i = 0; i < world->nents; i++) {
        Ent *e = &world->ents[i];
        if (!eq(e->type, "npc")) continue;
        e->t += dt;
        if (!e->hasWander) continue;
        if (!e->hasTarget) {
            e->moving = false;
            e->wait -= dt;
            if (e->wait > 0) continue;
            e->hasTarget = true;
            e->tx = (e->wander.x + frand() * e->wander.w) * 16;
            e->ty = (e->wander.y + 0.6 + frand() * (e->wander.h - 1)) * 16;
        }
        double dx = e->tx - e->px, dy = e->ty - e->py, d = hypot(dx, dy);
        if (d < 2) { e->hasTarget = false; e->wait = 1 + frand() * 3; continue; }
        double nx = e->px + dx / d * 18 * dt, ny = e->py + dy / d * 18 * dt;
        Rect box = feetBox(nx, ny);
        if (blocked(box) || rectsOverlap(box, feetBox(player.x, player.y))) { e->hasTarget = false; e->wait = 1; continue; }
        e->px = nx; e->py = ny; e->moving = true;
        e->dir = fabs(dx) > fabs(dy) ? (dx > 0 ? RIGHT : LEFT) : (dy > 0 ? DOWN : UP);
    }
}

static void updateAnimals(double dt) {
    JFOR(a, jget(state, "animals")) {
        if (!cJSON_IsNumber(jget(a, "px"))) continue;
        setnum(a, "t", num(a, "t") + dt);
        if (num(a, "wait") > 0) { setnum(a, "wait", num(a, "wait") - dt); continue; }
        double dx = num(a, "tx") - num(a, "px"), dy = num(a, "ty") - num(a, "py"), d = hypot(dx, dy);
        if (d < 1) {
            J *pen = jget(jget(T("BUILDINGS"), str(jget(T("ANIMALS"), str(a, "kind")), "home")), "pen");
            setnum(a, "tx", (num(pen, "x") + 0.5 + frand() * (num(pen, "w") - 1)) * 16);
            setnum(a, "ty", (num(pen, "y") + 0.8 + frand() * (num(pen, "h") - 1)) * 16);
            setnum(a, "wait", 1 + frand() * 4);
            continue;
        }
        setnum(a, "px", num(a, "px") + dx / d * 10 * dt); setnum(a, "py", num(a, "py") + dy / d * 10 * dt);
        if (fabs(dx) > 0.5) setstr(a, "dir", dx > 0 ? "right" : "left");
    }
}

static void updateMascot(double dt) {
    if (!mascotOf(state)) return;
    double tx = player.x + (player.dir == LEFT ? 14 : -14), ty = player.y + 2;
    double dx = tx - mascot.x, dy = ty - mascot.y, d = hypot(dx, dy);
    if (d > 60) { mascot.x = tx; mascot.y = ty; return; }
    if (d > 6) { mascot.x += dx * fmin(1, dt * 4); mascot.y += dy * fmin(1, dt * 4); }
    if (fabs(dx) > 1) mascot.dir = dx > 0 ? RIGHT : LEFT;
}

static char terrainAt(double px, double py) {
    int y = floor((py - 3) / 16), x = floor(px / 16);
    Map *m = world->map;
    return y >= 0 && y < m->nterrain && x >= 0 && x < (int)strlen(m->terrain[y]) ? m->terrain[y][x] : 0;
}
static void footsteps(double x0, double y0) {
    stepDist += hypot(player.x - x0, player.y - y0);
    if (stepDist < 18) return;
    stepDist = 0;
    char t = terrainAt(player.x, player.y);
    sfxStep(t == 'd' ? "dirt" : t == 'c' ? "cave" : "grass");
    if (t == 'd' || t == 'c') fx_puff(player.x, player.y - 1, t == 'c' ? 0x6b5a5aff : 0xd9b98aff, 1);
}

static void checkWarps(double dt) {
    if ((warpLock -= dt) > 0 || transitioning) return;
    Tile t = playerTile(&player);
    for (int i = 0; i < world->nents; i++) {
        Ent *e = &world->ents[i];
        if (eq(e->type, "warp") && !has(e->d, "interact") && inRectT(t.x, t.y, e->x, e->y, e->w, e->h)) {
            J *sp = jget(e->d, "spawn");
            goTo(str(e->d, "to"), sp != NULL, num(sp, "x"), num(sp, "y"));
            return;
        }
    }
    if (world->hasLadder && t.x == world->ladder.x && t.y == world->ladder.y) descend();
}

// ---------- tutorial / quests (the objective chip and the target arrow) ----------
static struct { bool on; Tile t; } tut;
static void tutorialTarget(void);   // see tutorial hookup below

// ---------- drawing ----------
static void drawEntity(Ent *e) {
    J *d = e->d;
    if (eq(e->type, "building")) {
        Img *im = I(str(d, "img"));
        BRect r = buildingRect(d, im);
        cv_image(im, r.sx, 0, r.sw, r.sh, r.dx, r.dy, r.sw, r.sh);
        int i = 0;
        JFOR(id, jget(d, "goods")) drawItem(id->valuestring, r.dx + 2 + i++ * 15, r.dy + 21);   // wares on the counter
    } else if (eq(e->type, "deco")) {
        // small set piece standing on its footprint row, centered
        Img *im = str(d, "sprite") ? sprite(str(d, "sprite")) : I(str(d, "img"));
        if (!im) return;
        int sw = num(d, "sw") ? num(d, "sw") : im->w, sh = num(d, "sh") ? num(d, "sh") : im->h;
        cv_image(im, num(d, "sx"), num(d, "sy"), sw, sh, jround(e->x * 16 + (e->w * 16 - sw) / 2.0), (e->y + 1) * 16 - sh, sw, sh);
    } else if (eq(e->type, "sell")) cv_draw(sprite("bin"), e->x * 16, e->y * 16);
    else if (eq(e->type, "gacha")) {
        cv_fill(e->x * 16 + 2, e->y * 16 + 14, 12, 3, css("rgba(42,36,64,0.25)"));
        cv_draw(sprite("gacha"), e->x * 16, e->y * 16 + 16 - 24);
    } else if (eq(e->type, "board")) cv_draw(sprite("board"), e->x * 16, (e->y + 1) * 16 - 23);
    else if (eq(e->type, "ladder")) cv_draw(sprite("ladder"), e->x * 16 + 1, e->y * 16 + 1);
    else if (eq(e->type, "npc")) {
        Img *im = I(str(d, "sprite"));
        if (im) { fx_shadow(e->px, e->py, 12); drawNpc(im, e->px, e->py, e->dir, e->moving, e->t); }
    }
}
// Farm buildings: drawn like town buildings.
static void drawBuilt(J *b) { Ent e = { .d = b, .type = "building" }; drawEntity(&e); }

void drawItem(const char *id, double x, double y) {
    J *ic = jget(ITEM(id), "icon");
    if (!ic) return;
    if (str(ic, "pixel")) { cv_draw(sprite(str(ic, "pixel")), x, y); return; }
    cv_image(I(str(ic, "src")), num(ic, "sx"), num(ic, "sy"), 16, 16, x, y, 16, 16);
}

#define TF "assets/Tiny_Factory/Automation Components/"
static const double ANGLE[4] = { [RIGHT] = 0, [DOWN] = M_PI / 2, [LEFT] = M_PI, [UP] = -M_PI / 2 };
static const int ARM_FRAME[4] = { [UP] = 2, [RIGHT] = 4, [DOWN] = 6, [LEFT] = 0 };
static const int GRIP[8][2] = { { 8, 18 }, { 11, 8 }, { 24, 5 }, { 37, 8 }, { 40, 18 }, { 37, 31 }, { 24, 39 }, { 11, 31 } };   // claw center per frame
static const int BACK_DIR[4] = { [UP] = DOWN, [DOWN] = UP, [LEFT] = RIGHT, [RIGHT] = LEFT };
static struct { double id, a, t; } armTurn[128]; static int narm;
void armForget(double id) { for (int i = 0; i < narm; i++) if (armTurn[i].id == id) armTurn[i] = armTurn[--narm]; }

// Sprinkler: the nozzle turns and, in daylight, two jets sweep around and rain on the 8 tiles.
static bool spraying(void) { double t = num(state, "time"); return eq(world->id, "farm") && t >= 6 * 60 && t < 18 * 60; }
static void drawSprinkler(J *m) {
    double x = num(m, "x") * 16, y = num(m, "y") * 16;
    cv_draw(sprite("sprinkler"), x, y);
    cv_fill(x + 8 + jround(cos(gtime * 4) * 2), y + 5, 1, 1, 0xffffffff);
}
static void drawSprays(void) {
    if (!spraying()) return;
    JFOR(m, jget(state, "machines")) {
        if (!eq(str(m, "type"), "sprinkler")) continue;
        double cx = num(m, "x") * 16 + 8, cy = num(m, "y") * 16 + 5;
        for (int j = 0; j < 2; j++) {
            double a = gtime * 3 + j * M_PI + num(m, "id");
            for (int i = 1; i <= 6; i++) {
                double r = i * 3.4, b = a - i * 0.09;
                double px = cx + cos(b) * r, py = cy + sin(b) * r * 0.6 - sin(i / 6.0 * M_PI) * 6 + i * 0.8;
                cv_fill(jround(px), jround(py), i > 4 ? 1 : 2, 1, i > 4 ? 0xffffffff : 0x9fe2ffff);
            }
            if (frand() < 0.08) fx_splash(cx + cos(a - 0.5) * 20, cy + 8 + sin(a - 0.5) * 12, 2);
        }
    }
}

// Items on belts glide from the tile they came from, over one simulation step.
static void itemPos(J *m, double *x, double *y) {
    J *from = jget(m, "from");
    if (!cJSON_IsArray(from) || !cJSON_IsNumber(jget(m, "at"))) { *x = num(m, "x") * 16; *y = num(m, "y") * 16 - 3; return; }
    double k = fmin(1, fmax(0, currentTick() - num(m, "at") + simAcc / STEP));
    double fx = numv(jat(from, 0)), fy = numv(jat(from, 1));
    *x = jround((fx + (num(m, "x") - fx) * k) * 16);
    *y = jround((fy + (num(m, "y") - fy) * k) * 16) - 3;
}

static void drawBelt(J *m) {
    int f = (int)floor(gtime * 16) % 8;   // texture scrolls at the items' speed (1 tile per step)
    cv_save();
    cv_translate(num(m, "x") * 16 + 8, num(m, "y") * 16 + 8);
    cv_rotate(ANGLE[dirFrom(str(m, "dir"))]);
    cv_image(I(TF "Conveyor_Belt.png"), 16 + f * 96, 48, 16, 16, -8, -8, 16, 16);
    cv_restore();
}
static void drawFlatPiece(J *m) {
    if (eq(str(m, "type"), "belt")) { drawBelt(m); return; }
    double cx = num(m, "x") * 16 + 8, cy = num(m, "y") * 16 + 8;
    int dir = dirFrom(str(m, "dir"));
    cv_save();
    cv_translate(cx, cy);
    if (eq(str(m, "type"), "splitter")) { cv_rotate(ANGLE[dir] + M_PI / 2); cv_draw(I(TF "Splitter_A.png"), -24, -24); }
    else { cv_rotate(ANGLE[dir] + M_PI); cv_image(I(TF "Underground_Belt_A.png"), 16, 8, 16, 24, -8, -12, 16, 24); }   // underground belt, rotated
    cv_restore();
}
// belt items go after all belts, so one sliding in from the next tile isn't covered by that belt
static void drawBeltItems(J **list, int n) {
    for (int i = 0; i < n; i++) if (isFlat(list[i]) && str(list[i], "item")) { double x, y; itemPos(list[i], &x, &y); drawItem(str(list[i], "item"), x, y); }
}

// arms reach over their neighbours, so they draw after the machines they feed and take from
static double mbottom(J *m) { return (num(m, "y") + num(mdef(m), "size")) * 16 - 1; }
static double machineY(J *st, J *m) {
    double y = mbottom(m);
    if (eq(str(m, "type"), "arm")) {
        int dir = dirFrom(str(m, "dir")), ds[2] = { dir, BACK_DIR[dir] };
        for (int i = 0; i < 2; i++) { J *n = machineAt(st, num(m, "x") + DV[ds[i]][0], num(m, "y") + DV[ds[i]][1]); if (n && !isFlat(n)) y = fmax(y, mbottom(n)) + 0.5; }
    }
    return y;
}

void drawMachine(J *m) {
    J *def = mdef(m);
    const char *type = str(m, "type");
    double mx = num(m, "x"), my = num(m, "y");
    if (eq(type, "sprinkler")) { fx_shadow(mx * 16 + 8, my * 16 + 14, 12); drawSprinkler(m); return; }
    if (eq(type, "arm")) {
        // 8 frames turn the arm all the way round: it swings toward its source when empty, toward its target when
        // carrying, passing through the in-between frames. The item rides in the claw.
        int dir = dirFrom(str(m, "dir")), target = ARM_FRAME[str(m, "item") ? dir : BACK_DIR[dir]];
        int si = -1;
        for (int i = 0; i < narm; i++) if (armTurn[i].id == num(m, "id")) si = i;
        if (si < 0 && narm < 128) { si = narm++; armTurn[si].id = num(m, "id"); armTurn[si].a = target; armTurn[si].t = gtime; }
        double *a = &armTurn[si].a;
        int d = ((target - (int)jround(*a) + 12) % 8) - 4;
        if (d == -4 && jround(*a) < target) d = 4;   // half turns swing over the top (frames 1-3), like a crane
        *a = fmod(*a + (d > 0 ? 1 : d < 0 ? -1 : 0) * fmin(abs(d), (gtime - armTurn[si].t) * 22) + 8, 8);
        armTurn[si].t = gtime;
        int f = (int)jround(*a) % 8, ox = mx * 16 - 16, oy = my * 16 - 18;
        if (str(m, "item")) drawItem(str(m, "item"), ox + GRIP[f][0] - 8, oy + GRIP[f][1] - 10);
        cv_image(I(str(def, "img")), f * 48, 0, 48, 48, ox, oy, 48, 48);
        return;
    }
    if (eq(type, "pole")) { cv_draw(I(str(def, "img")), mx * 16 - 8, (my + 1) * 16 - 48); return; }
    int n = num(def, "size");
    if (n == 1) {
        char k[48]; snprintf(k, sizeof k, "machine_%s", type);
        drawItem(k, mx * 16, my * 16);
        J *out = jget(m, "out");
        if (eq(type, "chest") && jlen(out)) drawItem(jat(out, jlen(out) - 1)->valuestring, mx * 16, my * 16 - 14);
        return;
    }
    bool running = isRunning(m) && str(def, "run");
    Img *im = I(running ? str(def, "run") : str(def, "img"));
    if (!im) return;
    int runFrames = num(def, "runFrames");
    double fw = running ? im->w / (double)runFrames : im->w, fh = im->h;
    int f = running ? (int)floor(gtime * 6) % runFrames : 0;
    cv_image(im, f * fw, 0, fw, fh, mx * 16 + (n * 16 - fw) / 2, (my + n) * 16 - fh, fw, fh);
    J *out = jget(m, "out");
    if (jlen(out)) drawItem(jat(out, 0)->valuestring, mx * 16 + (n * 8 - 8), my * 16 - 24 + jround(sin(gtime * 3)));
    if (has(m, "unpowered") && (int)floor(gtime * 2) % 2)
        cv_image(I("assets/Cute_Fantasy_UI/UI/UI_Icons.png"), 11 * 16, 0, 16, 16, mx * 16 + n * 8 - 8, (my + n) * 16 - fh - 14, 16, 16);
}

static void drawAnimal(J *a) {
    J *def = jget(T("ANIMALS"), str(a, "kind"));
    Img *im = I(str(def, "sprite"));
    int f = (int)floor(num(a, "t") * 3) % 2;
    double px = jround(num(a, "px")), py = jround(num(a, "py"));
    cv_save();
    if (eq(str(a, "dir"), "right")) { cv_translate(px * 2, 0); cv_scale(-1, 1); }
    cv_image(im, f * 32, 0, 32, 32, px - 16, py - 28, 32, 32);
    cv_restore();
    if (has(a, "product")) drawItem(str(def, "product"), px - 8, py - 42 + jround(sin(gtime * 3) * 1.5));
}

static void drawMascot(void) {
    J *c = mascotOf(state);
    if (!c) return;
    Img *im = I(str(c, "sprite"));
    int size = num(c, "size") ? num(c, "size") : 32, frames = num(c, "frames") ? num(c, "frames") : 2;
    int f = (int)floor(gtime * 4) % frames;
    cv_save();
    if (mascot.dir == RIGHT && size == 32) { cv_translate(jround(mascot.x) * 2, 0); cv_scale(-1, 1); }
    cv_image(im, f * size, 0, size, size, jround(mascot.x) - size / 2.0, jround(mascot.y) - size + 4, size, size);
    cv_restore();
}

// Water: tiled animated texture + mirrored, rippling reflections of things standing at the shore, all
// clipped to the map's water mask.
// ponytail: on the GPU the reflections draw straight onto the water (no offscreen buffer): the ground,
// drawn next, covers whatever spills past the pond (it is opaque everywhere except the water).
static bool nearWater(double fx, double fy) {
    Map *m = world->map;
    int x = floor(fx / 16), y0 = floor(fy / 16);
    for (int d = 0; d < 3; d++) { int y = y0 + d; if (y >= 0 && y < m->nterrain && x >= 0 && x < (int)strlen(m->terrain[y]) && m->terrain[y][x] == 'w') return true; }
    return false;
}
static void mirror(double fy) { cv_save(); cv_translate(0, fy * 2); cv_scale(1, -1); }
static void drawWater(void) {
    cv_fill_pattern(WATER[(int)floor(gtime * 6) % 16], cam.x, cam.y, VW, VH);
    // ponytail: the web copies reflections in 2px strips with a sine wobble at 38% alpha; here they draw
    // whole at 38% alpha (the wobble needs the offscreen buffer the GPU path doesn't have)
    cv_alpha(0.38);
    Map *map = world->map;
    for (int i = 0; i < map->nprops; i++) { Prop *p = &map->props[i]; if (!p->flat && nearWater(p->box.x + p->box.w / 2, p->foot)) { mirror(p->foot); drawProp(p, gtime); cv_restore(); } }
    for (int i = 0; i < world->nents; i++) { Ent *e = &world->ents[i]; if (eq(e->type, "npc") && nearWater(e->px, e->py)) { mirror(e->py); drawEntity(e); cv_restore(); } }
    if (eq(world->id, "farm")) JFOR(a, jget(state, "animals")) if (cJSON_IsNumber(jget(a, "px")) && nearWater(num(a, "px"), num(a, "py"))) { mirror(num(a, "py")); drawAnimal(a); cv_restore(); }
    if (nearWater(player.x, player.y)) { mirror(player.y); drawPlayer(&player); cv_restore(); }
    if (mascotOf(state) && nearWater(mascot.x, mascot.y)) { mirror(mascot.y); drawMascot(); cv_restore(); }
    cv_alpha(1);
}

typedef struct { double y; int idx, kind; void *p; } Draw;
enum { D_PROP, D_ENT, D_NODE, D_CROP, D_BUILT, D_ANIMAL, D_MACHINE, D_PLAYER, D_MASCOT };
static int cmpDraw(const void *a, const void *b) {   // stable: ties keep insertion order, like Array.sort
    const Draw *x = a, *y = b;
    return x->y < y->y ? -1 : x->y > y->y ? 1 : x->idx - y->idx;
}

static void draw(void) {
    Map *map = world->map;
    int mw = map->w * 16, mh = map->h * 16;
    // ponytail: the web lets the camera slide past the left edge by the tool bar width (safeLeft); the 3DS
    // hotbar is on the bottom screen, so safeLeft = 0.
    int cx = fmax(0, fmin(mw - VW, jround(player.x) - (VW >> 1)));
    int cy = fmax(0, fmin(mh - VH, jround(player.y) - (VH >> 1)));
    cam.x = cx - fmax(0, floor((VW - mw) / 2.0));
    cam.y = cy - fmax(0, floor((VH - mh) / 2.0));
    cv_target(cv_screen(0));
    cv_save();
    cv_scale(ZOOM, ZOOM);
    cv_fill(0, 0, VW, VH, eq(world->id, "mine") ? 0x1b1218ff : 0x193c3eff);
    cv_save();
    int shx, shy; fx_shakeOffset(&shx, &shy);
    cv_translate(-cam.x + shx, -cam.y + shy);

    // flat layers: animated water (under the ground, which has holes there), ground, flat props, soil, belts
    if (map->mask) drawWater();
    cv_draw(map->ground, 0, 0);
    for (int i = 0; i < map->nprops; i++) if (map->props[i].flat) drawProp(&map->props[i], gtime);
    J *flatList[256]; int nflat = 0;
    if (eq(world->id, "farm")) {
        drawSoil(state, SOILDRY, SOILWET);
        JFOR(m, jget(state, "machines")) if (isFlat(m)) { drawFlatPiece(m); if (nflat < 256) flatList[nflat++] = m; }
        drawBeltItems(flatList, nflat);
    }
    if (world->hasLadder) cv_draw(sprite("hole"), world->ladder.x * 16, world->ladder.y * 16);

    // y-sorted world (sorted by where things touch the ground)
    static Draw list[1024]; int n = 0;
#define ADD(Y, K, P) if (n < 1024) list[n] = (Draw){ (Y), n, (K), (P) }, n++
    for (int i = 0; i < map->nprops; i++) { Prop *p = &map->props[i]; if (!p->flat) ADD(p->hasCol ? p->col.y + p->col.h : p->foot, D_PROP, p); }
    for (int i = 0; i < world->nents; i++) {
        Ent *e = &world->ents[i];
        if (eq(e->type, "warp")) continue;
        double y = eq(e->type, "npc") ? e->py : eq(e->type, "building") ? (e->hasCol ? e->col.y + e->col.h : (e->y + e->h) * 16) : eq(e->type, "ladder") ? 0 : (e->y + 1) * 16;
        ADD(y, D_ENT, e);
    }
    for (int i = 0; i < world->nnodes; i++) ADD((world->nodes[i].y + 1) * 16 - 1, D_NODE, &world->nodes[i]);
    if (eq(world->id, "farm")) {
        JFOR(t, jget(state, "soil")) if (str(t, "crop")) { int x, y; sscanf(t->string, "%d,%d", &x, &y); ADD((y + 1) * 16 - 2, D_CROP, t); }
        J *bs[8]; int nb = builtBuildings(bs);
        for (int i = 0; i < nb; i++) ADD((num(bs[i], "y") + num(bs[i], "h")) * 16, D_BUILT, bs[i]);
        JFOR(a, jget(state, "animals")) if (cJSON_IsNumber(jget(a, "px"))) ADD(num(a, "py"), D_ANIMAL, a);
        JFOR(m, jget(state, "machines")) if (!isFlat(m)) ADD(machineY(state, m), D_MACHINE, m);
    }
    ADD(player.y, D_PLAYER, NULL);
    if (mascotOf(state)) ADD(mascot.y, D_MASCOT, NULL);
    qsort(list, n, sizeof(Draw), cmpDraw);
    Img *crops = I("assets/Cute_Fantasy/Crops/Crops.png");
    for (int i = 0; i < n; i++) {
        Draw *d = &list[i];
        switch (d->kind) {
        case D_PROP: drawProp(d->p, gtime); break;
        case D_ENT: drawEntity(d->p); break;
        case D_NODE: {
            Node *nd = d->p; J *def = jget(T("NODES"), nd->node);
            int sx = nd->shake > 0 ? jround(sin(gtime * 60)) : 0;
            cv_image(I(str(def, "img")), num(def, "sx"), num(def, "sy"), 16, 16, nd->x * 16 + sx, nd->y * 16, 16, 16);
            break;
        }
        case D_CROP: { J *t = d->p; int x, y; sscanf(t->string, "%d,%d", &x, &y); drawCrop(crops, x, y, t); break; }
        case D_BUILT: drawBuilt(d->p); break;
        case D_ANIMAL: drawAnimal(d->p); break;
        case D_MACHINE: drawMachine(d->p); break;
        case D_PLAYER: fx_shadow(player.x, player.y, 12); drawPlayer(&player); break;
        case D_MASCOT: fx_shadow(mascot.x, mascot.y + 2, 10); drawMascot(); break;
        }
    }

    // the rod sprite draws the line, bobber and splash itself; only the "!" goes on top
    if (fishing.on && eq(fishing.phase, "bite")) cv_draw(sprite("bang"), jround(player.x) - 2, jround(player.y) - 40);
    if (eq(world->id, "farm")) drawSprays();
    fx_drawWorld();
    fx_drawAmbient();
    for (int i = 0; i < nbubbles; i++) cv_draw(sprite(bubbles[i].sprite), jround(bubbles[i].x) - 3, jround(bubbles[i].y - (1.2 - bubbles[i].t) * 8));

    // target cursor / build preview
    if (!player.acting && !fishing.on) {
        Tile t = facingTile(&player);
        J *bp = blueprint();
        if (bp && eq(world->id, "farm")) {
            PlanPiece plan[64]; int np = blueprintPlan(bp, t, plan);
            for (int i = 0; i < np; i++) {
                int sz = num(jget(T("MACHINES"), plan[i].type), "size");
                char k[48]; snprintf(k, sizeof k, "machine_%s", plan[i].type);
                cv_alpha(0.55);
                drawItem(k, plan[i].x * 16 + (sz - 1) * 8, plan[i].y * 16 + (sz - 1) * 8);
                cv_alpha(1);
                cv_fill(plan[i].x * 16, plan[i].y * 16, sz * 16, sz * 16, eq(plan[i].st, "ok") ? css("rgba(99,199,77,0.3)") : eq(plan[i].st, "missing") ? css("rgba(254,174,52,0.35)") : css("rgba(228,59,68,0.3)"));
            }
            fx_cursor(t.x, t.y, gtime, true, 1, 1);
        } else if (eq(str(state, "tool"), "build") && str(state, "build") && eq(world->id, "farm")) {
            const char *b = str(state, "build");
            int sz = num(jget(T("MACHINES"), b), "size");
            bool ok = canPlace(state, b, t.x, t.y, freeForBuild);
            cv_fill(t.x * 16, t.y * 16, sz * 16, sz * 16, ok ? css("rgba(99,199,77,0.3)") : css("rgba(228,59,68,0.3)"));
            fx_cursor(t.x, t.y, gtime, ok, sz, sz);
            static const char *NO_DIR[] = { "chest", "autosell", "sprinkler", "solar", "pole", "steam" };
            bool nodir = false;
            for (int i = 0; i < 6; i++) nodir |= eq(b, NO_DIR[i]);
            if (!nodir) { Tile o = outputCellOf(b, t.x, t.y, player.dir); cv_fill(o.x * 16 + 6, o.y * 16 + 6, 4, 4, css("rgba(255,255,255,0.6)")); }
        } else if (!(in.held & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT))) fx_cursor(t.x, t.y, gtime, true, 1, 1);
    }
    if (!decor.on) {
        if (tut.on) {   // drawTutorial
            double tx = tut.t.x * 16 + 8, ty = tut.t.y * 16 - 2, sx = tx - cam.x, sy = ty - cam.y;
            Img *arrow = sprite("arrow");
            if (sx < 8 || sy < 8 || sx > VW - 8 || sy > VH - 8) {
                // off screen: arrow on the edge pointing at it
                double ex = fmax(14, fmin(VW - 14, sx)), ey = fmax(14, fmin(VH - 14, sy));
                cv_save();
                cv_translate(cam.x + ex, cam.y + ey);
                cv_rotate(atan2(sy - ey, sx - ex) - M_PI / 2);
                cv_draw(arrow, -4, -4 + jround(sin(gtime * 6) * 2));
                cv_restore();
            } else {
                // highlight the tile; the bouncing arrow only when the player isn't right next to it
                Tile me = playerTile(&player);
                if (fmax(abs(me.x - tut.t.x), abs(me.y - tut.t.y)) > 1) cv_draw(arrow, jround(tx - 4), jround(ty - 12 + jround(fabs(sin(gtime * 4)) * -4)));
                double pa = 0.15 + sin(gtime * 6) * 0.1, sa = 0.6 + sin(gtime * 6) * 0.3;
                cv_fill(tut.t.x * 16, tut.t.y * 16, 16, 16, 0xf2c14e00 | (uint32_t)(pa * 255 + 0.5));
                uint32_t sc = 0xf2c14e00 | (uint32_t)(fmin(1, sa) * 255 + 0.5);   // strokeRect at +0.5: a 1px outline
                cv_fill(tut.t.x * 16, tut.t.y * 16, 16, 1, sc); cv_fill(tut.t.x * 16, tut.t.y * 16 + 15, 16, 1, sc);
                cv_fill(tut.t.x * 16, tut.t.y * 16 + 1, 1, 14, sc); cv_fill(tut.t.x * 16 + 15, tut.t.y * 16 + 1, 1, 14, sc);
            }
        }
    } else if (map->floor.w) {
        Rect f = map->floor;
        uint32_t c = css("rgba(255,240,200,0.18)");
        for (int x = f.x; x <= f.x + f.w; x++) cv_fill(x * 16, f.y * 16, 1, f.h * 16, c);
        for (int y = f.y; y <= f.y + f.h; y++) cv_fill(f.x * 16, y * 16, f.w * 16, 1, c);
    }
    cv_restore();

    double l0 = profNow();
    drawLights();
    prof[2] += profNow() - l0;
    fx_drawFireflies(cam.x, cam.y);
    cv_restore();
    hud_drawFloaters(cam.x, cam.y, ZOOM);   // text stays 1x, like the web's DOM floaters
}

// ---------- loop ----------
static bool isBusy(void) { return hud_busy() || transitioning; }

void game_frame(double dt) {
    double p0 = profNow();
    gtime += dt;
    // buttons → the web's input events: A = act, B = menu/back, X/Y/L/R = tools 1-4
    static const uint32_t TOOLBTN[4] = { BTN_Y, BTN_X, BTN_L, BTN_R };
    if (in.down & BTN_B) { if (isBusy()) hud_back(); else shops_mainMenu(); }
    if (in.down & BTN_A && !isBusy()) {
        if (fishing.on) fishTap();
        else if (decor.on) decorTap(facingTile(&player));
        else interact(facingTile(&player));
    }
    for (int i = 0; i < 4; i++) if (in.down & TOOLBTN[i] && !isBusy()) { const char *l[16]; int n = toolList(l); if (i < n) selectTool(l[i]); }

    double dx = ((in.held & BTN_RIGHT) ? 1 : 0) - ((in.held & BTN_LEFT) ? 1 : 0), dy = ((in.held & BTN_DOWN) ? 1 : 0) - ((in.held & BTN_UP) ? 1 : 0);
    bool stick = dx || dy;
    double px0 = player.x, py0 = player.y;
    if (!isBusy()) {
        if (fishing.on) {
            updateFishing(dt);
            updatePlayer(&player, dt, 0, 0, blocked);
            if (fishing.on && stick && !eq(fishing.phase, "reel")) endFishing();
        } else updatePlayer(&player, dt, player.acting ? 0 : dx, player.acting ? 0 : dy, blocked);
        footsteps(px0, py0);
        checkWarps(dt);
    }
    if (!isBusy()) tickClock(dt);
    setMusicMood(eq(world->id, "mine") ? "mine" : eq(world->id, "town") && isFestival(state) ? "festival" : num(state, "time") >= 20 * 60 ? "night" : seasonId(state));
    if (!isBusy() && npopups) { Popup fn = popups[0].fn; char arg[64]; memcpy(arg, popups[0].arg, 64); memmove(popups, popups + 1, --npopups * sizeof popups[0]); fn(arg); }
    updateNpcs(dt);
    if (eq(world->id, "farm")) updateAnimals(dt);
    updateMascot(dt);
    // sprinklers spraying in daylight keep their 8 tiles wet (they also water overnight)
    if (spraying() && floor(gtime) != floor(gtime - dt))
        JFOR(m, jget(state, "machines")) if (eq(str(m, "type"), "sprinkler")) for (int yy = -1; yy <= 1; yy++) for (int xx = -1; xx <= 1; xx++) { J *t = soilAt(state, num(m, "x") + xx, num(m, "y") + yy); if (t) setbool(t, "wet", true); }
    // the factory lives on the farm but keeps running wherever you are
    simAcc += dt;
    while (simAcc >= STEP) {
        simAcc -= STEP;
        int sold, items;
        step(state, NULL, &sold, &items);
        if (sold || items) changed();
        scene_step();
    }
    fx_update(dt);
    fx_updateAmbient(dt, ambientKind(), isNight() && !world->map->indoor && !eq(world->id, "mine"), cam.x, cam.y, VW, VH);
    int nb = 0;
    for (int i = 0; i < nbubbles; i++) if ((bubbles[i].t -= dt) > 0) bubbles[nb++] = bubbles[i];
    nbubbles = nb;
    for (int i = 0; i < world->map->nprops; i++) if (world->map->props[i].shake > 0) world->map->props[i].shake -= dt;
    for (int i = 0; i < world->nnodes; i++) if (world->nodes[i].shake > 0) world->nodes[i].shake -= dt;
    tutorialTarget();
    double p1 = profNow();
    prof[0] += p1 - p0;
    draw();
    prof[1] += profNow() - p1;
    hud_frame(dt);
    // autosave every 10 s if something changed
    static double saveT;
    if ((saveT += dt) >= 10) { saveT = 0; if (dirty) persist(); }
}

// ---------- tutorial hookup ----------
// Asks tutorial.c / quests.c what to point at; on another map, point at the exit that leads there.
static void tutorialTarget(void) {
    Hint h = tutorial_update(world->id, player.x, player.y);   // fills the objective chip, returns the target
    tut.on = h.has;
    if (!h.has) return;
    if (!eq(h.map, world->id)) {
        const char *hop = nextHop(world->id, h.map);
        Ent *exit = NULL;
        for (int i = 0; hop && i < world->nents && !exit; i++) {
            Ent *e = &world->ents[i];
            if ((eq(e->type, "warp") && eq(str(e->d, "to"), hop)) || eq(str(e->d, "interior"), hop)) exit = e;
        }
        if (!exit) { tut.on = false; return; }
        J *door = jget(exit->d, "door");
        tut.t = door ? (Tile){ num(door, "x"), num(door, "y") } : str(exit->d, "interior") ? (Tile){ exit->x + exit->w / 2, exit->y + exit->h - 1 } : (Tile){ exit->x, exit->y };
    } else tut.t = (Tile){ h.x, h.y };
}
// live NPC tile (quest markers) when they're on this map
bool game_npcTile(const char *name, Tile *t) {
    for (int i = 0; world && i < world->nents; i++) if (eq(world->ents[i].type, "npc") && eq(str(world->ents[i].d, "name"), name)) { *t = tileOf(world->ents[i].px, world->ents[i].py); return true; }
    return false;
}
bool game_bedTile(Tile *t) {
    if (!world || !eq(world->id, "house")) return false;
    for (int i = 0; i < world->map->nprops; i++) { Prop *p = &world->map->props[i]; if (eq(p->kind, "bed")) { *t = (Tile){ floor((p->col.x + p->col.w / 2) / 16), floor((p->col.y + p->col.h / 2) / 16) }; return true; } }
    return false;
}
bool game_nearestDebris(const char **tool, Tile *t) {
    if (!world || !eq(world->id, "farm")) return false;
    double bd = 1e9; bool found = false;
    for (int i = 0; i < world->map->nprops; i++) {
        Prop *p = &world->map->props[i];
        const char *tl = str(jget(T("OBJECT_KINDS"), p->kind), "tool");
        if (!tl || eq(p->kind, "grass") || !p->hasCol) continue;
        double d = hypot(p->col.x - player.x, p->col.y - player.y);
        if (d < bd) { bd = d; *tool = tl; *t = (Tile){ floor((p->col.x + p->col.w / 2) / 16), floor((p->col.y + p->col.h / 2) / 16) }; found = true; }
    }
    return found;
}

// ---------- boot ----------
void game_boot(void) {
    char *txt = readFile("romfs:/data.json", NULL);
    DATA = cJSON_Parse(txt);
    free(txt);
    srand((unsigned)nowMs());
    state = loadState();
    setMuted(has(state, "muted"));
    plog("boot: data %.0f ms", profNow());
    waterFrames(WATER);
    soilTiles(SOILDRY, SOILWET);
    plog("boot: tiles %.0f ms", profNow());
    loadPlayerSprites();
    plog("boot: player %.0f ms", profNow());
    player = (Player){ .dir = DOWN };
    const char *id = jget(T("MAPS"), str(state, "map")) ? str(state, "map") : "farm";
    char mid[16]; snprintf(mid, sizeof mid, "%s", id);
    bool hasPos = cJSON_IsNumber(jget(state, "px"));
    enterMap(mid, true, hasPos ? num(state, "px") / 16 : num(jget(jget(T("MAPS"), mid), "spawn"), "x"), hasPos ? num(state, "py") / 16 : num(jget(jget(T("MAPS"), mid), "spawn"), "y"));
    plog("boot: map %.0f ms", profNow());
    setnum(state, "energy", fmin(num(state, "energy"), maxEnergy(state)));
    refreshTools();
    hud_boot();
}

void game_quit(void) { persist(); }

// ---- for shops.c ----
void game_popup(void (*fn)(void *), const void *arg, size_t n) { popup(fn, arg, n); }
void game_drawFlatPiece(J *m) { drawFlatPiece(m); }
void game_drawBeltItems(J **list, int n) { drawBeltItems(list, n); }
double game_machineY(J *st, J *m) { return machineY(st, m); }
// location.reload() after switching / resetting a save: drop the running farm and boot the one now in KEY.
void game_reload(void) {
    hud_closeMenu();
    cJSON_Delete(state);
    state = loadState();
    setMuted(has(state, "muted"));
    npopups = 0;   // ponytail: queued popups belong to the old farm (a pending quest-done tree leaks)
    fishing.on = false; transitioning = false; nbubbles = 0; simAcc = 0;
    decor.on = false; decor.carrying = false; decor.item[0] = 0; hud_decor(false);
    player = (Player){ .dir = DOWN };
    const char *id = jget(T("MAPS"), str(state, "map")) ? str(state, "map") : "farm";
    char mid[16]; snprintf(mid, sizeof mid, "%s", id);
    bool hasPos = cJSON_IsNumber(jget(state, "px"));
    enterMap(mid, true, hasPos ? num(state, "px") / 16 : num(jget(jget(T("MAPS"), mid), "spawn"), "x"), hasPos ? num(state, "py") / 16 : num(jget(jget(T("MAPS"), mid), "spawn"), "y"));
    setnum(state, "energy", fmin(num(state, "energy"), maxEnergy(state)));
    refreshTools();
}

