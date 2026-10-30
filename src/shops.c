// Every building / NPC menu (shops.js) + main.js machineMenu. `g` (the game API) is game.c's functions.
// ponytail: the web's 2-column `grid` rows are plain rows here; furniture rows use a "furniture:<id>" icon spec.
#include <stdio.h>
#include <stdarg.h>
#include "api.h"

#define T(name) table(name)
static J *ITEM(const char *id) { return jget(T("ITEMS"), id); }
static const char *itemName(const char *id) { const char *n = str(ITEM(id), "name"); return n ? n : id; }
static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static bool starts(const char *s, const char *p) { return s && !strncmp(s, p, strlen(p)); }
static J *critter(int id) { JFOR(c, T("CRITTERS")) if (num(c, "id") == id) return c; return NULL; }

void game_popup(void (*fn)(void *), const void *arg, size_t n);
void game_reload(void);

// string builder (template literals / [..].filter(Boolean).join(sep))
typedef struct { char b[800]; size_t n; } Sb;
static void sb(Sb *s, const char *fmt, ...) {
    if (s->n >= sizeof s->b - 1) return;
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(s->b + s->n, sizeof s->b - s->n, fmt, ap);
    va_end(ap);
    if (k > 0) s->n = s->n + k < sizeof s->b ? s->n + k : sizeof s->b - 1;
}
static void sbpart(Sb *s, const char *sep, const char *part) { if (!part || !*part) return; if (s->n) sb(s, "%s", sep); sb(s, "%s", part); }

// toLocaleString('en-US'): 12,345
static const char *loc(double v) {
    static char buf[6][24]; static int k;
    char *b = buf[k++ % 6], t[24];
    long n = (long)v;
    snprintf(t, sizeof t, "%ld", n < 0 ? -n : n);
    int len = strlen(t), o = 0;
    if (n < 0) b[o++] = '-';
    for (int i = 0; i < len; i++) { if (i && (len - i) % 3 == 0) b[o++] = ','; b[o++] = t[i]; }
    b[o] = 0;
    return b;
}
static const char *coins(void) { static char b[40]; snprintf(b, sizeof b, ":coin: %s", loc(num(state, "money"))); return b; }
static int priceOf(const char *id) { return jround(num(ITEM(id), "sell") * sellMult(state)); }
static void iconItem(Row *r, const char *id) { char b[64]; snprintf(b, sizeof b, "item:%s", id); row_icon(r, b); }
static void iconCritter(Row *r, int id) { char b[24]; snprintf(b, sizeof b, "critter:%d", id); row_icon(r, b); }
static Row *back(const char *label, RowFn fn) { Row *r = menu_row(label, NULL, fn, NULL, 0); row_stay(r); return r; }
static void todayStr(char out[11]) { today(out); }
static const char *ddmm(const char *d) { static char b[8]; snprintf(b, sizeof b, "%.2s/%.2s", d + 8, d + 5); return b; }   // `${d.slice(8)}/${d.slice(5, 7)}`

// ---------- costs ----------
typedef struct { char k[20]; int n; } CostE;
typedef struct { CostE c[4]; int nc; } Cost;
static Cost costOf(J *cost) {
    Cost c = { 0 };
    JFOR(e, cost) if (c.nc < 4) { snprintf(c.c[c.nc].k, sizeof c.c[0].k, "%s", e->string); c.c[c.nc++].n = numv(e); }
    return c;
}
static Cost money(int n) { Cost c = { 0 }; strcpy(c.c[0].k, "money"); c.c[0].n = n; c.nc = 1; return c; }
static J *costJ(const Cost *c) { J *o = cJSON_CreateObject(); for (int i = 0; i < c->nc; i++) setnum(o, c->c[i].k, c->c[i].n); return o; }
static int haveOf(const char *k) { return eq(k, "money") ? num(state, "money") : count(state, k); }

// Friendship discount (10% off coins) for the shop currently open.
static const char *shop;
static Cost withDiscount(Cost c) {
    if (shop && hasDiscount(state, shop)) for (int i = 0; i < c.nc; i++) if (eq(c.c[i].k, "money") && c.c[i].n) c.c[i].n = jround(c.c[i].n * 0.9);
    return c;
}

// Generic "buy with cost" menu row. The JS onBuy closures become an action code + id.
enum { B_GIVE, B_SMELT, B_TOOL, B_ROD1, B_ROD2, B_MACHINE, B_BUILDING, B_HOUSE, B_UPGRADE };
typedef struct { Cost cost; int act; char id[24]; int n; } BuyArg;
static bool buyClick(void *arg) {
    BuyArg *b = arg;
    J *s = state, *cost = costJ(&b->cost);
    pay(s, cost);
    cJSON_Delete(cost);
    switch (b->act) {
    case B_GIVE: give(s, b->id, b->n); break;
    case B_SMELT: { give(s, b->id, 1); J *st = child(s, "stats"); setnum(st, "smelted", num(st, "smelted") + 1); break; }
    case B_TOOL: setnum(child(s, "tools"), b->id, b->n); refreshTools(); break;
    case B_ROD1: setnum(child(s, "tools"), "rod", 1); setstr(s, "tool", "rod"); refreshTools(); toast("Rod in hand! Tap the water to fish."); break;
    case B_ROD2: setnum(child(s, "tools"), "rod", 2); refreshTools(); break;
    case B_MACHINE: {
        char k[40]; snprintf(k, sizeof k, "machine_%s", b->id);
        give(s, k, b->n);
        if (!str(s, "build")) setstr(s, "build", b->id);
        refreshTools();
        break;
    }
    case B_BUILDING: setbool(child(s, "buildings"), b->id, true); game_onBuilt(b->id); toast("%s is ready on the farm!", str(jget(T("BUILDINGS"), b->id), "name")); break;
    case B_HOUSE: { J *h = child(s, "house"); setnum(h, "level", num(h, "level") + 1); toast("House renovated! Go in and see."); game_onHouse(); break; }
    case B_UPGRADE: setbool(child(s, "upgrades"), b->id, true); break;
    }
    sfx("coin"); changed();
    return true;
}
static Row *buyRow(const char *icon, const char *label, const char *sub, Cost cost, bool can, const char *why, int act, const char *id, int n) {
    cost = withDiscount(cost);
    J *cj = costJ(&cost);
    bool ok = can && canAfford(state, cj);
    cJSON_Delete(cj);
    Sb items = { 0 }, miss = { 0 }, t = { 0 };
    int price = -1;
    for (int i = 0; i < cost.nc; i++) {
        const CostE *e = &cost.c[i];
        if (eq(e->k, "money")) price = e->n;
        else { if (items.n) sb(&items, " + "); sb(&items, "%d %s", e->n, itemName(e->k)); }
        int have = haveOf(e->k);
        if (have < e->n) { if (miss.n) sb(&miss, ", "); if (eq(e->k, "money")) sb(&miss, "%d coins", e->n - have); else sb(&miss, "%d %s", e->n - have, itemName(e->k)); }
    }
    sbpart(&t, " · ", sub);
    sbpart(&t, " · ", items.b);
    if (!can) sbpart(&t, " · ", why);
    else if (!ok) { char f[300]; snprintf(f, sizeof f, "need %s", miss.b); sbpart(&t, " · ", f); }
    BuyArg a = { cost, act, "", n };
    snprintf(a.id, sizeof a.id, "%s", id ? id : "");
    Row *r = menu_row(label, t.n ? t.b : NULL, buyClick, &a, sizeof a);
    if (icon) row_icon(r, icon);
    if (price >= 0) row_price(r, price);
    if (!ok) row_disabled(r);
    return r;
}

// ---------- shipping ----------
typedef struct { char title[40]; bool fish; } SellArg;
static int sellable(bool fish, char ids[][24], int max) {
    int n = 0;
    JFOR(e, jget(state, "inv")) {
        const char *id = e->string;
        if (n < max && ITEM(id) && num(ITEM(id), "sell") > 0 && !starts(id, "seed_") && !starts(id, "machine_") && !eq(id, "feed") && (!fish || has(ITEM(id), "fish")))
            snprintf(ids[n++], 24, "%s", id);
    }
    return n;
}
static void sellOne(const char *id) { int n = count(state, id); take(state, id, n); earn(state, priceOf(id) * n); }
static void sellMenu(const char *title, bool fish);
static void sellRefresh(void *arg) { SellArg *a = arg; sellMenu(a->title, a->fish); }
static bool sellAll(void *arg) {
    SellArg *a = arg;
    char ids[64][24]; int n = sellable(a->fish, ids, 64), total = 0;
    for (int i = 0; i < n; i++) total += priceOf(ids[i]) * count(state, ids[i]);
    for (int i = 0; i < n; i++) sellOne(ids[i]);
    toast("+%d coins", total); sfx("coin"); changed();
    return true;
}
static bool sellRow(void *arg) { sellOne(arg); sfx("coin"); changed(); return true; }
static void sellMenu(const char *title, bool fish) {
    SellArg a = { "", fish };
    snprintf(a.title, sizeof a.title, "%s", title);
    char ids[64][24]; int n = sellable(fish, ids, 64), total = 0;
    for (int i = 0; i < n; i++) total += priceOf(ids[i]) * count(state, ids[i]);
    char t[96]; snprintf(t, sizeof t, "%s · %s", title, coins());
    menu_begin(t);
    Row *r = menu_row(total ? "Sell all" : "Nothing to sell here", NULL, sellAll, &a, sizeof a);
    row_icon(r, "coins");
    if (total) row_price(r, total); else row_disabled(r);
    for (int i = 0; i < n; i++) {
        const char *id = ids[i];
        char label[96], sub[96];
        snprintf(label, sizeof label, "%s ×%d", itemName(id), count(state, id));
        snprintf(sub, sizeof sub, "%d each · tap to sell all", priceOf(id));
        r = menu_row(label, sub, sellRow, id, strlen(id) + 1);
        iconItem(r, id);
        row_price(r, priceOf(id) * count(state, id));
    }
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(sellRefresh, &a, sizeof a);
    menu_end();
}
void shops_sellMenu(void) { sellMenu("Shipping Bin", false); }

// ---------- town shops ----------
static bool toMarketSell(void *a) { sellMenu("General Store", false); return false; }
static void marketRefresh(void *a) { shops_marketMenu(); }
void shops_marketMenu(void) {
    J *s = state;
    shop = "market";
    char t[96];
    snprintf(t, sizeof t, "General Store · %s", coins());
    menu_begin(t);
    menu_text("Chloe: \"%s seeds! Out of season, they won't grow.\"%s", str(season(s), "name"), hasDiscount(s, "market") ? " (Friend: 10% off)" : "");
    JFOR(e, T("CROP_IDS")) {
        const char *id = e->valuestring;
        if (!inSeason(s, id)) continue;
        J *c = jget(T("CROPS"), id);
        bool locked = num(c, "rep") > num(jget(s, "rep"), "local");
        char seed[40], icon[48], label[80], sub[120], why[80];
        snprintf(seed, sizeof seed, "seed_%s", id);
        snprintf(icon, sizeof icon, "item:%s", seed);
        snprintf(label, sizeof label, "%s Seed", str(c, "name"));
        snprintf(sub, sizeof sub, "%d days · sells %d · have %d", (int)num(c, "days"), (int)num(c, "sell"), count(s, seed));
        snprintf(why, sizeof why, "needs %d local rep", (int)num(c, "rep"));
        buyRow(icon, label, sub, money(num(c, "seed")), !locked, why, B_GIVE, seed, 1);
    }
    char sub[80]; snprintf(sub, sizeof sub, "1 per animal per night · have %d", count(s, "feed"));
    buyRow("item:feed", "Feed", sub, money(15), true, NULL, B_GIVE, "feed", 1);
    buyRow("item:feed", "Feed ×10", NULL, money(140), true, NULL, B_GIVE, "feed", 10);
    row_stay(menu_row("Sell crops and goods", NULL, toMarketSell, NULL, 0));
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(marketRefresh, NULL, 0);
    menu_end();
}

