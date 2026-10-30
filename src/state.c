// Game state + save/load (state.js). The state is the same JSON tree as the web build, so saves move between them.
// localStorage keys become files on the SD card: sdmc:/3ds/PocketFarm/<key>.json
#include <stdio.h>
#include "game.h"

#define VERSION 3

J *newState(void) {
    J *s = cJSON_Parse("{\"v\":3,\"slot\":1,\"day\":1,\"time\":0,\"money\":200,\"energy\":0,\"map\":\"farm\",\"px\":null,\"py\":null,"
        "\"tool\":\"hoe\",\"seed\":\"carrot\",\"build\":null,\"tools\":{\"hoe\":1,\"can\":1,\"axe\":1,\"pickaxe\":1,\"rod\":0},"
        "\"inv\":{\"seed_carrot\":6,\"seed_radish\":3},\"soil\":{},\"removed\":{},\"stumps\":{},\"forage\":[],\"forageDay\":0,"
        "\"flags\":{},\"tutorialDone\":false,\"house\":{\"level\":1,\"placed\":[{\"uid\":1,\"item\":\"bed_orange\",\"x\":2,\"y\":4},{\"uid\":2,\"item\":\"table\",\"x\":6,\"y\":5}]},"
        "\"decor\":{},\"projects\":{},\"friends\":{},\"discounts\":{},\"quests\":{\"done\":{}},\"critters\":{},\"tokens\":0,\"tickets\":0,"
        "\"lastCapsuleDay\":0,\"mascot\":null,\"buildings\":{},\"upgrades\":{},\"animals\":[],\"machines\":[],\"rep\":{\"local\":0,\"business\":0},"
        "\"contracts\":{\"day\":0,\"offers\":[],\"active\":[]},\"tax\":{\"amount\":0,\"due\":0},\"mineLevel\":1,\"mineDeepest\":1,"
        "\"stats\":{\"earned\":0,\"harvested\":0,\"cleared\":0,\"contracts\":0,\"fished\":0,\"smelted\":0,\"foraged\":0},"
        "\"goals\":{},\"freeWaterDay\":0,\"trades\":{\"pending\":{},\"used\":[]},\"muted\":false,\"farmName\":\"\",\"workshop\":{},"
        "\"daily\":null,\"friendsList\":{},\"blueprints\":[],\"blueprint\":null,\"giftDay\":\"\"}");
    setnum(s, "time", num(DATA, "DAY_START"));
    setnum(s, "energy", num(DATA, "BASE_ENERGY"));
    cJSON_ReplaceItemInObjectCaseSensitive(s, "decor", cJSON_Duplicate(table("STARTER_DECOR"), true));
    return s;
}

static char *keyPath(const char *key) {
    static char p[96];
    snprintf(p, sizeof p, "sdmc:/3ds/PocketFarm/%s.json", key);
    return p;
}
static J *readKey(const char *key) { char *t = readFile(keyPath(key), NULL); J *j = t ? cJSON_Parse(t) : NULL; free(t); return j; }
static void writeKey(const char *key, J *j) { char *t = cJSON_PrintUnformatted(j); writeFile(keyPath(key), t, strlen(t)); free(t); }

// {...a, ...b}: b's keys win (shallow)
static J *merge(J *a, J *b) {
    JFOR(it, b) {
        J *dup = cJSON_Duplicate(it, true);
        if (jget(a, it->string)) cJSON_ReplaceItemInObjectCaseSensitive(a, it->string, dup);
        else cJSON_AddItemToObject(a, it->string, dup);
    }
    return a;
}

J *loadState(void) {
    J *s = readKey("pocketfarm.v1");
    int v = num(s, "v");
    if (v == VERSION) {
        J *n = newState(), *stats = merge(cJSON_Duplicate(jget(n, "stats"), true), jget(s, "stats"));
        merge(n, s);
        cJSON_ReplaceItemInObjectCaseSensitive(n, "stats", stats);
        cJSON_Delete(s);
        return n;
    }
    // Older saves: keep money, items, critters, upgrades… but the farm map was redrawn, so field, cleared
    // objects and placed machines (refunded) start over.
    if (v == 1 || v == 2) {
        J *m = merge(newState(), s);
        setnum(m, "v", VERSION); setstr(m, "tool", "hoe"); setstr(m, "map", "farm");
        J *inv = child(m, "inv");
        JFOR(mc, jget(s, "machines")) {
            char k[48]; snprintf(k, sizeof k, "machine_%s", str(mc, "type"));
            setnum(inv, k, num(inv, k) + 1);
        }
        const char *reset[] = { "soil", "removed", "stumps" };
        for (int i = 0; i < 3; i++) cJSON_ReplaceItemInObjectCaseSensitive(m, reset[i], cJSON_CreateObject());
        cJSON_ReplaceItemInObjectCaseSensitive(m, "machines", cJSON_CreateArray());
        cJSON_ReplaceItemInObjectCaseSensitive(m, "px", cJSON_CreateNull());
        cJSON_ReplaceItemInObjectCaseSensitive(m, "py", cJSON_CreateNull());
        setbool(m, "tutorialDone", (num(s, "day") ? num(s, "day") : 1) > 3);
        cJSON_Delete(s);
        return m;
    }
    cJSON_Delete(s);
    return newState();
}

