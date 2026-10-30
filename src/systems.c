// Game rules that run on menus and at night (systems.js). Results that the JS returns as objects come back as
// cJSON trees (the caller frees them), so the menus read them the same way.
#include <stdarg.h>
#include <stdio.h>
#include "game.h"

// Small seeded RNG so a day's contracts / mine layout are stable across reloads.
Rng rng(double seed) { uint32_t s = (uint32_t)(int64_t)seed; return (Rng){ s ? s : 1 }; }
double rngNext(Rng *r) { r->s = r->s * 1664525u + 1013904223u; return r->s / 4294967296.0; }
static double R(Rng *r) { return r ? rngNext(r) : frand(); }
int randInt(Rng *r, int a, int b) { return a + (int)floor(R(r) * (b - a + 1)); }

J *rollDrops(J *drops, Rng *r) {
    J *out = cJSON_CreateObject();
    JFOR(d, drops) {
        const char *id = jat(d, 0)->valuestring;
        int n = randInt(r, numv(jat(d, 1)), numv(jat(d, 2)));
        if (n > 0) setnum(out, id, num(out, id) + n);
    }
    return out;
}

// ---------- calendar ----------
int seasonIndex(J *s) { return (int)floor((num(s, "day") - 1) / num(DATA, "SEASON_DAYS")) % 4; }
J *season(J *s) { return jat(table("SEASONS"), seasonIndex(s)); }
const char *seasonId(J *s) { return str(season(s), "id"); }
int dayOfSeason(J *s) { return (int)(num(s, "day") - 1) % (int)num(DATA, "SEASON_DAYS") + 1; }
int year(J *s) { return (int)floor((num(s, "day") - 1) / (num(DATA, "SEASON_DAYS") * 4)) + 1; }
bool isFestival(J *s) { return dayOfSeason(s) == 13; }
bool listHas(J *list, const char *v) { JFOR(e, list) if (cJSON_IsString(e) && !strcmp(e->valuestring, v)) return true; return false; }
bool inSeason(J *s, const char *crop) { return listHas(jget(jget(table("CROPS"), crop), "seasons"), seasonId(s)); }
// Is this item obtainable right now? (crops/fish only in their season)
bool available(J *s, const char *id) {
    if (jget(table("CROPS"), id)) return inSeason(s, id);
    J *it = jget(table("ITEMS"), id);
    return !jget(it, "seasons") || listHas(jget(it, "seasons"), seasonId(s));
}

// ---------- villagers ----------
J *friendOf(J *s, const char *name) {
    J *fr = child(s, "friends"), *f = jget(fr, name);
    if (!f) { f = cJSON_Parse("{\"pts\":0,\"talked\":0,\"gifted\":0,\"claimed\":[]}"); cJSON_AddItemToObject(fr, name, f); }
    return f;
}
int hearts(J *s, const char *name) { return fmin(10, floor(num(friendOf(s, name), "pts") / 100)); }
int maxHearts(J *s) { int m = 0; JFOR(v, table("VILLAGERS")) { int h = hearts(s, v->string); if (h > m) m = h; } return m; }