static bool toFishSell(void *a) { sellMenu("Fish Shop", true); return false; }
static void fishRefresh(void *a) { shops_fishMenu(); }
void shops_fishMenu(void) {
    J *s = state, *rod = jget(T("UPGRADES"), "rod");
    shop = "fish";
    menu_begin("Fish Shop");
    menu_text("Fin: \"Patience is the best bait. When the '!' shows up, tap fast!\"");
    int lvl = num(jget(s, "tools"), "rod");
    if (!lvl) buyRow("tool:rod", "Fishing Rod", "Fish in the town or farm pond", costOf(jget(rod, "1")), true, NULL, B_ROD1, NULL, 0);
    else if (lvl < 2) buyRow("tool:rod", "Sturdy Rod", "More rare fish, and you can hook the legendary one", costOf(jget(rod, "2")), true, NULL, B_ROD2, NULL, 0);
    else row_disabled(menu_row("Your rod is already the best", NULL, NULL, NULL, 0));
    row_stay(menu_row("Sell fish", NULL, toFishSell, NULL, 0));
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(fishRefresh, NULL, 0);
    menu_end();
}

static bool toWorkshop(void *a) { workshopMenu(); return false; }
static void forgeRefresh(void *a) { shops_forgeMenu(); }
void shops_forgeMenu(void) {
    J *s = state;
    shop = "forge";
    char t[96]; snprintf(t, sizeof t, "Forge · %s", coins());
    menu_begin(t);
    menu_text("Mike: \"2 ore + 1 coal = 1 bar. With bars I make tools and machines.\"");
    static const char *BARS[3][2] = { { "copper_ore", "copper_bar" }, { "iron_ore", "iron_bar" }, { "gold_ore", "gold_bar" } };
    for (int i = 0; i < 3; i++) {
        const char *ore = BARS[i][0], *bar = BARS[i][1];
        char icon[48], label[80], sub[40];
        snprintf(icon, sizeof icon, "item:%s", bar);
        snprintf(label, sizeof label, "Smelt %s", itemName(bar));
        snprintf(sub, sizeof sub, "have %d", count(s, bar));
        Cost c = { 0 };
        snprintf(c.c[0].k, 20, "%s", ore); c.c[0].n = 2;
        strcpy(c.c[1].k, "coal"); c.c[1].n = 1;
        strcpy(c.c[2].k, "money"); c.c[2].n = 10;
        c.nc = 3;
        buyRow(icon, label, sub, c, true, NULL, B_SMELT, bar, 1);
    }
    JFOR(levels, T("UPGRADES")) {
        const char *tool = levels->string;
        if (eq(tool, "rod")) continue;
        int cur = num(jget(s, "tools"), tool), next = (cur ? cur : 1) + 1;
        char k[8]; snprintf(k, sizeof k, "%d", next);
        if (!jget(levels, k)) continue;
        const char *what = eq(tool, "axe") || eq(tool, "pickaxe") ? "double damage" : next == 2 ? "hits 3 tiles in a row" : "hits a 3x3 area";
        char icon[32], label[64], roman[8] = "";
        for (int i = 0; i < next && i < 7; i++) roman[i] = 'I', roman[i + 1] = 0;
        snprintf(icon, sizeof icon, "tool:%s", tool);
        snprintf(label, sizeof label, "%s %s", str(jget(T("TOOLS"), tool), "name"), roman);
        buyRow(icon, label, what, costOf(jget(levels, k)), true, NULL, B_TOOL, tool, next);
    }
    int tier = factoryTier(s);
    static const char *TIER_NAMES[] = { "", "First machines", "Production line", "Power!", "Logistics" };
    if (tier) menu_text("Machines — stage %d/4: %s (place them on the farm with the Build tool):", tier, TIER_NAMES[tier < 5 ? tier : 4]);
    else menu_text("Mike: \"Machines are serious business. Once your farm has a Coop, we'll talk.\" (Mike's quests unlock machines)");
    if (tier) { Row *r = menu_row("Workshop", "factory challenges, parts on Mike", toWorkshop, NULL, 0); row_icon(r, "wrench"); row_stay(r); }
    JFOR(m, T("MACHINES")) {
        if (num(m, "tier") > tier) continue;
        const char *id = m->string;
        char icon[48], label[80];
        snprintf(icon, sizeof icon, "item:machine_%s", id);
        if (num(m, "gives")) snprintf(label, sizeof label, "%s ×%d", str(m, "name"), (int)num(m, "gives"));
        else snprintf(label, sizeof label, "%s", str(m, "name"));
        buyRow(icon, label, str(m, "desc"), costOf(jget(m, "cost")), true, NULL, B_MACHINE, id, num(m, "gives") ? num(m, "gives") : 1);
    }
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(forgeRefresh, NULL, 0);
    menu_end();
}

static void furnitureShop(void);
static bool toFurniture(void *a) { furnitureShop(); return false; }
static bool buyAnimalRow(void *arg) {
    const char *kind = arg;
    J *a = jget(T("ANIMALS"), kind);
    int n = animalsIn(state, str(a, "home"));
    static const char *NAMES[3][4] = { { "Popcorn", "Clucky", "Penny", "Henrietta" }, { "Daisy", "Spot", "Star", "Cookie" }, { "Cloud", "Cotton", "Fluffy", "Woolly" } };
    int k = eq(kind, "chicken") ? 0 : eq(kind, "cow") ? 1 : 2;
    const char *err = buyAnimal(state, kind, NAMES[k][n % 4], nowMs());
    if (err) toast("%s", err); else toast("New %s on the farm!", str(a, "name"));
    if (!err) { sfx("coin"); changed(); }
    return true;
}
static void carpenterRefresh(void *a) { shops_carpenterMenu(); }
void shops_carpenterMenu(void) {
    J *s = state;
    shop = "carpenter";
    char t[96]; snprintf(t, sizeof t, "Carpenter · %s", coins());
    menu_begin(t);
    menu_text("Jack: \"Wood and stone in hand, buildings on the land!\"");
    JFOR(b, T("BUILDINGS")) {
        if (has(jget(s, "buildings"), b->string)) continue;
        buyRow(NULL, str(b, "name"), eq(b->string, "coop") ? "For chickens" : "For cows and sheep", costOf(jget(b, "cost")), true, NULL, B_BUILDING, b->string, 0);
    }
    int maxPer = num(DATA, "MAX_PER_HOME");
    JFOR(a, T("ANIMALS")) {
        const char *homeId = str(a, "home");
        J *home = jget(T("BUILDINGS"), homeId);
        int n = animalsIn(s, homeId);
        bool built = has(jget(s, "buildings"), homeId);
        char label[64], sub[120];
        snprintf(label, sizeof label, "Buy %s", str(a, "name"));
        if (!built) snprintf(sub, sizeof sub, "needs %s", str(home, "name"));
        else snprintf(sub, sizeof sub, "%s: %d/%d · gives %s", str(home, "name"), n, maxPer, itemName(str(a, "product")));
        Row *r = menu_row(label, sub, buyAnimalRow, a->string, strlen(a->string) + 1);
        row_price(r, num(a, "price"));
        if (!built || n >= maxPer || num(s, "money") < num(a, "price")) row_disabled(r);
    }
    int level = num(jget(s, "house"), "level");
    J *next = jat(T("HOUSE_LEVELS"), level + 1);
    if (cJSON_IsObject(next)) {
        char label[64], sub[80];
        snprintf(label, sizeof label, "Renovate House (lvl %d)", level + 1);
        snprintf(sub, sizeof sub, "Room %d×%d: more space to decorate", (int)num(next, "w"), (int)num(next, "d"));
        buyRow(NULL, label, sub, costOf(jget(next, "cost")), true, NULL, B_HOUSE, NULL, 0);
    }
    char sub[96]; snprintf(sub, sizeof sub, "House comfort: %d (+%d max energy)", comfort(s), comfortBonus(s));
    row_stay(menu_row(":chair: Furniture & decor", sub, toFurniture, NULL, 0));
    JFOR(u, T("HOUSE_UPGRADES")) {
        if (has(jget(s, "upgrades"), u->string)) continue;
        buyRow(NULL, str(u, "name"), str(u, "desc"), costOf(jget(u, "cost")), true, NULL, B_UPGRADE, u->string, 0);
    }
    buyRow("item:wood", "Wood ×10", NULL, money(60), true, NULL, B_GIVE, "wood", 10);
    buyRow("item:feed", "Feed ×10", NULL, money(140), true, NULL, B_GIVE, "feed", 10);
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(carpenterRefresh, NULL, 0);
    menu_end();
}

static bool buyFurniture(void *arg) {
    const char *id = arg;
    J *s = state, *d = child(s, "decor");
    setnum(s, "money", num(s, "money") - num(jget(T("FURNITURE"), id), "price"));
    setnum(d, id, num(d, id) + 1);
    sfx("coin"); changed();
    return true;
}
static bool toCarpenter(void *a) { shops_carpenterMenu(); return false; }
static void furnitureRefresh(void *a) { furnitureShop(); }
static void furnitureShop(void) {
    J *s = state;
    char t[96]; snprintf(t, sizeof t, "Furniture · %s", coins());
    menu_begin(t);
    menu_text("Jack: \"Good furniture makes a cozy house. Cozy house = more energy in the morning!\" Buy here, then place it at home with :chair: Decorate.");
    JFOR(f, T("FURNITURE")) {
        if (has(f, "hidden")) continue;
        const char *id = f->string;
        int stored = num(jget(s, "decor"), id);
        char sub[80], icon[64];
        if (stored) snprintf(sub, sizeof sub, "+%d comfort · stored: %d", (int)num(f, "comfort"), stored);
        else snprintf(sub, sizeof sub, "+%d comfort", (int)num(f, "comfort"));
        Row *r = menu_row(str(f, "name"), sub, buyFurniture, id, strlen(id) + 1);
        if (str(f, "pixel")) snprintf(icon, sizeof icon, "sprite:%s", str(f, "pixel")); else snprintf(icon, sizeof icon, "furniture:%s", id);
        row_icon(r, icon);
        row_price(r, num(f, "price"));
        if (num(s, "money") < num(f, "price")) row_disabled(r);
    }
    back("Back", toCarpenter);
    menu_refresh(furnitureRefresh, NULL, 0);
    menu_end();
}