// The running game lives in KEY; every save also goes to its slot, so the 3 slots are 3 separate farms.
void saveState(J *s, double nowMs) {
    setnum(s, "savedAt", nowMs);
    writeKey("pocketfarm.v1", s);
    char slot[32]; snprintf(slot, sizeof slot, "pocketfarm.slot.%d", (int)(num(s, "slot") ? num(s, "slot") : 1));
    writeKey(slot, s);
}

J *slotInfo(int n) {
    char k[32]; snprintf(k, sizeof k, "pocketfarm.slot.%d", n);
    J *s = readKey(k);
    if (s && num(s, "v")) return s;
    cJSON_Delete(s);
    return NULL;
}

// Make slot n the running game (a fresh farm if it is empty). The caller restarts the game.
void useSlot(int n) {
    J *s = slotInfo(n);
    if (!s) { s = newState(); setnum(s, "slot", n); }
    writeKey("pocketfarm.v1", s);
    cJSON_Delete(s);
}

void deleteSlot(int n) { char k[32]; snprintf(k, sizeof k, "pocketfarm.slot.%d", n); remove(keyPath(k)); }

// "Novo jogo": a fresh farm in the same slot.
void resetSlot(int slot) {
    J *s = newState(); setnum(s, "slot", slot);
    writeKey("pocketfarm.v1", s);
    cJSON_Delete(s);
    deleteSlot(slot);
}

// ---------- queries ----------
J *mascotOf(J *s) {
    JFOR(c, table("CRITTERS")) if (cJSON_IsNumber(jget(s, "mascot")) && num(c, "id") == num(s, "mascot")) return c;
    return NULL;
}
bool hasBonus(J *s, const char *b) { J *m = mascotOf(s); return m && str(m, "bonus") && !strcmp(str(m, "bonus"), b); }
static bool owned(J *s, J *c) { char k[16]; snprintf(k, sizeof k, "%d", (int)num(c, "id")); return has(jget(s, "critters"), k); }
bool albumDone(J *s) { JFOR(c, table("BASE_CRITTERS")) if (!owned(s, c)) return false; return true; }
bool commonsDone(J *s) { JFOR(c, table("BASE_CRITTERS")) if (!strcmp(str(c, "rarity"), "common") && !owned(s, c)) return false; return true; }

// Comfort: every placed piece of furniture and displayed critter adds up; it turns into bonus max energy.
int comfort(J *s) {
    int n = 0;
    JFOR(p, jget(jget(s, "house"), "placed")) {
        const char *item = str(p, "item");
        n += !strncmp(item, "critter:", 8) ? 1 : num(jget(table("FURNITURE"), item), "comfort");
    }
    return n;
}
int comfortBonus(J *s) { return fmin(6, floor(comfort(s) / 6.0)); }
int maxEnergy(J *s) {
    return num(DATA, "BASE_ENERGY") + (has(jget(s, "upgrades"), "bed") ? 5 : 0) + (hasBonus(s, "energy") ? 2 : 0) + (albumDone(s) ? 3 : 0) + comfortBonus(s);
}

// ---------- inventory ----------
int count(J *s, const char *id) { return num(jget(s, "inv"), id); }
void give(J *s, const char *id, int n) { setnum(child(s, "inv"), id, count(s, id) + n); }
bool take(J *s, const char *id, int n) {
    if (count(s, id) < n) return false;
    J *inv = jget(s, "inv");
    setnum(inv, id, count(s, id) - n);
    if (!count(s, id)) delkey(inv, id);
    return true;
}
// cost = { itemId: qty, money: n }
bool canAfford(J *s, J *cost) {
    JFOR(k, cost) if ((!strcmp(k->string, "money") ? num(s, "money") : count(s, k->string)) < numv(k)) return false;
    return true;
}
bool pay(J *s, J *cost) {
    if (!canAfford(s, cost)) return false;
    JFOR(k, cost) if (!strcmp(k->string, "money")) setnum(s, "money", num(s, "money") - numv(k)); else take(s, k->string, numv(k));
    return true;
}
void earn(J *s, int n) {
    setnum(s, "money", num(s, "money") + n);
    J *st = child(s, "stats");
    setnum(st, "earned", num(st, "earned") + n);
}
// Selling price with mascot / album bonuses.
bool isFairDay(J *s) { return has(jget(s, "projects"), "fair") && (int)num(s, "day") % 7 == 0; }
double sellMult(J *s) { return 1 + (hasBonus(s, "sell") ? 0.05 : 0) + (commonsDone(s) ? 0.05 : 0) + (isFairDay(s) ? 0.25 : 0); }