static int cmpInt(const void *a, const void *b) { return *(int *)a - *(int *)b; }
// Apply heart-level rewards/scenes not yet claimed. Returns [{ level, scene, reward }].
static J *milestones(J *s, const char *name) {
    J *v = jget(table("VILLAGERS"), name), *f = friendOf(s, name), *out = cJSON_CreateArray();
    int h = hearts(s, name), lv[32], n = 0;
    const char *srcs[2] = { "rewards", "scenes" };
    for (int k = 0; k < 2; k++) JFOR(e, jget(v, srcs[k])) {
        int l = atoi(e->string), dup = 0;
        for (int i = 0; i < n; i++) dup |= lv[i] == l;
        if (!dup && n < 32) lv[n++] = l;
    }
    qsort(lv, n, sizeof(int), cmpInt);
    J *claimed = childarr(f, "claimed");
    for (int i = 0; i < n; i++) {
        int lvl = lv[i];
        bool got = false;
        JFOR(c, claimed) got |= numv(c) == lvl;
        if (lvl > h || got) continue;
        cJSON_AddItemToArray(claimed, cJSON_CreateNumber(lvl));
        char key[8]; snprintf(key, sizeof key, "%d", lvl);
        J *r = jget(jget(v, "rewards"), key);
        JFOR(it, jget(r, "items")) give(s, it->string, numv(it));
        if (str(r, "decor")) setnum(child(s, "decor"), str(r, "decor"), num(jget(s, "decor"), str(r, "decor")) + 1);
        if (num(r, "tickets")) setnum(s, "tickets", num(s, "tickets") + num(r, "tickets"));
        if (str(r, "discount")) setbool(child(s, "discounts"), str(r, "discount"), true);
        if (has(r, "rod")) setnum(child(s, "tools"), "rod", fmax(num(jget(s, "tools"), "rod"), 2));
        J *ev = cJSON_CreateObject();
        setnum(ev, "level", lvl);
        J *scene = jget(jget(v, "scenes"), key);
        if (scene) cJSON_AddItemToObject(ev, "scene", cJSON_Duplicate(scene, true));
        if (r) cJSON_AddItemToObject(ev, "reward", cJSON_Duplicate(r, true));
        cJSON_AddItemToArray(out, ev);
    }
    return out;
}

// { gained, events }
J *talkTo(J *s, const char *name) {
    J *res = cJSON_CreateObject(), *f = friendOf(s, name);
    if (num(f, "talked") == num(s, "day") || !jget(table("VILLAGERS"), name)) { setnum(res, "gained", 0); cJSON_AddItemToObject(res, "events", cJSON_CreateArray()); return res; }
    setnum(f, "talked", num(s, "day"));
    int gained = isFestival(s) ? 30 : 15;
    setnum(f, "pts", num(f, "pts") + gained);
    setnum(res, "gained", gained);
    cJSON_AddItemToObject(res, "events", milestones(s, name));
    return res;
}

// { reaction: love|like|neutral|hate, gained, events } or { error }
J *giveGift(J *s, const char *name, const char *item) {
    J *res = cJSON_CreateObject(), *v = jget(table("VILLAGERS"), name), *f = friendOf(s, name);
    char err[96];
    if (!v) { setstr(res, "error", "Can't give that as a gift."); return res; }
    if (num(f, "gifted") == num(s, "day")) { snprintf(err, sizeof err, "%s already got a gift today.", name); setstr(res, "error", err); return res; }
    if (!take(s, item, 1)) { setstr(res, "error", "You don't have that."); return res; }
    setnum(f, "gifted", num(s, "day"));
    const char *reaction = listHas(jget(v, "loves"), item) ? "love" : listHas(jget(v, "likes"), item) ? "like" : listHas(jget(v, "hates"), item) ? "hate" : "neutral";
    int base = !strcmp(reaction, "love") ? 80 : !strcmp(reaction, "like") ? 40 : !strcmp(reaction, "neutral") ? 20 : -30;
    int gained = base * (isFestival(s) ? 2 : 1);
    setnum(f, "pts", fmax(0, num(f, "pts") + gained));
    setstr(res, "reaction", reaction);
    setnum(res, "gained", gained);
    cJSON_AddItemToObject(res, "events", milestones(s, name));
    return res;
}

bool hasDiscount(J *s, const char *shop) { return has(jget(s, "discounts"), shop); }