static int innPrice(J *f) { Cost c = withDiscount(money(num(f, "price"))); return c.c[0].n; }
static bool eatRow(void *arg) {
    J *s = state, *f = jat(T("INN_FOOD"), *(int *)arg);
    setnum(s, "money", num(s, "money") - innPrice(f));
    setnum(s, "energy", fmin(maxEnergy(s), num(s, "energy") + num(f, "energy")));
    sfx("coin"); changed();
    return true;
}
static bool tokensRow(void *a) { setnum(state, "tokens", num(state, "tokens") - 10); setnum(state, "tickets", num(state, "tickets") + 1); changed(); return true; }
static void innRefresh(void *a) { shops_innMenu(); }
void shops_innMenu(void) {
    J *s = state;
    shop = "inn";
    int max = maxEnergy(s);
    menu_begin("Inn");
    J *rumors = T("RUMORS");
    menu_text("Katy: \"%s\"", jat(rumors, (int)(num(s, "day") + num(s, "money")) % jlen(rumors))->valuestring);
    int i = 0;
    JFOR(f, T("INN_FOOD")) {
        char sub[80]; snprintf(sub, sizeof sub, "+%d energy (%d/%d)", (int)num(f, "energy"), (int)num(s, "energy"), max);
        Row *r = menu_row(str(f, "name"), sub, eatRow, &i, sizeof i);
        row_price(r, innPrice(f));
        if (num(s, "money") < innPrice(f) || num(s, "energy") >= max) row_disabled(r);
        i++;
    }
    char sub[48]; snprintf(sub, sizeof sub, "You have %d tokens", (int)num(s, "tokens"));
    Row *r = menu_row("Trade 10 tokens for 1 capsule ticket", sub, tokensRow, NULL, 0);
    if (num(s, "tokens") < 10) row_disabled(r);
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(innRefresh, NULL, 0);
    menu_end();
}

static bool toProjects(void *a) { shops_projectsMenu(); return false; }
static bool payTaxRow(void *a) { payTax(state); toast("Tax paid. The town thanks you!"); sfx("coin"); changed(); return true; }
static bool deliverRow(void *arg) {
    J *d = deliverContract(state, *(double *)arg);
    if (d) {
        if (has(d, "last")) toast("Delivered! +%d", (int)num(d, "reward"));
        else toast("Weekly delivery done! +%d. Next by day %d.", (int)num(d, "reward"), (int)num(d, "due") + 7);
        sfx("coin"); changed();
        cJSON_Delete(d);
    }
    return true;
}
static bool acceptRow(void *arg) {
    const char *err = acceptContract(state, *(double *)arg);
    if (err) toast("%s", err); else toast("Contract accepted!");
    changed();
    return true;
}
static void boardRefresh(void *a) { shops_boardMenu(); }
void shops_boardMenu(void) {
    J *s = state;
    J *offers = contractOffers(s);
    int done = 0;
    JFOR(p, T("PROJECTS")) done += has(jget(s, "projects"), str(p, "id"));
    menu_begin("Notice Board");
    char label[200], sub[300];
    snprintf(label, sizeof label, ":home: Town Projects (%d/%d)", done, jlen(T("PROJECTS")));
    row_stay(menu_row(label, "Rebuild Pocket Town with what you produce", toProjects, NULL, 0));
    menu_text("Reputation: local %d · business %d", (int)num(jget(s, "rep"), "local"), (int)num(jget(s, "rep"), "business"));
    J *tax = jget(s, "tax");
    if (num(tax, "due")) {
        if (has(tax, "late")) snprintf(sub, sizeof sub, "OVERDUE! Business contracts locked");
        else snprintf(sub, sizeof sub, "Due on day %d", (int)num(tax, "due"));
        Row *r = menu_row("Pay tax", sub, payTaxRow, NULL, 0);
        row_price(r, num(tax, "amount"));
        if (num(s, "money") < num(tax, "amount")) row_disabled(r);
    }
    J *active = jget(jget(s, "contracts"), "active");
    menu_text("Your contracts (%d/3):", jlen(active));
    JFOR(c, active) {
        const char *item = str(c, "item");
        int have = count(s, item), qty = num(c, "qty"), weeks = num(c, "weeks");
        Sb l = { 0 }, u = { 0 };
        sb(&l, "%s: %d× %s", str(c, "client"), qty, itemName(item));
        if (weeks) sb(&l, " · weekly (%d deliver%s left)", weeks, weeks > 1 ? "ies" : "y");
        sb(&u, "%d/%d · pays %d%s%s · due day %d", have, qty, (int)num(c, "reward"), weeks == 1 ? " +50% bonus" : "", has(c, "ticket") ? " + :ticket:" : "", (int)num(c, "due"));
        if (have >= qty) sb(&u, " · TAP TO DELIVER");
        double id = num(c, "id");
        Row *r = menu_row(l.b, u.b, deliverRow, &id, sizeof id);
        iconItem(r, item);
        if (have < qty) row_disabled(r);
    }
    if (!jlen(active)) menu_text("None. Accept one below.");
    menu_text("Today's orders:");
    JFOR(o, offers) {
        const char *item = str(o, "item");
        int weeks = num(o, "weeks");
        snprintf(label, sizeof label, "%s: %d× %s%s", str(o, "client"), (int)num(o, "qty"), itemName(item), weeks ? " · weekly, 4×" : "");
        snprintf(sub, sizeof sub, "%s · pays %d%s%s · +%d rep · %d days · fine %d", eq(str(o, "kind"), "business") ? "Business" : "Local", (int)num(o, "reward"),
                 weeks ? " per delivery" : "", has(o, "ticket") ? " + :ticket:" : "", (int)num(o, "rep"), (int)(num(o, "due") - num(s, "day")), (int)num(o, "fine"));
        double id = num(o, "id");
        Row *r = menu_row(label, sub, acceptRow, &id, sizeof id);
        iconItem(r, item);
        if (jlen(active) >= 3) row_disabled(r);
    }
    if (!jlen(offers)) menu_text("Come back tomorrow for new orders.");
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(boardRefresh, NULL, 0);
    menu_end();
}

// ---------- player menus ----------
static bool toMain(void *a) { shops_mainMenu(); return false; }
static bool bagGroup(int g, const char *id) {
    static const char *PRODUCTS[] = { "egg", "milk", "wool", "cheese", "mayo", "yarn", "bread", "sauce", "feed" };
    switch (g) {
    case 0: return starts(id, "seed_");
    case 1: return starts(id, "machine_");
    case 2: { if (jget(T("CROPS"), id)) return true; for (int i = 0; i < 9; i++) if (eq(id, PRODUCTS[i])) return true; return false; }
    case 3: return has(ITEM(id), "fish");
    }
    return true;
}
static bool equipAction(void *arg) { equip(arg); return false; }
static void backpackMenu(void) {
    J *s = state, *inv = jget(s, "inv");
    static const char *TITLES[] = { "Seeds", "Machines", "Crops & goods", "Fish", "Materials & nature" };
    const char *ids[128]; int n = 0;
    JFOR(e, inv) if (numv(e) > 0 && ITEM(e->string) && n < 128) ids[n++] = e->string;
    bool used[128] = { 0 }, any = false;
    menu_begin(":bag: Backpack");
    for (int g = 0; g < 5; g++) {
        bool titled = false;
        for (int i = 0; i < n; i++) {
            const char *id = ids[i];
            if (used[i] || !bagGroup(g, id)) continue;
            if (!titled) { menu_title(TITLES[g]); titled = any = true; }
            used[i] = true;
            int c = count(s, id);
            char name[96], shortName[64], info[300];
            snprintf(name, sizeof name, "%s ×%d", itemName(id), c);
            const char *nm = itemName(id);
            size_t nl = strlen(nm);
            // the slot shows the icon, so "Carrot Seeds" → "Carrot", "Copper bar" → "Copper"
            static const char *SUFFIX[] = { " Seeds", " bar" };
            snprintf(shortName, sizeof shortName, "%s", nm);
            for (int k = 0; k < 2; k++) { size_t sl = strlen(SUFFIX[k]); if (nl > sl && !strcmp(nm + nl - sl, SUFFIX[k])) shortName[nl - sl] = 0; }
            Sb in = { 0 };
            if (starts(id, "machine_")) sbpart(&in, " · ", str(jget(T("MACHINES"), id + 8), "desc"));
            if (num(ITEM(id), "sell")) { char v[48]; snprintf(v, sizeof v, "Worth :coin: %d each", priceOf(id)); sbpart(&in, " · ", v); }
            else if (!starts(id, "machine_")) sbpart(&in, " · ", "Can't be sold");
            snprintf(info, sizeof info, "%s", in.b);
            char icon[48]; snprintf(icon, sizeof icon, "item:%s", id);
            menu_slot(icon, c, name, shortName, info, false);
            if (starts(id, "seed_") || starts(id, "machine_")) slot_action("Equip", equipAction, id, strlen(id) + 1);
        }
    }
    if (!any) menu_text("Backpack is empty.");
    back("Back", toMain);
    menu_end();
}

static void goalsMenu(void) {
    J *s = state;
    menu_begin("Goals");
    JFOR(goal, T("GOALS")) {
        char label[120], sub[64];
        snprintf(label, sizeof label, "%s%s", has(jget(s, "goals"), str(goal, "id")) ? ":check: " : "", str(goal, "name"));
        { int tk = num(goal, "tickets"); snprintf(sub, sizeof sub, "Reward: %d ticket%s", tk, tk == 1 ? "" : "s"); }
        row_disabled(menu_row(label, sub, NULL, NULL, 0));
    }
    J *st = jget(s, "stats");
    menu_text("Farm: %d factory parts, %d animals, %d harvests, %d fish.", jlen(jget(s, "machines")), jlen(jget(s, "animals")), (int)num(st, "harvested"), (int)num(st, "fished"));
    back("Back", toMain);
    menu_end();
}

// ---------- trading / friends ----------
// ponytail: no share sheet, clipboard, camera or QR on the 3DS: codes are shown as text and typed in with the keyboard.
static const char *farmName(void) { const char *n = str(state, "farmName"); return n && *n ? n : "Your farm"; }
static bool hasFarmName(void) { const char *n = str(state, "farmName"); return n && *n; }
static void tradeMenu(void);
static void sendGift(void);
static void openScore(void);
static void openGift(void);
static bool toTrade(void *a) { tradeMenu(); return false; }
static bool toFriends(void *a) { shops_friendsMenu(); return false; }

static void showCode(const char *title, const char *code, const char *message, const char *help) {
    if (!code) { toast("Invalid code."); return; }
    (void)message;   // the share text: there's nothing to share it with
    menu_begin(title);
    menu_text("%s", help);
    menu_title("Code:");
    menu_text("%s", code);
    menu_text("Your friend types this code in Friends → Trade critters.");
    menu_row("Done", NULL, NULL, NULL, 0);
    menu_end();
}

// The farm name goes in every link ("Oferta de Sítio do Sol").
enum { THEN_TRADE, THEN_SHARE, THEN_GIFT, THEN_BP };
static J *bpCur; static bool bpRecv;   // the blueprint on screen (owned copy)
static void nameThen(int then) {
    if (then == THEN_TRADE) tradeMenu();
    else if (then == THEN_SHARE) shops_shareScore();
    else if (then == THEN_GIFT) sendGift();
    else shops_blueprintMenu(bpCur, false);
}
static void nameSubmit(const char *v, void *arg) {
    // trim, collapse spaces, at most 24 characters
    char name[128]; int o = 0, chars = 0; bool sp = false;
    for (const char *p = v; *p && o < 120; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { sp = o > 0; continue; }
        if ((*p & 0xc0) != 0x80) { if (chars + (sp ? 1 : 0) >= 24) break; if (sp) { name[o++] = ' '; chars++; } chars++; }
        sp = false;
        name[o++] = *p;
    }
    name[o] = 0;
    if (!o) { toast("Type a name."); return; }
    setstr(state, "farmName", name); changed();
    nameThen(*(int *)arg);
}
static void nameFarm(int then) {
    menu_begin("Farm name");
    menu_text("Your friends will see this name in trades.");
    menu_input("E.g.: Sunny Acres", str(state, "farmName"), "Save", nameSubmit, &then, sizeof then);
    menu_end();
}

// Opens a trade code: an offer asks to accept, a reply finishes your own offer.
static J *code;    // the code on screen (offer / score / gift), owned
static J *rival;   // the friend's score handed to playDaily (kept alive until the next one)
static bool acceptTrade(void *a) {
    J *res = acceptOffer(state, code);
    if (!res || str(res, "error")) { toast("%s", res ? str(res, "error") : "Invalid code."); cJSON_Delete(res); return false; }
    changed();
    J *give = critter(num(code, "give")), *want = critter(num(code, "want"));
    char msg[200], help[240];
    snprintf(msg, sizeof msg, "%s accepted the trade: tap to get %s", farmName(), str(want, "name"));
    snprintf(help, sizeof help, "You got %s. The trade ends when %s opens this code.", str(give, "name"), str(code, "from") && *str(code, "from") ? str(code, "from") : "your friend");
    showCode("Trade accepted! Send the reply", str(res, "code"), msg, help);
    cJSON_Delete(res);
    return true;
}
void shops_openTradeCode(const char *v) {
    J *s = state, *r = readCode(v);
    if (!r || str(r, "error")) { toast("%s", r ? str(r, "error") : "Invalid code."); cJSON_Delete(r); return; }
    cJSON_Delete(code); code = r;
    const char *kind = str(r, "kind");
    bool score = eq(kind, "score");
    const char *who = score ? str(r, "n") : str(r, "from");
    if (who) {
        J *sc = NULL;
        if (score) { sc = cJSON_CreateObject(); setstr(sc, "d", str(r, "d")); setnum(sc, "s", num(r, "s")); setnum(sc, "t", num(r, "t")); setnum(sc, "p", num(r, "p")); }
        noteFriend(s, who, sc);
        cJSON_Delete(sc);
    }
    if (score) { openScore(); return; }
    if (eq(kind, "gift")) { openGift(); return; }
    if (eq(kind, "blueprint")) {
        J *bp = cJSON_CreateObject();
        setstr(bp, "name", str(r, "name") ? str(r, "name") : "Blueprint");
        setstr(bp, "from", str(r, "from"));
        cJSON_AddItemToObject(bp, "pieces", cJSON_Duplicate(jget(r, "pieces"), true));
        shops_blueprintMenu(bp, true);
        cJSON_Delete(bp);
        return;
    }
    if (eq(kind, "reply")) {
        J *done = applyReply(s, r);
        if (!done || str(done, "error")) { toast("%s", done ? str(done, "error") : "Invalid code."); cJSON_Delete(done); return; }
        toast("Trade complete! You got %s.", str(jget(done, "got"), "name")); changed();
        cJSON_Delete(done);
        tradeMenu();
        return;
    }
    J *give = critter(num(r, "give")), *want = critter(num(r, "want"));
    char wantKey[16]; snprintf(wantKey, sizeof wantKey, "%d", (int)num(want, "id"));
    int have = num(jget(s, "critters"), wantKey);
    char t[96], label[96], sub[48];
    snprintf(t, sizeof t, "Offer from %s", str(r, "from") && *str(r, "from") ? str(r, "from") : "a friend");
    menu_begin(t);
    snprintf(label, sizeof label, "You get: %s", str(give, "name"));
    Row *row = menu_row(label, NULL, NULL, NULL, 0); iconCritter(row, num(give, "id")); row_disabled(row);
    snprintf(label, sizeof label, "You give: %s", str(want, "name"));
    snprintf(sub, sizeof sub, "You have %d", have);
    row = menu_row(label, sub, NULL, NULL, 0); iconCritter(row, num(want, "id")); row_disabled(row);
    row = menu_row("Accept trade", NULL, acceptTrade, NULL, 0);
    if (!have) row_disabled(row);
    if (!have) menu_text("You don't have %s to trade yet.", str(want, "name"));
    back("Not now", toTrade);
    menu_end();
}

static J *scoreOf(J *r) { J *sc = cJSON_CreateObject(); setnum(sc, "s", num(r, "s")); setnum(sc, "t", num(r, "t")); setnum(sc, "p", num(r, "p")); return sc; }
static bool playRow(void *a) { playDaily(NULL); return false; }
static bool playRival(void *a) {
    cJSON_Delete(rival);
    rival = scoreOf(code);
    setstr(rival, "n", str(code, "n"));
    playDaily(rival);
    return false;
}
// A friend's daily score: play the same challenge with their score as the one to beat.
static void openScore(void) {
    J *r = code;
    changed();
    J *mine = myScore(state), *sc = scoreOf(r);
    char day[8], td[11], t[96], label[120], txt[64];
    snprintf(day, sizeof day, "%s", ddmm(str(r, "d")));
    todayStr(td);
    snprintf(t, sizeof t, "%s's score", str(r, "n"));
    menu_begin(t);
    if (!eq(str(r, "d"), td)) {
        menu_text("This score is from the %s challenge, which is over. Today's is different: why not send yours?", day);
        back("Daily Challenge", playRow);
        back("Friends", toFriends);
    } else {
        snprintf(label, sizeof label, "%s · %s challenge", str(r, "n"), day);
        Row *row = menu_row(label, scoreText(sc, txt, sizeof txt), NULL, NULL, 0); row_icon(row, "trophy"); row_disabled(row);
        if (mine) {
            row = menu_row("You", scoreText(mine, txt, sizeof txt), NULL, NULL, 0); row_icon(row, "star"); row_disabled(row);
            menu_text("%s", better(mine, sc) ? "You're ahead!" : "Can you do better?");
        } else menu_text("Think you can beat it?");
        back("Play the Daily Challenge", playRival);
        back("Friends", toFriends);
    }
    menu_end();
    cJSON_Delete(mine); cJSON_Delete(sc);
}

void shops_shareScore(void) {
    J *s = state, *mine = myScore(s);
    if (!mine) { toast("Play the Daily Challenge first."); return; }
    if (!hasFarmName()) { cJSON_Delete(mine); nameFarm(THEN_SHARE); return; }
    char day[11], stars[40] = "", msg[300];
    todayStr(day);
    for (int i = 0; i < 3; i++) strcat(stars, i < num(mine, "s") ? "★" : "☆");
    snprintf(msg, sizeof msg, "%s did the %s Daily Challenge in PocketFarm: %s, %d steps, %d pieces. Can you beat it?", str(s, "farmName"), ddmm(day), stars, (int)num(mine, "t"), (int)num(mine, "p"));
    char *c = scoreCode(s, mine, day);
    showCode("Send score", c, msg, "Whoever opens it plays today's challenge with your score as the goal. Their score shows up here in Friends when they send it back.");
    free(c);
    cJSON_Delete(mine);
}

// Friends: today's ranking (you + friends who sent today's score), the daily challenge and trades.
static bool shareRow(void *a) { shops_shareScore(); return false; }
static bool giftRow(void *a) { sendGift(); return false; }
void shops_friendsMenu(void) {
    J *s = state, *fl = jget(s, "friendsList");
    char day[11]; todayStr(day);
    J *rank[64]; int n = 0;
    JFOR(f, fl) {
        J *sc = jget(f, "score");
        if (!eq(str(sc, "d"), day) || n >= 63) continue;
        J *r = cJSON_Duplicate(sc, true); setstr(r, "n", f->string);
        rank[n++] = r;
    }
    J *mine = myScore(s);
    if (mine) { J *r = cJSON_Duplicate(mine, true); setstr(r, "n", hasFarmName() ? str(s, "farmName") : "You"); setbool(r, "me", true); rank[n++] = r; }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && !better(rank[j - 1], rank[j]) && better(rank[j], rank[j - 1]); j--) { J *t = rank[j]; rank[j] = rank[j - 1]; rank[j - 1] = t; }
    Sb others = { 0 };
    JFOR(f, fl) {
        bool in = false;
        for (int i = 0; i < n; i++) in |= eq(str(rank[i], "n"), f->string);
        if (!in) { if (others.n) sb(&others, ", "); sb(&others, "%s", f->string); }
    }
    menu_begin(":gift: Friends");
    menu_text("Daily Challenge %s: everyone plays the same one. Send your score and see who did best.", ddmm(day));
    for (int i = 0; i < n; i++) {
        char label[96], txt[64];
        bool me = has(rank[i], "me");
        snprintf(label, sizeof label, "%d. %s%s", i + 1, str(rank[i], "n"), me ? " (you)" : "");
        Row *r = menu_row(label, scoreText(rank[i], txt, sizeof txt), NULL, NULL, 0);
        row_icon(r, me ? "star" : "trophy"); row_disabled(r);
    }
    if (!n) menu_text("Nobody has played today's yet.");
    back(mine ? "Improve my score" : "Play the Daily Challenge", playRow);
    if (mine) row_icon(back("Send my score", shareRow), "gift");
    bool gifted = eq(str(s, "giftDay"), day);
    Row *r = menu_row("Send gift", gifted ? "already sent today" : "one a day, up to 10 of an item", giftRow, NULL, 0);
    row_icon(r, "gift"); row_stay(r); if (gifted) row_disabled(r);
    r = menu_row("Trade critters", "dupes with friends", toTrade, NULL, 0);
    row_icon(r, "gift"); row_stay(r);
    if (others.n) menu_text("Also sent codes: %s.", others.b);
    back("Back", toMain);
    menu_end();
    for (int i = 0; i < n; i++) cJSON_Delete(rank[i]);
    cJSON_Delete(mine);
}

static bool receiveGift(void *a) {
    J *res = acceptGift(state, code);
    if (!res || str(res, "error")) { toast("%s", res ? str(res, "error") : "Invalid code."); cJSON_Delete(res); return false; }
    cJSON_Delete(res);
    const char *name = itemName(str(code, "item"));
    int qty = num(code, "qty");
    sfx("pop"); toast("You got %d× %s!", qty, name); changed();
    char t[96], td[11]; todayStr(td);
    snprintf(t, sizeof t, "Gift from %s", str(code, "from"));
    menu_begin(t);
    menu_text("%d× %s went to your backpack.", qty, name);
    Row *r = menu_row("Send one back", NULL, giftRow, NULL, 0);
    row_icon(r, "gift"); row_stay(r);
    if (eq(str(state, "giftDay"), td)) row_disabled(r);
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_end();
    return true;
}
static void openGift(void) {
    J *r = code;
    const char *item = str(r, "item"), *name = itemName(item);
    char t[96], label[96], sub[96];
    snprintf(t, sizeof t, "Gift from %s", str(r, "from"));
    snprintf(label, sizeof label, "%d× %s", (int)num(r, "qty"), name);
    snprintf(sub, sizeof sub, "%s sent you this", str(r, "from"));
    menu_begin(t);
    Row *row = menu_row(label, sub, NULL, NULL, 0); iconItem(row, item); row_disabled(row);
    row_stay(menu_row("Receive", NULL, receiveGift, NULL, 0));
    menu_row("Not now", NULL, NULL, NULL, 0);
    menu_end();
}