// ---------- contracts ----------
J *contractOffers(J *s) {
    J *ct = child(s, "contracts");
    if (num(ct, "day") != num(s, "day")) {
        Rng r = rng(num(s, "day") * 7919 + 13);
        double repTotal = num(jget(s, "rep"), "local") + num(jget(s, "rep"), "business");
        bool blocked = has(jget(s, "tax"), "late");
        J *pool[64]; int np = 0;
        JFOR(t, table("CONTRACT_TEMPLATES")) if (num(t, "minRep") <= repTotal && !(blocked && !strcmp(str(t, "kind"), "business")) && np < 64) pool[np++] = t;
        J *offers = cJSON_CreateArray();
        cJSON_ReplaceItemInObjectCaseSensitive(ct, "offers", offers);
        for (int i = 0; i < 3 && np; i++) {
            int pick = floor(rngNext(&r) * np);
            J *t = pool[pick];
            memmove(pool + pick, pool + pick + 1, (np - pick - 1) * sizeof(J *)); np--;
            const char *items[32]; int ni = 0;
            JFOR(id, jget(t, "items")) if (available(s, id->valuestring) && ni < 32) items[ni++] = id->valuestring;
            if (!ni) { i--; continue; }
            const char *item = items[(int)floor(rngNext(&r) * ni)];
            int qty = randInt(&r, numv(jat(jget(t, "qty"), 0)), numv(jat(jget(t, "qty"), 1)));
            bool business = !strcmp(str(t, "kind"), "business");
            // businesses sometimes want a weekly supply: 4 deliveries, one every 7 days, +50% bonus on the last
            bool weekly = business && rngNext(&r) < 0.35;
            J *o = cJSON_CreateObject();
            setnum(o, "id", num(s, "day") * 10 + i);
            setstr(o, "client", str(t, "client")); setstr(o, "kind", str(t, "kind")); setstr(o, "item", item);
            setnum(o, "qty", qty);
            setnum(o, "due", num(s, "day") + (weekly ? 7 : num(t, "days")));
            double sell = num(jget(table("ITEMS"), item), "sell");
            setnum(o, "reward", jround(sell * qty * num(t, "mult") * (weekly ? 0.9 : 1) * (business && has(jget(s, "projects"), "lamps") ? 1.1 : 1) / 5) * 5);
            setnum(o, "rep", num(t, "rep")); setnum(o, "fine", num(t, "fine"));
            setbool(o, "ticket", rngNext(&r) < 0.2);
            if (weekly) setnum(o, "weeks", 4);
            cJSON_AddItemToArray(offers, o);
        }
        setnum(ct, "day", num(s, "day"));
    }
    return jget(ct, "offers");
}

const char *acceptContract(J *s, double id) {
    J *ct = jget(s, "contracts"), *active = jget(ct, "active"), *offers = jget(ct, "offers");
    if (jlen(active) >= 3) return "You already have 3 contracts.";
    int i = 0;
    JFOR(o, offers) { if (num(o, "id") == id) { cJSON_AddItemToArray(active, cJSON_DetachItemFromArray(offers, i)); return NULL; } i++; }
    return "That contract no longer exists.";
}

// A copy of the contract with { reward: paid, last }, or NULL.
J *deliverContract(J *s, double id) {
    J *active = jget(jget(s, "contracts"), "active"), *c = NULL;
    int idx = 0;
    JFOR(a, active) { if (num(a, "id") == id) { c = a; break; } idx++; }
    if (!c || !take(s, str(c, "item"), num(c, "qty"))) return NULL;
    bool last = !num(c, "weeks") || num(c, "weeks") <= 1;
    int paid = last && num(c, "weeks") ? jround(num(c, "reward") * 1.5) : num(c, "reward");
    earn(s, paid);
    J *rep = child(s, "rep");
    setnum(rep, str(c, "kind"), num(rep, str(c, "kind")) + num(c, "rep"));
    if (has(c, "ticket") && last) setnum(s, "tickets", num(s, "tickets") + 1);
    J *st = child(s, "stats");
    setnum(st, "contracts", num(st, "contracts") + 1);
    J *out = cJSON_Duplicate(c, true);
    setnum(out, "reward", paid); setbool(out, "last", last);
    if (last) cJSON_DeleteItemFromArray(active, idx);
    else { setnum(c, "weeks", num(c, "weeks") - 1); setnum(c, "due", num(c, "due") + 7); }   // weekly: next delivery in a week
    return out;
}

// ---------- taxes ----------
int taxAmount(J *s) {
    int week = floor(num(s, "day") / 7);
    return week <= 1 ? 50 : week == 2 ? 80 : jround(80 + num(s, "money") * 0.05 + jlen(jget(s, "machines")) * 10);
}