typedef struct { char id[24]; int qty; } GiftArg;
static void giftQty(const char *id);
static bool giftQtyRow(void *arg) { giftQty(arg); return false; }
static bool toSendGift(void *a) { sendGift(); return false; }
static void sendGift(void) {
    J *s = state;
    if (!hasFarmName()) { nameFarm(THEN_GIFT); return; }
    menu_begin(":gift: Send gift");
    menu_text("Pick an item. It leaves your backpack now and goes to your friend via the code.");
    int n = 0;
    JFOR(e, jget(s, "inv")) {
        if (numv(e) <= 0 || !giftable(e->string)) continue;
        char sub[32]; snprintf(sub, sizeof sub, "have %d", (int)numv(e));
        Row *r = menu_row(itemName(e->string), sub, giftQtyRow, e->string, strlen(e->string) + 1);
        iconItem(r, e->string); row_stay(r);
        n++;
    }
    if (!n) menu_text("Your backpack has nothing to give yet.");
    back("Back", toFriends);
    menu_end();
}
static bool sendGiftQty(void *arg) {
    GiftArg *g = arg;
    char day[11]; todayStr(day);
    J *r = makeGift(state, g->id, g->qty, day);
    if (!r || str(r, "error")) { toast("%s", r ? str(r, "error") : "Can't send this gift."); cJSON_Delete(r); return false; }
    changed();
    char msg[200];
    snprintf(msg, sizeof msg, "%s sent you %d× %s in PocketFarm! Tap to receive.", str(state, "farmName"), g->qty, itemName(g->id));
    showCode("Send gift", str(r, "code"), msg, "The code can only be received once. You can send another gift tomorrow.");
    cJSON_Delete(r);
    return false;
}
static void giftQty(const char *id) {
    char t[96]; snprintf(t, sizeof t, "Send %s", itemName(id));
    menu_begin(t);
    static const int Q[] = { 1, 5, 10 };
    for (int i = 0; i < 3; i++) {
        if (Q[i] > count(state, id)) continue;
        GiftArg a = { "", Q[i] };
        snprintf(a.id, sizeof a.id, "%s", id);
        char label[96]; snprintf(label, sizeof label, "%d× %s", Q[i], itemName(id));
        Row *r = menu_row(label, NULL, sendGiftQty, &a, sizeof a);
        iconItem(r, id); row_stay(r);
    }
    back("Back", toSendGift);
    menu_end();
}

// A factory line as a blueprint: animated preview, what it needs, save it for the build tool, send it as a link.
static bool saveBlueprint(void *a) {
    J *s = state, *list = childarr(s, "blueprints"), *b = cJSON_Duplicate(bpCur, true);
    setstr(b, "from", bpRecv ? str(bpCur, "from") : "");
    cJSON_InsertItemInArray(list, 0, b);
    while (jlen(list) > 8) cJSON_DeleteItemFromArray(list, jlen(list) - 1);
    setnum(s, "blueprint", 0);
    toast("Blueprint saved! Pick it in the Build tool."); changed(); refreshTools();
    return false;
}
static bool sendBlueprint(void *a) {
    if (!hasFarmName()) { nameFarm(THEN_BP); return false; }
    char msg[200]; snprintf(msg, sizeof msg, "%s sent you a factory blueprint in PocketFarm: %s", str(state, "farmName"), str(bpCur, "name"));
    char *c = blueprintCode(state, bpCur);
    showCode("Send blueprint", c, msg, "Whoever opens it sees the line being built and can save the blueprint to copy it.");
    free(c);
    return false;
}
void shops_blueprintMenu(J *bp0, bool received) {
    J *copy = cJSON_Duplicate(bp0, true);
    cJSON_Delete(bpCur);
    bpCur = copy; bpRecv = received;
    J *s = state, *bp = bpCur, *pieces = jget(bp, "pieces");
    int w = 0, h = 0;
    const char *types[32]; int need[32], nt = 0;
    JFOR(p, pieces) {
        const char *t = jat(p, 0)->valuestring;
        int size = num(jget(T("MACHINES"), t), "size");
        w = fmax(w, numv(jat(p, 1)) + size); h = fmax(h, numv(jat(p, 2)) + size);
        int k = 0;
        while (k < nt && !eq(types[k], t)) k++;
        if (k == nt && nt < 32) { types[nt] = t; need[nt++] = 0; }
        if (k < 32) need[k]++;
    }
    bool saved = false;
    JFOR(b, jget(s, "blueprints")) saved |= eq(str(b, "name"), str(bp, "name")) && cJSON_Compare(jget(b, "pieces"), pieces, true);
    char t[96];
    if (received) snprintf(t, sizeof t, "Blueprint from %s", str(bp, "from") ? str(bp, "from") : "");
    else snprintf(t, sizeof t, ":book: Blueprint");
    menu_begin(t);
    J *sc = cJSON_CreateObject();
    setnum(sc, "w", w); setnum(sc, "h", h);
    cJSON_AddItemToObject(sc, "pieces", cJSON_Duplicate(pieces, true));
    setstr(sc, "caption", str(bp, "name"));
    scene_ponder(NULL, sc);
    cJSON_Delete(sc);
    Sb txt = { 0 };
    sb(&txt, "Needs: ");
    for (int i = 0; i < nt; i++) {
        char k[40]; snprintf(k, sizeof k, "machine_%s", types[i]);
        sb(&txt, "%s%d %s (have %d)", i ? ", " : "", need[i], str(jget(T("MACHINES"), types[i]), "name"), count(s, k));
    }
    sb(&txt, ".");
    menu_text("%s", txt.b);
    Row *r = menu_row(saved ? "Blueprint saved" : "Save blueprint", "shows up in the Build tool (tap it again to pick)", saveBlueprint, NULL, 0);
    row_icon(r, "book");
    if (saved) row_disabled(r);
    if (!received) { r = menu_row("Send to a friend", NULL, sendBlueprint, NULL, 0); row_icon(r, "gift"); row_stay(r); }
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_end();
}

static void pickWant(int give);
static bool pickWantRow(void *arg) { pickWant(*(int *)arg); return false; }
static bool farmNameRow(void *a) { nameFarm(THEN_TRADE); return false; }
static void codeSubmit(const char *v, void *arg) { shops_openTradeCode(v); }
static void tradeMenu(void) {
    J *s = state, *cr = jget(s, "critters");
    if (!hasFarmName()) { nameFarm(THEN_TRADE); return; }
    menu_begin("Trade critters");
    menu_text("Trade dupes with a friend: make an offer and send the code. They accept it and send a reply code back.");
    menu_input("Or enter a trade code here", "", "Open", codeSubmit, NULL, 0);
    int nd = 0;
    JFOR(c, T("CRITTERS")) {
        char k[16]; snprintf(k, sizeof k, "%d", (int)num(c, "id"));
        int have = num(cr, k);
        if (have <= 1) continue;
        if (!nd++) menu_text("Make an offer: pick the dupe you'll give.");
        char label[96]; snprintf(label, sizeof label, "%s ×%d", str(c, "name"), have);
        int id = num(c, "id");
        Row *r = menu_row(label, NULL, pickWantRow, &id, sizeof id);
        iconCritter(r, id); row_stay(r);
    }
    if (!nd) menu_text("You have no dupes to offer yet.");
    char label[96]; snprintf(label, sizeof label, "Farm: %s", str(s, "farmName"));
    row_stay(menu_row(label, "change name", farmNameRow, NULL, 0));
    back("Back", toFriends);
    menu_end();
}

typedef struct { int give, want; } WantArg;
static bool makeOfferRow(void *arg) {
    WantArg *w = arg;
    J *r = makeOffer(state, w->give, w->want, hasFarmName() ? str(state, "farmName") : "Farm");
    if (!r || str(r, "error")) { toast("%s", r ? str(r, "error") : "Pick a different critter to ask for."); cJSON_Delete(r); return false; }
    changed();
    char msg[200];
    snprintf(msg, sizeof msg, "%s wants to trade %s for %s in PocketFarm", farmName(), str(critter(w->give), "name"), str(critter(w->want), "name"));
    showCode("Send the code to your friend", str(r, "code"), msg, "When they accept, they'll send you a reply code: open it to finish the trade.");
    cJSON_Delete(r);
    return false;
}
static void pickWant(int give) {
    J *cr = jget(state, "critters");
    menu_begin("Ask in return");
    menu_text("You give %s. Which critter do you want?", str(critter(give), "name"));
    JFOR(c, T("CRITTERS")) {
        int id = num(c, "id");
        if (id == give) continue;
        char k[16], label[96]; snprintf(k, sizeof k, "%d", id);
        bool own = has(cr, k);
        snprintf(label, sizeof label, own ? "%s" : "%s (new!)", str(c, "name"));
        WantArg a = { give, id };
        Row *r = menu_row(label, NULL, makeOfferRow, &a, sizeof a);
        iconCritter(r, id); row_stay(r);   // ponytail: not-owned critters aren't drawn as silhouettes in rows
    }
    back("Back", toTrade);
    menu_end();
}

// ---------- main menu ----------
static bool albumRow(void *a) { hud_closeMenu(); openAlbum(); return false; }
static bool questLogRow(void *a) { shops_questLog(); return false; }
static bool bagRow(void *a) { backpackMenu(); return false; }
static bool goalsRow(void *a) { goalsMenu(); return false; }
static void contractsMenu(void);
static bool contractsRow(void *a) { contractsMenu(); return false; }
static bool soundRow(void *a) { setbool(state, "muted", !has(state, "muted")); changed(); shops_mainMenu(); return false; }
static bool musicRow(void *a) { bool off = !has(state, "musicOff"); setbool(state, "musicOff", off); changed(); if (!off) startMusic(); shops_mainMenu(); return false; }
static bool saveRow(void *a) { persist(); toast(":save: Jogo salvo"); return false; }
static bool savesRow(void *a) { shops_savesMenu(); return false; }
static Row *tile(const char *icon, const char *label, const char *sub, RowFn fn, bool stay) {
    Row *r = menu_row(label, sub, fn, NULL, 0);
    row_icon(r, icon);
    if (stay) row_stay(r);
    return r;
}
void shops_mainMenu(void) {
    J *s = state;
    J *q[32]; int active = activeQuests(s, q, 32);
    char a[80], b[48], c[48], d[48], e[48];
    J *mascot = cJSON_IsNumber(jget(s, "mascot")) ? critter(num(s, "mascot")) : NULL;
    if (mascot) snprintf(a, sizeof a, "Mascot: %s", str(mascot, "name"));
    else snprintf(a, sizeof a, "%d/%d critters", jlen(jget(s, "critters")), jlen(T("CRITTERS")));
    if (active) snprintf(b, sizeof b, "%d quest%s", active, active == 1 ? "" : "s"); else snprintf(b, sizeof b, "Nothing pending");
    int kinds = 0; JFOR(it, jget(s, "inv")) kinds += numv(it) > 0;
    snprintf(c, sizeof c, "%d item type%s", kinds, kinds == 1 ? "" : "s");
    snprintf(d, sizeof d, "%d/%d done", jlen(jget(s, "goals")), jlen(T("GOALS")));
    snprintf(e, sizeof e, "%d/3 active", jlen(jget(jget(s, "contracts"), "active")));
    menu_begin("Menu");
    tile("star", "Album", a, albumRow, true);
    tile("book", "Journal", b, questLogRow, true);
    tile("bag", "Backpack", c, bagRow, true);
    tile("trophy", "Goals", d, goalsRow, true);
    tile("mail", "Contracts", e, contractsRow, true);
    tile("gift", "Friends", "daily score & trades", toFriends, true);
    bool muted = has(s, "muted"), musicOff = has(s, "musicOff");
    tile(muted ? "sound_off" : "sound", "Sound", muted ? "off" : "on", soundRow, true);
    tile(musicOff ? "music_off" : "music", "Music", musicOff ? "off" : "on", musicRow, true);
    tile("save", "Save", "also autosaves", saveRow, false);
    tile("book_green", "Saves", "load, backup", savesRow, true);
    menu_end();
}