bool payTax(J *s) {
    J *tax = jget(s, "tax");
    if (!num(tax, "due") || num(s, "money") < num(tax, "amount")) return false;
    setnum(s, "money", num(s, "money") - num(tax, "amount"));
    cJSON_ReplaceItemInObjectCaseSensitive(s, "tax", cJSON_Parse("{\"amount\":0,\"due\":0}"));
    setnum(child(s, "rep"), "local", num(jget(s, "rep"), "local") + 1);
    return true;
}

// ---------- animals ----------
int animalsIn(J *s, const char *home) {
    int n = 0;
    JFOR(a, jget(s, "animals")) n += !strcmp(str(jget(table("ANIMALS"), str(a, "kind")), "home"), home);
    return n;
}

const char *buyAnimal(J *s, const char *kind, const char *name, double nowMs) {
    J *def = jget(table("ANIMALS"), kind);
    const char *home = str(def, "home");
    if (!has(jget(s, "buildings"), home)) return !strcmp(home, "coop") ? "You need to build the Coop." : "You need to build the Barn.";
    if (animalsIn(s, home) >= num(DATA, "MAX_PER_HOME")) return "No room for more.";
    if (num(s, "money") < num(def, "price")) return "Not enough coins.";
    setnum(s, "money", num(s, "money") - num(def, "price"));
    J *a = cJSON_CreateObject(), *animals = childarr(s, "animals");
    setnum(a, "id", fmod(nowMs, 1e9) + jlen(animals));
    setstr(a, "kind", kind); setstr(a, "name", name);
    setnum(a, "happy", 50); setbool(a, "fed", false); setbool(a, "product", false); setnum(a, "age", 0); setnum(a, "petDay", 0);
    cJSON_AddItemToArray(animals, a);
    return NULL;
}

// Pet or collect. Returns a short message.
const char *touchAnimal(J *s, J *a) {
    static char msg[96];
    if (has(a, "product")) {
        const char *item = str(jget(table("ANIMALS"), str(a, "kind")), "product");
        bool big = num(a, "happy") >= 80 && frand() < 0.3;
        give(s, item, big ? 2 : 1);
        setbool(a, "product", false);
        snprintf(msg, sizeof msg, "+%d %s", big ? 2 : 1, str(jget(table("ITEMS"), item), "name"));
        return msg;
    }
    if (num(a, "petDay") == num(s, "day")) { snprintf(msg, sizeof msg, "%s is happy (%d%%)", str(a, "name"), (int)num(a, "happy")); return msg; }
    setnum(a, "petDay", num(s, "day"));
    setnum(a, "happy", fmin(100, num(a, "happy") + (hasBonus(s, "animal") ? 12 : 8)));
    snprintf(msg, sizeof msg, "You petted %s :heart:", str(a, "name"));
    return msg;
}

static void logf_(J *log, const char *fmt, ...) {
    char buf[256]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    cJSON_AddItemToArray(log, cJSON_CreateString(buf));
}

static void nightAnimals(J *s, J *log) {
    JFOR(a, jget(s, "animals")) {
        J *def = jget(table("ANIMALS"), str(a, "kind"));
        setnum(a, "age", num(a, "age") + 1);
        if (take(s, "feed", 1)) {
            setnum(a, "happy", fmin(100, num(a, "happy") + 2));
            if ((int)num(a, "age") % (int)num(def, "every") == 0) setbool(a, "product", true);
        } else {
            setnum(a, "happy", fmax(0, num(a, "happy") - 10));
            logf_(log, "%s went without feed.", str(a, "name"));
        }
        if (num(a, "petDay") != num(s, "day")) setnum(a, "happy", fmax(0, num(a, "happy") - 3));
        if (has(a, "product") && has(jget(s, "upgrades"), "collector")) { give(s, str(def, "product"), 1); setbool(a, "product", false); }
    }
}