static void contractsMenu(void) {
    J *s = state, *active = jget(jget(s, "contracts"), "active");
    menu_begin("Contracts");
    JFOR(c, active) {
        int weeks = num(c, "weeks");
        char label[120], sub[160];
        snprintf(label, sizeof label, "%s: %d× %s", str(c, "client"), (int)num(c, "qty"), itemName(str(c, "item")));
        int n = snprintf(sub, sizeof sub, "%d/%d · pays %d · due day %d", count(s, str(c, "item")), (int)num(c, "qty"), (int)num(c, "reward"), (int)num(c, "due"));
        if (weeks) snprintf(sub + n, sizeof sub - n, " · weekly (%d left)", weeks);
        Row *r = menu_row(label, sub, NULL, NULL, 0);
        iconItem(r, str(c, "item")); row_price(r, num(c, "reward")); row_disabled(r);
    }
    if (!jlen(active)) menu_text("No active contracts. Accept orders at the Notice Board.");
    back("Back", toMain);
    menu_end();
}

// ---------- factory guide ----------
// Shown once when each factory stage unlocks (and when you tap the objective chip): a little scene + rules.
// ponytail: the web's diagram rows are only a fallback for a missing Ponder scene; every guide has one.
static const struct { const char *id, *title, *text[4]; } GUIDES[] = {
    { "machine", "Factory · First machines", {
        ":wrench: Machines come from the Forge and go on the farm with the Build tool (wrench). Tap it again to pick which machine.",
        "The way you face when placing it is the machine's output. You can rotate it later by tapping it.",
        "The Furnace burns 1 coal per bar. Tap it to add ore and coal by hand." } },
    { "line", "Factory · Production line", {
        "The arm takes what is BEHIND it and puts it in FRONT. Place it facing the machine it will feed.",
        "It only grabs what the machine in front accepts, so it never gets stuck holding the wrong thing.",
        "The Conveyor Belt carries items forward (to a Chest, Auto-Sell Box or another belt), but never pushes into machines: that's what the arm is for.",
        "Tap the Chest to store ore and coal in it. The arm takes them out on its own." } },
    { "power", "Factory · Power", {
        ":energy: The Processor and Miner need energy. They get it from a generator up to 3 tiles away.",
        "Solar Panel: 2 machines, daytime only. Steam Boiler: 5 machines, day and night, but burns coal (tap it to add).",
        "Power Poles carry energy further: each reaches 6 tiles from the last one.",
        "A machine without energy flashes a bolt above it." } },
    { "logistics", "Factory · Logistics", {
        "The Splitter sends one item to each side, alternating. If one side is full, it uses the other.",
        "The Underground Belt jumps to the next Underground Belt ahead (up to 5 tiles) facing the same way. Use it to cross another line." } },
};
void shops_factoryGuide(const char *id) {
    for (size_t i = 0; i < sizeof GUIDES / sizeof *GUIDES; i++) {
        if (!eq(GUIDES[i].id, id)) continue;
        char t[96]; snprintf(t, sizeof t, ":wrench: %s", GUIDES[i].title);
        menu_begin(t);
        scene_ponder(id, NULL);
        for (int k = 0; k < 4 && GUIDES[i].text[k]; k++) menu_text("%s", GUIDES[i].text[k]);
        menu_row("Got it", NULL, NULL, NULL, 0);
        menu_end();
        return;
    }
}

// ---------- save slots ----------
static const char *ago(double t) {
    static char b[40];
    if (!t) return "";
    int m = jround((nowMs() - t) / 60000);
    if (m < 1) return "just now";
    if (m < 60) snprintf(b, sizeof b, "%d min ago", m);
    else if (m < 1440) snprintf(b, sizeof b, "%d h ago", (int)jround(m / 60.0));
    else { int d = jround(m / 1440.0); snprintf(b, sizeof b, "%d day%s ago", d, d == 1 ? "" : "s"); }
    return b;
}
static const char *seasonName(int d) {
    static char b[32];
    static const char *S[] = { "Spring", "Summer", "Fall", "Winter" };
    snprintf(b, sizeof b, "%s %d", S[((d - 1) / 14) % 4], (d - 1) % 14 + 1);
    return b;
}
static int curSlot(void) { int n = num(state, "slot"); return n ? n : 1; }
// switch to another save: the current farm is saved first (unless it is being replaced), then the game restarts
// on the other one (the web reloads the page).
static void switchSlot(int n, bool keepCurrent) { if (keepCurrent) persist(); useSlot(n); game_reload(); }

static void slotMenu(int n);
static bool slotRow(void *arg) {
    int n = *(int *)arg;
    J *info = slotInfo(n);
    if (info) { cJSON_Delete(info); slotMenu(n); } else switchSlot(n, true);
    return false;
}
#define SAVE_DIR "sdmc:/3ds/PocketFarm/"
// ponytail: no browser downloads on the 3DS: "Exportar" writes the save as a .json file on the SD card and
// "Importar" reads SAVE_DIR "import.json" (put a save exported from the web build or another 3DS there).
static bool exportRow(void *a) {
    persist();
    char path[96]; snprintf(path, sizeof path, SAVE_DIR "pocketfarm-slot%d-day%d.json", curSlot(), (int)num(state, "day"));
    char *t = cJSON_PrintUnformatted(state);
    bool ok = writeFile(path, t, strlen(t));
    free(t);
    if (ok) toast(":save: Save copied to SD card: %s", path + 5); else toast("Couldn't write to the SD card.");
    return true;
}
static void importMenu(void);
static bool importRow(void *a) { importMenu(); return false; }
static void confirmReset(void);
static bool resetRow(void *a) { confirmReset(); return false; }
void shops_savesMenu(void) {
    J *s = state;
    int cur = curSlot();
    menu_begin(":save: Saves");
    menu_text("The game autosaves every few seconds and when you quit. Each slot is a separate farm.");
    for (int n = 1; n <= 3; n++) {
        J *info = n == cur ? s : slotInfo(n);
        char label[64], sub[160];
        snprintf(label, sizeof label, "Slot %d%s", n, n == cur ? " · playing now" : "");
        if (info) {
            int k = snprintf(sub, sizeof sub, "%s · %s coins · %d critters", seasonName(num(info, "day")), loc(num(info, "money")), jlen(jget(info, "critters")));
            if (n != cur) snprintf(sub + k, sizeof sub - k, " · saved %s", ago(num(info, "savedAt")));
        } else snprintf(sub, sizeof sub, "Empty · tap to start a new farm");
        Row *r = menu_row(label, sub, n == cur ? NULL : slotRow, &n, sizeof n);
        row_icon(r, info ? "save" : "question"); row_stay(r);
        if (n == cur) row_disabled(r);
        if (info != s) cJSON_Delete(info);
    }
    menu_text("Backup: keep the save in a file on the SD card so you don't lose it, or to move it to another device.");
    tile("mail", "Export", "write to SD card", exportRow, false);
    tile("gift", "Import", "load file", importRow, true);
    char sub[32]; snprintf(sub, sizeof sub, "erases slot %d", cur);
    tile("trash", "New game", sub, resetRow, true);
    // ponytail: the web hides "Back" on the title screen; title.c isn't queryable from here
    back("Back", toMain);
    menu_end();
}

static bool loadSlotRow(void *arg) { switchSlot(*(int *)arg, true); return false; }
static bool toSaves(void *a) { shops_savesMenu(); return false; }
static bool deleteSlotRow(void *arg) { deleteSlot(*(int *)arg); shops_savesMenu(); return false; }
static bool askDeleteRow(void *arg) {
    int n = *(int *)arg;
    char t[48]; snprintf(t, sizeof t, "Delete slot %d?", n);
    menu_begin(t);
    menu_text("This farm will be gone forever.");
    row_stay(menu_row("Delete", NULL, deleteSlotRow, &n, sizeof n));
    back("Cancel", toSaves);
    menu_end();
    return false;
}
static void slotMenu(int n) {
    J *info = slotInfo(n);
    char t[32]; snprintf(t, sizeof t, "Slot %d", n);
    menu_begin(t);
    menu_text("%s · %s coins · saved %s. Your current farm stays saved in slot %d.", seasonName(num(info, "day")), loc(num(info, "money")), ago(num(info, "savedAt")), curSlot());
    row_icon(menu_row("Load this farm", NULL, loadSlotRow, &n, sizeof n), "save");
    Row *r = menu_row("Delete this slot", "can't be undone", askDeleteRow, &n, sizeof n);
    row_icon(r, "trash"); row_stay(r);
    back("Back", toSaves);
    menu_end();
    cJSON_Delete(info);
}

// Pick a slot, then the file; the imported farm goes into that slot and starts.
static bool importSlotRow(void *arg) {
    int n = *(int *)arg;
    char *txt = readFile(SAVE_DIR "import.json", NULL);
    if (!txt) { toast("File not found: 3ds/PocketFarm/import.json"); return false; }
    J *s = cJSON_Parse(txt);
    free(txt);
    if (!s) { toast("Invalid file"); return false; }
    if (!cJSON_IsObject(s) || !has(s, "v") || !cJSON_IsNumber(jget(s, "day")) || !has(s, "inv")) { cJSON_Delete(s); toast("File isn't a PocketFarm save."); return false; }
    setnum(s, "slot", n);
    char path[64]; snprintf(path, sizeof path, SAVE_DIR "pocketfarm.slot.%d.json", n);
    char *out = cJSON_PrintUnformatted(s);
    writeFile(path, out, strlen(out));
    free(out); cJSON_Delete(s);
    switchSlot(n, n != curSlot());   // importing over the running farm: don't save it back
    return false;
}
static void importMenu(void) {
    menu_begin("Import save");
    menu_text("Put the save file at 3ds/PocketFarm/import.json on the SD card.");
    menu_text("Pick the slot to receive it (whatever is in it gets replaced).");
    for (int n = 1; n <= 3; n++) {
        J *info = n == curSlot() ? state : slotInfo(n);
        char label[32], sub[64];
        snprintf(label, sizeof label, "Slot %d", n);
        if (info) snprintf(sub, sizeof sub, "%s · will be replaced", seasonName(num(info, "day"))); else snprintf(sub, sizeof sub, "Empty");
        Row *r = menu_row(label, sub, importSlotRow, &n, sizeof n);
        row_icon(r, info ? "save" : "question"); row_stay(r);
        if (info != state) cJSON_Delete(info);
    }
    back("Back", toSaves);
    menu_end();
}

static bool resetGameRow(void *a) { resetSlot(curSlot()); game_reload(); return false; }
static void confirmReset(void) {
    menu_begin("Delete the save?");
    menu_text("This erases the whole farm, critters and progress. Can't be undone.");
    menu_row("Delete and start over", NULL, resetGameRow, NULL, 0);
    back("Cancel", toSaves);
    menu_end();
}

// ---------- villagers ----------
static const char *heartBar(const char *name) {
    static char b[400];
    int h = hearts(state, name), total = fmax(0, fmin(10, fmax(h + 1, 5)) - h);
    b[0] = 0;
    for (int i = 0; i < h; i++) strcat(b, ":heart:");
    for (int i = 0; i < total; i++) strcat(b, ":heart_empty:");
    return b;
}
// "Entregue 5 colheitas à Chloe".toLowerCase(): ASCII + Latin-1 capitals in UTF-8
static void lower(char *s) {
    for (unsigned char *p = (unsigned char *)s; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
        else if (*p == 0xc3 && p[1] >= 0x80 && p[1] <= 0x9e && p[1] != 0x97) { p[1] += 0x20; p++; }
    }
}

typedef struct { J *npc; void (*shop)(void *); const char *action; char id[24]; } NpcArg;
static J *questFor(const char *name) {
    J *q[32]; int n = activeQuests(state, q, 32);
    for (int i = 0; i < n; i++) if (jget(q[i], "deliver") && eq(str(q[i], "from"), name)) return q[i];
    return NULL;
}
static const char *npcLine(J *npc) {
    J *lines = jget(npc, "lines");
    const char *name = str(npc, "name");
    return jat(lines, ((int)num(state, "day") + (int)strlen(name)) % jlen(lines))->valuestring;
}

// Toast the line, then show heart scenes / rewards if a level was reached (once nothing else is open).
static J *heartQueue;   // [{ name, e }]
static void heartPopup(void *a) {
    J *it = cJSON_DetachItemFromArray(heartQueue, 0);
    if (!it) return;
    const char *name = str(it, "name");
    J *e = jget(it, "e"), *r = jget(e, "reward");
    Sb what = { 0 };
    if (str(r, "decor")) { char b[96]; snprintf(b, sizeof b, "furniture: %s", str(jget(T("FURNITURE"), str(r, "decor")), "name")); sbpart(&what, " · ", b); }
    if (jget(r, "items")) {
        Sb it2 = { 0 };
        JFOR(k, jget(r, "items")) { if (it2.n) sb(&it2, ", "); sb(&it2, "%d× %s", (int)numv(k), itemName(k->string)); }
        sbpart(&what, " · ", it2.b);
    }
    if (num(r, "tickets")) { char b[32]; int tk = num(r, "tickets"); snprintf(b, sizeof b, "%d ticket%s", tk, tk == 1 ? "" : "s"); sbpart(&what, " · ", b); }
    if (has(r, "discount")) sbpart(&what, " · ", "10% off in the shop");
    if (has(r, "rod")) sbpart(&what, " · ", "sturdy rod");
    sfx("catch");
    Sb t = { 0 };
    sb(&t, "%s ", name);
    for (int i = 0; i < num(e, "level"); i++) sb(&t, ":heart:");
    menu_begin(t.b);
    if (str(e, "scene")) menu_text("%s", str(e, "scene"));
    if (what.n) menu_text("Gift from %s: %s", name, what.b);
    menu_row(":heart:", NULL, NULL, NULL, 0);
    menu_end();
    cJSON_Delete(it);
}
static void showEvents(const char *name, J *events, const char *line) {
    toast("%s", line);
    if (!heartQueue) heartQueue = cJSON_CreateArray();
    JFOR(e, events) {
        J *it = cJSON_CreateObject();
        setstr(it, "name", name);
        cJSON_AddItemToObject(it, "e", cJSON_Duplicate(e, true));
        cJSON_AddItemToArray(heartQueue, it);
        game_popup(heartPopup, NULL, 0);
    }
}

static void giftMenu(NpcArg a);
static void festivalBooth(void);
static bool talkRow(void *arg) {
    NpcArg *a = arg;
    const char *name = str(a->npc, "name");
    J *r = talkTo(state, name);
    changed();
    char line[400]; snprintf(line, sizeof line, "%s: \"%s\"", name, npcLine(a->npc));
    showEvents(name, jget(r, "events"), line);
    cJSON_Delete(r);
    return true;
}
static bool deliverQuestRow(void *arg) {
    NpcArg *a = arg;
    J *r = deliverQuest(state, str(a->npc, "name"));
    if (!r) return true;
    if (has(r, "missing")) { toast("Still missing some things."); cJSON_Delete(r); return true; }
    changed();
    // the web returns true here, which reopens the NPC menu over the quest-done card; show the card instead
    shops_questDone(r);
    return false;
}
static bool giftMenuRow(void *arg) { giftMenu(*(NpcArg *)arg); return false; }
static bool shopRow(void *arg) { NpcArg *a = arg; a->shop((void *)a->action); return false; }
static bool festivalRow(void *a) { festivalBooth(); return false; }
static void npcRefresh(void *arg) { NpcArg *a = arg; shops_npcMenu(a->npc, a->shop, a->action); }
void shops_npcMenu(J *npc, void (*openShop)(void *), const char *action) {
    J *s = state;
    const char *name = str(npc, "name");
    bool isVillager = jget(T("VILLAGERS"), name) != NULL;
    J *f = friendOf(s, name);
    bool talked = num(f, "talked") == num(s, "day");
    const char *line = npcLine(npc);
    NpcArg a = { npc, openShop, action, "" };
    J *q = questFor(name);
    char t[480]; snprintf(t, sizeof t, "%s %s", name, isVillager ? heartBar(name) : "");
    menu_begin(t);
    if (q) {
        char goal[200]; snprintf(goal, sizeof goal, "%s", str(q, "goal")); lower(goal);
        char say[500]; snprintf(say, sizeof say, "%s Oh, and %s?", line, goal);
        menu_say(NULL, str(npc, "sprite"), say);
    } else menu_say(NULL, str(npc, "sprite"), line);
    Row *r = menu_row("Talk", talked ? "already talked today" : "+friendship", talkRow, &a, sizeof a);
    row_icon(r, "talk"); row_stay(r);
    if (q) {
        char prog[120] = "", sub[300];
        const char *p = questProgress(s, q, prog, sizeof prog);
        snprintf(sub, sizeof sub, "%s · %s", str(q, "goal"), p ? p : "");
        r = menu_row("Deliver", sub, deliverQuestRow, &a, sizeof a);
        row_icon(r, "quest"); row_stay(r);
    }
    if (isVillager) {
        bool gifted = num(f, "gifted") == num(s, "day");
        r = menu_row("Gift", gifted ? "already got one today" : "1 per day", giftMenuRow, &a, sizeof a);
        row_icon(r, "gift"); row_stay(r);
        if (gifted) row_disabled(r);
    }
    if (openShop) { r = menu_row("Shop", "buy and sell", shopRow, &a, sizeof a); row_icon(r, "coins"); row_stay(r); }
    if (eq(name, "Katy") && isFestival(s)) {
        r = menu_row("Festival", str(jget(T("FESTIVALS"), str(season(s), "id")), "name"), festivalRow, NULL, 0);
        row_icon(r, "crown"); row_stay(r);
    }
    menu_refresh(npcRefresh, &a, sizeof a);
    menu_end();
}

static bool giveRow(void *arg) {
    NpcArg *a = arg;
    const char *name = str(a->npc, "name");
    J *r = giveGift(state, name, a->id);
    if (str(r, "error")) { toast("%s", str(r, "error")); cJSON_Delete(r); return false; }
    const char *re = str(r, "reaction");
    const char *say = eq(re, "love") ? "LOVE IT!! How did you know?! :heart::heart:" : eq(re, "like") ? "How nice, thank you!" : eq(re, "neutral") ? "Oh, thanks." : "Hm… you shouldn't have.";
    sfx(eq(re, "hate") ? "miss" : "pop"); changed();
    int gained = num(r, "gained");
    char line[300]; snprintf(line, sizeof line, "%s: \"%s\" (%s%d)", name, say, gained > 0 ? "+" : "", gained);
    showEvents(name, jget(r, "events"), line);
    cJSON_Delete(r);
    return false;
}
static bool backToNpc(void *arg) { NpcArg *a = arg; shops_npcMenu(a->npc, a->shop, a->action); return false; }
static void giftMenu(NpcArg a) {
    J *s = state;
    char t[96];
    snprintf(t, sizeof t, "What to give %s?", str(a.npc, "name"));
    menu_begin("Give gift");
    menu_text("%s", t);
    int n = 0;
    JFOR(e, jget(s, "inv")) {
        const char *id = e->string;
        if (numv(e) <= 0 || !ITEM(id) || starts(id, "machine_")) continue;
        char label[96]; snprintf(label, sizeof label, "%s ×%d", itemName(id), (int)numv(e));
        snprintf(a.id, sizeof a.id, "%s", id);
        Row *r = menu_row(label, NULL, giveRow, &a, sizeof a);
        iconItem(r, id);
        n++;
    }
    if (!n) menu_text("Your backpack is empty.");
    row_stay(menu_row("Back", NULL, backToNpc, &a, sizeof a));
    menu_end();
}

static bool boothBuy(void *arg) {
    const char *id = arg;
    J *s = state, *d = child(s, "decor");
    setnum(s, "money", num(s, "money") - jround(num(jget(T("FURNITURE"), id), "price") * 0.7));
    setnum(d, id, num(d, id) + 1);
    sfx("coin"); changed();
    return true;
}
static void boothRefresh(void *a) { festivalBooth(); }
static void festivalBooth(void) {
    J *s = state, *fest = jget(T("FESTIVALS"), str(season(s), "id"));
    menu_begin("Festival Booth");
    menu_text("%s: seasonal furniture 30%% off!", str(fest, "name"));
    JFOR(e, jget(fest, "decor")) {
        const char *id = e->valuestring;
        int price = jround(num(jget(T("FURNITURE"), id), "price") * 0.7);
        char label[96]; snprintf(label, sizeof label, "%s · %d", str(jget(T("FURNITURE"), id), "name"), price);
        Row *r = menu_row(label, NULL, boothBuy, id, strlen(id) + 1);
        if (num(s, "money") < price) row_disabled(r);
    }
    menu_row("Back", NULL, NULL, NULL, 0);
    menu_refresh(boothRefresh, NULL, 0);
    menu_end();
}

// ---------- quests ----------
// done = { quest, reward: [strings] } (owned)
void shops_questDone(J *done) {
    J *q = jget(done, "quest");
    sfx("tada");   // ponytail: sfx() has no rarity option ({ rarity: 'rare' })
    char t[200]; snprintf(t, sizeof t, ":quest: Quest complete: %s", str(q, "title"));
    menu_begin(t);
    menu_text("%s: \"Thank you so much! Here, this is for you.\"", str(q, "from"));
    Sb r = { 0 };
    JFOR(e, jget(done, "reward")) if (cJSON_IsString(e)) { if (r.n) sb(&r, " · "); sb(&r, "%s", e->valuestring); }
    menu_text("Reward: %s", r.n ? r.b : "eternal gratitude");
    menu_row("Yay!", NULL, NULL, NULL, 0);
    menu_end();
    cJSON_Delete(done);
}