// ---------- fishing ----------
Fish rollFish(J *s, Rng *r, bool river) {
    int lvl = num(jget(s, "tools"), "rod") ? num(jget(s, "tools"), "rod") : 1;
    double total = 0, w[8]; const char *names[8]; int n = 0;
    JFOR(e, table("FISH_RARITY")) {
        names[n] = e->string;
        w[n] = !strcmp(e->string, "common") ? num(e, "weight") : num(e, "weight") * (lvl >= 2 ? 1.6 : 1) * (river ? 1.5 : 1);
        total += w[n++];
    }
    double x = R(r) * total;
    const char *rarity = "common";
    for (int i = 0; i < n; i++) if ((x -= w[i]) < 0) { rarity = names[i]; break; }
    if (!strcmp(rarity, "legendary") && lvl < 2) rarity = "rare";   // the Dourado needs a good rod
    const char *pool[32]; int np = 0;
    JFOR(it, table("ITEMS")) if (str(it, "fish") && !strcmp(str(it, "fish"), rarity) && available(s, it->string) && np < 32) pool[np++] = it->string;
    if (!np) JFOR(it, table("ITEMS")) if (str(it, "fish") && !strcmp(str(it, "fish"), "common") && available(s, it->string) && np < 32) pool[np++] = it->string;
    Fish f = { .rarity = rarity };
    snprintf(f.id, sizeof f.id, "%s", pool[(int)floor(R(r) * np)]);
    f.window = num(jget(table("FISH_RARITY"), rarity), "window") * (hasBonus(s, "fish") ? 1.35 : 1) * (lvl >= 2 ? 1.15 : 1);
    return f;
}

// ---------- village projects ----------
bool projectOpen(J *s, J *p) {
    if (!has(p, "final")) return true;
    JFOR(x, table("PROJECTS")) if (!has(x, "final") && !has(jget(s, "projects"), str(x, "id"))) return false;
    return true;
}

bool buildProject(J *s, const char *id) {
    J *p = NULL;
    JFOR(x, table("PROJECTS")) if (!strcmp(str(x, "id"), id)) p = x;
    if (!p || has(jget(s, "projects"), id) || !projectOpen(s, p)) return false;
    if (!pay(s, jget(p, "cost"))) return false;
    setbool(child(s, "projects"), id, true);
    return true;
}

// What to aim for next (shown in the objective chip once the tutorial is over). label gets "Name · done/parts".
J *nextGoal(J *s, char *label, int n) {
    JFOR(p, table("PROJECTS")) {
        if (has(jget(s, "projects"), str(p, "id")) || !projectOpen(s, p)) continue;
        int parts = 0, done = 0;
        JFOR(k, jget(p, "cost")) { parts++; done += (!strcmp(k->string, "money") ? num(s, "money") : count(s, k->string)) >= numv(k); }
        if (label) snprintf(label, n, "%s · %d/%d", str(p, "name"), done, parts);
        return p;
    }
    return NULL;
}

// ---------- goals ----------
static bool goalCheck(J *s, const char *id) {
    J *st = jget(s, "stats");
    if (!strcmp(id, "money1")) return num(st, "earned") >= 1000;
    if (!strcmp(id, "harvest")) return num(st, "harvested") >= 50;
    if (!strcmp(id, "clear")) return num(st, "cleared") >= 25;
    if (!strcmp(id, "coop")) return has(jget(s, "buildings"), "coop");
    if (!strcmp(id, "barn")) return has(jget(s, "buildings"), "barn");
    if (!strcmp(id, "contract")) return num(st, "contracts") >= 10;
    if (!strcmp(id, "fish")) return num(st, "fished") >= 20;
    if (!strcmp(id, "mine")) return num(s, "mineDeepest") >= 8;
    if (!strcmp(id, "factory")) { int n = 0; JFOR(m, jget(s, "machines")) n += strcmp(str(m, "type"), "belt") != 0; return n >= 5; }
    if (!strcmp(id, "rep")) return num(jget(s, "rep"), "local") + num(jget(s, "rep"), "business") >= 50;
    if (!strcmp(id, "money2")) return num(st, "earned") >= 20000;
    if (!strcmp(id, "album")) return albumDone(s);
    if (!strcmp(id, "seasons")) {
        JFOR(c, table("CRITTERS")) if (has(c, "set")) { char k[16]; snprintf(k, sizeof k, "%d", (int)num(c, "id")); if (!has(jget(s, "critters"), k)) return false; }
        return true;
    }
    return false;
}

// Returns an array of the goal definitions just reached (references into the table: don't free the items).
int checkGoals(J *s, J *done[], int max) {
    int n = 0;
    JFOR(g, table("GOALS")) {
        const char *id = str(g, "id");
        if (!has(jget(s, "goals"), id) && goalCheck(s, id)) {
            setbool(child(s, "goals"), id, true);
            setnum(s, "tickets", num(s, "tickets") + num(g, "tickets"));
            if (n < max) done[n++] = g;
        }
    }
    return n;
}

// ---------- night ----------
// Everything that happens when the player sleeps. Returns lines for the morning report.
J *newDay(J *s) {
    J *log = cJSON_CreateArray();
    sleepCrops(s);
    if (has(jget(s, "projects"), "greenhouse")) JFOR(t, jget(s, "soil")) setbool(t, "wet", true);
    nightAnimals(s, log);
    int sold = 0, items = 0;
    nightFactory(s, &sold, &items);
    if (sold) { earn(s, sold); logf_(log, "Auto-Sell Box sold %d coins' worth.", sold); }
    if (items) logf_(log, "The machines worked all night (%d items).", items);

    // contracts past their deadline
    J *active = jget(jget(s, "contracts"), "active");
    J *expired = cJSON_CreateArray();
    for (int i = 0; i < jlen(active);) {
        J *c = jat(active, i);
        if (num(s, "day") > num(c, "due")) cJSON_AddItemToArray(expired, cJSON_DetachItemFromArray(active, i));
        else i++;
    }
    JFOR(c, expired) {
        setnum(s, "money", fmax(0, num(s, "money") - num(c, "fine")));
        J *rep = child(s, "rep");
        setnum(rep, str(c, "kind"), fmax(0, num(rep, str(c, "kind")) - ceil(num(c, "rep") / 2) - 1));
        logf_(log, "%s contract expired. Fine: %d.", str(c, "client"), (int)num(c, "fine"));
    }
    cJSON_Delete(expired);
    // taxes: billed every 7 days, 3 days to pay; late = one-time fine and rep loss
    J *tax = child(s, "tax");
    if (num(tax, "due") && !has(tax, "late") && num(s, "day") >= num(tax, "due")) {
        setbool(tax, "late", true);
        setnum(tax, "amount", jround(num(tax, "amount") * 1.25));
        J *rep = child(s, "rep");
        setnum(rep, "local", fmax(0, num(rep, "local") - 5));
        setnum(rep, "business", fmax(0, num(rep, "business") - 5));
        logf_(log, "Tax overdue! Now %d coins. Business contracts locked until paid.", (int)num(tax, "amount"));
    }
    const char *before = seasonId(s);
    setnum(s, "day", num(s, "day") + 1);
    setnum(s, "time", num(DATA, "DAY_START"));
    if (strcmp(seasonId(s), before)) {
        int dead = 0;
        JFOR(t, jget(s, "soil")) if (str(t, "crop") && !inSeason(s, str(t, "crop"))) { delkey(t, "crop"); delkey(t, "age"); dead++; }
        char extra[64] = "";
        if (dead) snprintf(extra, sizeof extra, " %d out-of-season crop(s) withered.", dead);
        logf_(log, "%s has begun!%s", str(season(s), "name"), extra);
    }
    if (isFestival(s)) logf_(log, "Festival today in the town square!");
    if ((int)num(s, "day") % 7 == 0 && !num(jget(s, "tax"), "due")) {
        J *t = cJSON_CreateObject();
        setnum(t, "amount", taxAmount(s)); setnum(t, "due", num(s, "day") + 3);
        cJSON_ReplaceItemInObjectCaseSensitive(s, "tax", t);
        logf_(log, "Weekly tax: %d coins. Pay at the Notice Board by day %d.", (int)num(t, "amount"), (int)num(t, "due"));
    }
    setnum(s, "energy", maxEnergy(s));
    setnum(s, "mineLevel", 1);
    if (has(jget(s, "projects"), "fair") && (int)num(s, "day") % 7 == 0) logf_(log, "Market day today! Sales +25%%.");
    return log;
}