void shops_questLog(void) {
    J *s = state;
    J *q[32]; int n = activeQuests(s, q, 32);
    menu_begin("Journal");
    for (int i = 0; i < n; i++) {
        char label[200], prog[120] = "", sub[600];
        snprintf(label, sizeof label, ":quest: %s — %s", str(q[i], "title"), str(q[i], "from"));
        const char *p = questProgress(s, q[i], prog, sizeof prog);
        snprintf(sub, sizeof sub, "%s%s%s\n%s", str(q[i], "goal"), p && *p ? " · " : "", p && *p ? p : "", str(q[i], "text"));
        row_disabled(menu_row(label, sub, NULL, NULL, 0));
    }
    if (!n) menu_text("No quests right now. The Town Projects (Notice Board) are your big goal!");
    menu_text("Completed: %d/%d", jlen(jget(jget(s, "quests"), "done")), jlen(T("QUESTS")));
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_end();
}

static bool continueRow(void *a) { game_onProject(); return false; }
static void ending(void) {
    J *s = state;
    menu_begin(":crown: Pocket Town is reborn! :crown:");
    menu_text("The fountain flows again. Neighbors fill the square, the market is packed, the lights are on. It all started with a Hoe and some wheat seeds.");
    menu_text("Day %d · %d harvests · %d contracts · %d/20 critters.", (int)num(s, "day"), (int)num(jget(s, "stats"), "harvested"), (int)num(jget(s, "stats"), "contracts"), jlen(jget(s, "critters")));
    menu_text("Thanks for playing PocketFarm! The game goes on: complete the Album, the Mine and your factory.");
    menu_row("Continue", NULL, continueRow, NULL, 0);
    menu_end();
}
static bool projectRow(void *arg) {
    J *p = jat(T("PROJECTS"), *(int *)arg);
    if (!buildProject(state, str(p, "id"))) return true;
    sfx("tada");
    changed();
    // the web returns true after ending(), which reopens the projects list over it; show the ending instead
    if (has(p, "final")) { ending(); return false; }
    toast("%s built! %s", str(p, "name"), str(p, "reward"));
    game_onProject();
    return false;
}
static void projectsRefresh(void *a) { shops_projectsMenu(); }
void shops_projectsMenu(void) {
    J *s = state;
    menu_begin("Town Projects");
    menu_text("Pocket Town was once lively, but it's run down now. Each project you fund changes the town and gives you a perk. Finish them all to rebuild the fountain!");
    int i = 0;
    JFOR(p, T("PROJECTS")) {
        bool built = has(jget(s, "projects"), str(p, "id")), open = projectOpen(s, p);
        Sb parts = { 0 };
        JFOR(k, jget(p, "cost")) {
            int have = haveOf(k->string), n = numv(k);
            if (parts.n) sb(&parts, " · ");
            sb(&parts, "%d/%d %s%s", have < n ? have : n, n, eq(k->string, "money") ? "coins" : itemName(k->string), have >= n ? " :check:" : "");
        }
        char label[120], sub[600];
        snprintf(label, sizeof label, "%s%s", built ? ":check: " : open ? "" : ":lock: ", str(p, "name"));
        if (built) snprintf(sub, sizeof sub, "%s", str(p, "reward"));
        else if (!open) snprintf(sub, sizeof sub, "Finish the other projects first");
        else snprintf(sub, sizeof sub, "%s\n%s", str(p, "reward"), parts.b);
        Row *r = menu_row(label, sub, projectRow, &i, sizeof i);
        if (has(p, "final")) row_stay(r);
        if (built || !open || !canAfford(s, jget(p, "cost"))) row_disabled(r);
        i++;
    }
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(projectsRefresh, NULL, 0);
    menu_end();
}

// log: [strings] (owned); goals: GOALS rows
void shops_morningReport(J *log, J **goals, int ngoals) {
    char t[48]; snprintf(t, sizeof t, "Good morning! Day %d", (int)num(state, "day"));
    menu_begin(t);
    int n = 0;
    JFOR(e, log) if (cJSON_IsString(e)) { menu_text("%s", e->valuestring); n++; }
    for (int i = 0; i < ngoals; i++) { menu_text("Goal complete: %s! +%d capsule ticket%s.", str(goals[i], "name"), (int)num(goals[i], "tickets"), num(goals[i], "tickets") == 1 ? "" : "s"); n++; }
    if (!n) menu_text("A quiet day on the farm.");
    menu_row("Let's go!", NULL, NULL, NULL, 0);
    menu_end();
    cJSON_Delete(log);
}

// ---------- machines (main.js machineMenu) ----------
static const char *NO_DIR[] = { "chest", "autosell", "sprinkler", "solar", "pole", "steam" };
static bool noDir(const char *t) { for (int i = 0; i < 6; i++) if (eq(t, NO_DIR[i])) return true; return false; }
static const char *dirName(const char *d) { return eq(d, "up") ? "up" : eq(d, "down") ? "down" : eq(d, "left") ? "left" : "right"; }
typedef struct { J *m; char id[24]; } MArg;
static bool coalRow(void *arg) {
    J *m = ((MArg *)arg)->m;
    while (count(state, "coal") && accepts(m, "coal")) { take(state, "coal", 1); insert(state, m, "coal"); }
    changed();
    return true;
}
static bool takeItemRow(void *arg) { J *m = ((MArg *)arg)->m; give(state, str(m, "item"), 1); setstr(m, "item", NULL); changed(); return true; }
static bool feedRow(void *arg) {
    MArg *a = arg;
    int n = 0;
    while (count(state, a->id) && accepts(a->m, a->id)) { take(state, a->id, 1); insert(state, a->m, a->id); n++; }
    toast("Added %d", n); changed();
    return true;
}
static bool storeRow(void *arg) {
    MArg *a = arg;
    int n = count(state, a->id);
    take(state, a->id, n);
    J *out = childarr(a->m, "out");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(out, cJSON_CreateString(a->id));
    sfx("pop"); changed();
    return true;
}
static bool takeOutRow(void *arg) {
    J *m = ((MArg *)arg)->m;
    JFOR(e, jget(m, "out")) give(state, e->valuestring, 1);
    cJSON_ReplaceItemInObjectCaseSensitive(m, "out", cJSON_CreateArray());
    sfx("pop"); changed();
    return true;
}
static bool rotateRow(void *arg) { J *m = ((MArg *)arg)->m; setstr(m, "dir", DIR_NAME[ROTATE[dirFrom(str(m, "dir"))]]); changed(); return true; }
static bool pickUpRow(void *arg) { pickUp(state, ((MArg *)arg)->m); refreshTools(); return false; }
static bool copyLineRow(void *arg) { J *bp = copyLine(((MArg *)arg)->m); shops_blueprintMenu(bp, false); cJSON_Delete(bp); return false; }
static void machineRefresh(void *arg) { machineMenu(((MArg *)arg)->m); }
static bool usedByMachines(const char *id) {
    JFOR(d, T("MACHINES")) { if (jget(jget(d, "recipes"), id) || eq(str(d, "fuel"), id)) return true; }
    return false;
}
void machineMenu(J *m) {
    J *def = mdef(m);
    const char *type = str(m, "type");
    MArg a = { m, "" };
    menu_begin(str(def, "name"));
    if (noDir(type)) menu_text("%s", str(def, "desc"));
    else menu_text("%s · %s: %s", str(def, "desc"), eq(type, "arm") ? "places" : "output", dirName(str(m, "dir")));
    if (has(def, "power")) menu_text("%s", has(m, "unpowered") ? ":energy: No energy! Put a Solar Panel or Steam Boiler nearby (or Power Poles up to it)." : ":energy: Powered");
    if (num(def, "generator")) {
        if (eq(type, "solar")) menu_text(":energy: Powers up to %d nearby machines (daytime only)", (int)num(def, "generator"));
        else menu_text(":energy: Powers up to %d nearby machines · coal: %d", (int)num(def, "generator"), (int)num(m, "fuel"));
    }
    Row *r;
    char label[120], sub[120];
    if (eq(type, "steam") && count(state, "coal")) {
        snprintf(sub, sizeof sub, "you have %d", count(state, "coal"));
        r = menu_row("Add coal", sub, coalRow, &a, sizeof a); row_icon(r, "item:coal");
    }
    if ((eq(type, "arm") || eq(type, "splitter") || eq(type, "tunnel")) && str(m, "item")) {
        snprintf(label, sizeof label, "Take %s", itemName(str(m, "item")));
        menu_row(label, NULL, takeItemRow, &a, sizeof a);
    }
    J *recipes = jget(def, "recipes");
    if (recipes) {
        Sb q = { 0 };
        JFOR(k, jget(m, "input")) { if (q.n) sb(&q, ", "); sb(&q, "%d %s", (int)numv(k), itemName(k->string)); }
        Sb t = { 0 };
        sb(&t, "Queued: %s", q.n ? q.b : "nothing");
        if (str(def, "fuel")) sb(&t, " · coal: %d", (int)num(m, "fuel"));
        if (str(m, "cur")) sb(&t, " · making %s", itemName(jat(jget(recipes, str(m, "cur")), 0)->valuestring));
        menu_text("%s", t.b);
        const char *ids[16]; int n = 0;
        JFOR(k, recipes) if (n < 15) ids[n++] = k->string;
        if (str(def, "fuel")) ids[n++] = str(def, "fuel");
        for (int i = 0; i < n; i++) {
            if (!count(state, ids[i])) continue;
            snprintf(a.id, sizeof a.id, "%s", ids[i]);
            snprintf(label, sizeof label, "Add %s", itemName(ids[i]));
            snprintf(sub, sizeof sub, "you have %d", count(state, ids[i]));
            r = menu_row(label, sub, feedRow, &a, sizeof a); iconItem(r, ids[i]);
        }
    }
    // chests are where a line starts: put things in by hand, machine inputs (ore, coal, eggs…) first
    if (eq(type, "chest")) {
        const char *ids[128]; int n = 0;
        for (int pass = 0; pass < 2; pass++)
            JFOR(e, jget(state, "inv")) {
                const char *id = e->string;
                if (numv(e) > 0 && ITEM(id) && !starts(id, "seed_") && !starts(id, "machine_") && usedByMachines(id) == !pass && n < 128) ids[n++] = id;
            }
        if (n) menu_text("Store in Chest (%d inside):", jlen(jget(m, "out")));
        for (int i = 0; i < n; i++) {
            snprintf(a.id, sizeof a.id, "%s", ids[i]);
            snprintf(label, sizeof label, "Store %s", itemName(ids[i]));
            snprintf(sub, sizeof sub, "all %d%s", count(state, ids[i]), usedByMachines(ids[i]) ? " · used by machines" : "");
            r = menu_row(label, sub, storeRow, &a, sizeof a); iconItem(r, ids[i]);
        }
    }
    int nout = jlen(jget(m, "out"));
    if (nout) { snprintf(label, sizeof label, "Take %d %s", nout, eq(type, "chest") ? "items" : "products"); menu_row(label, NULL, takeOutRow, &a, sizeof a); }
    if (eq(type, "belt") && str(m, "item")) { snprintf(label, sizeof label, "Take %s", itemName(str(m, "item"))); menu_row(label, NULL, takeItemRow, &a, sizeof a); }
    if (!noDir(type)) { snprintf(label, sizeof label, "Rotate (now: %s)", dirName(str(m, "dir"))); menu_row(label, NULL, rotateRow, &a, sizeof a); }
    menu_row("Put in Backpack", "Contents come along", pickUpRow, &a, sizeof a);
    r = menu_row("Copy line", "makes a blueprint: build it elsewhere or send it to a friend", copyLineRow, &a, sizeof a);
    row_icon(r, "book"); row_stay(r);
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_refresh(machineRefresh, &a, sizeof a);
    menu_end();
}
