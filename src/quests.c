// Villager requests (quests.js). The QUESTS table comes from data.json; its check / progress / hint / coach.done
// functions are ported by hand below, keyed by quest id.
#include <stdio.h>
#include "api.h"

static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static J *stats(J *s) { return jget(s, "stats"); }
static double stat(J *s, const char *k) { return num(stats(s), k); }

// ---------- helpers (quests.js top) ----------
static bool hasMachine(J *s, const char *type) {
    JFOR(m, jget(s, "machines")) if (eq(str(m, "type"), type)) return true;
    char k[48]; snprintf(k, sizeof k, "machine_%s", type);
    return count(s, k) > 0;
}
static bool placed2(J *s, const char *a, const char *b) {
    JFOR(m, jget(s, "machines")) { const char *t = str(m, "type"); if (eq(t, a) || (b && eq(t, b))) return true; }
    return false;
}
static bool anyNonBelt(J *s) { JFOR(m, jget(s, "machines")) if (!eq(str(m, "type"), "belt")) return true; return false; }
static bool wiredArm(J *s) {
    JFOR(m, jget(s, "machines")) {
        if (!eq(str(m, "type"), "arm")) continue;
        int d = dirFrom(str(m, "dir")), b = d == UP ? DOWN : d == DOWN ? UP : d == LEFT ? RIGHT : LEFT;
        int x = num(m, "x"), y = num(m, "y");
        J *f = machineAt(s, x + DV[d][0], y + DV[d][1]), *bk = machineAt(s, x + DV[b][0], y + DV[b][1]);
        if (f && bk && f != bk) return true;
    }
    return false;
}
// factory.js powered(s).size > 0. ponytail: factory.c keeps powered() static, so the network test is copied here.
static int msize(J *m) { return num(mdef(m), "size"); }
static bool near(J *a, J *b, double r) {
    double ax = num(a, "x") + (msize(a) - 1) / 2.0, ay = num(a, "y") + (msize(a) - 1) / 2.0;
    double bx = num(b, "x") + (msize(b) - 1) / 2.0, by = num(b, "y") + (msize(b) - 1) / 2.0;
    return fmax(fabs(ax - bx), fabs(ay - by)) <= r;
}
static bool anyPowered(J *s) {
    J *nodes[256]; int n = 0; double cap = 0;
    J *t = jget(s, "time");
    bool day = (cJSON_IsNumber(t) ? t->valuedouble : num(DATA, "DAY_START")) < 18 * 60;
    JFOR(m, jget(s, "machines")) {
        J *d = mdef(m);
        if (num(d, "generator") && (!eq(str(m, "type"), "solar") || day) && (!str(d, "fuel") || num(m, "fuel") > 0) && n < 256) { nodes[n++] = m; cap += num(d, "generator"); }
    }
    if (cap <= 0) return false;
    for (int i = 0; i < n; i++)
        JFOR(p, jget(s, "machines")) {
            if (!eq(str(p, "type"), "pole") || n >= 256) continue;
            bool in = false;
            for (int k = 0; k < n && !in; k++) in = nodes[k] == p;
            if (!in && near(nodes[i], p, 6)) nodes[n++] = p;
        }
    JFOR(m, jget(s, "machines")) {
        if (!has(mdef(m), "power")) continue;
        for (int i = 0; i < n; i++) if (near(nodes[i], m, 3 + (msize(m) - 1))) return true;
    }
    return false;
}
static int talked(J *s) {
    int n = 0;
    JFOR(v, table("VILLAGERS")) n += num(jget(jget(s, "friends"), v->string), "pts") > 0;
    return n;
}
static int crops(J *s) { int n = 0; JFOR(id, table("CROP_IDS")) n += count(s, id->valuestring); return n; }
static int houseCount(J *s) { return jlen(jget(jget(s, "house"), "placed")); }

// ---------- per-quest functions ----------
static bool check(J *s, const char *id, bool *hasCheck) {
    *hasCheck = true;
    if (eq(id, "intro")) return talked(s) >= 5;
    if (eq(id, "fish")) return stat(s, "fished") >= 3;
    if (eq(id, "decorate")) return houseCount(s) >= 5;
    if (eq(id, "project")) return jlen(jget(s, "projects")) >= 1;
    if (eq(id, "mine")) return num(s, "mineDeepest") >= 3;
    if (eq(id, "smelt")) return stat(s, "smelted") >= 2;
    if (eq(id, "friend")) return maxHearts(s) >= 2;
    if (eq(id, "coop")) { JFOR(a, jget(s, "animals")) if (eq(str(a, "kind"), "chicken")) return true; return false; }
    if (eq(id, "machine")) return anyNonBelt(s);
    if (eq(id, "line")) return stat(s, "armMoves") >= 5;
    if (eq(id, "power")) return stat(s, "poweredWork") >= 3;
    if (eq(id, "logistics")) return stat(s, "routed") >= 10;
    if (eq(id, "trail")) return has(jget(s, "projects"), "trail");
    if (eq(id, "forest")) return stat(s, "foraged") >= 5;
    if (eq(id, "festival")) return has(jget(s, "flags"), "festival");
    *hasCheck = false;
    return false;
}

static const char *fmtp(char *buf, int n, double have, int need) { snprintf(buf, n, "%d/%d", (int)fmin(need, have), need); return buf; }
const char *questProgress(J *s, J *q, char *buf, int n) {
    const char *id = str(q, "id");
    if (eq(id, "intro")) return fmtp(buf, n, talked(s), 5);
    if (eq(id, "carrots")) return fmtp(buf, n, crops(s), 5);
    if (eq(id, "fish")) return fmtp(buf, n, stat(s, "fished"), 3);
    if (eq(id, "wood")) return fmtp(buf, n, count(s, "wood"), 20);
    if (eq(id, "decorate")) return fmtp(buf, n, houseCount(s), 5);
    if (eq(id, "mine")) return fmtp(buf, n, num(s, "mineDeepest"), 3);
    if (eq(id, "smelt")) return fmtp(buf, n, stat(s, "smelted"), 2);
    if (eq(id, "friend")) { snprintf(buf, n, "%d/2 :heart:", maxHearts(s)); return buf; }
    if (eq(id, "line")) return fmtp(buf, n, stat(s, "armMoves"), 5);
    if (eq(id, "power")) return fmtp(buf, n, stat(s, "poweredWork"), 3);
    if (eq(id, "logistics")) return fmtp(buf, n, stat(s, "routed"), 10);
    if (eq(id, "forest")) return fmtp(buf, n, stat(s, "foraged"), 5);
    return NULL;   // project, coop, machine, trail, festival: ''
}

// questQuery().npc(name): live position when they're on this map, else their usual spot
static bool npcAt(J *s, const char *name, Hint *out) {
    Tile t;
    if (game_npcTile(name, &t)) { *out = (Hint){ true }; snprintf(out->map, sizeof out->map, "%s", game_worldId()); out->x = t.x; out->y = t.y; return true; }
    JFOR(e, jget(table("ENTITIES"), "town")) {
        if (!eq(str(e, "type"), "npc") || !eq(str(e, "name"), name)) continue;
        *out = (Hint){ true };
        if (str(e, "home") && !isFestival(s)) {
            J *p = jget(jget(table("INTERIORS"), str(e, "home")), "npc");
            snprintf(out->map, sizeof out->map, "%s", str(e, "home"));
            out->x = num(p, "x"); out->y = num(p, "y");
        } else { snprintf(out->map, sizeof out->map, "town"); out->x = num(e, "x"); out->y = num(e, "y"); }
        return true;
    }
    return false;
}
static bool at(Hint *out, const char *map, int x, int y) { *out = (Hint){ true }; snprintf(out->map, sizeof out->map, "%s", map); out->x = x; out->y = y; return true; }
static bool atP(Hint *out, const char *map, J *p) { return at(out, map, num(p, "x"), num(p, "y")); }

bool questHint(J *s, J *q, Hint *out) {
    const char *id = str(q, "id"), *where = game_worldId();
    J *town = jget(table("MAPS"), "town");
    *out = (Hint){ 0 };
    if (eq(id, "intro")) {
        JFOR(e, jget(table("ENTITIES"), "town"))
            if (eq(str(e, "type"), "npc") && !(num(jget(jget(s, "friends"), str(e, "name")), "pts") > 0)) return npcAt(s, str(e, "name"), out);
        return false;
    }
    if (eq(id, "fish")) return has(jget(s, "tools"), "rod") ? false : npcAt(s, "Fin", out);
    if (eq(id, "decorate")) return eq(where, "house") ? false : atP(out, "farm", jget(jget(table("MAPS"), "farm"), "door"));
    if (eq(id, "project") || eq(id, "trail")) return atP(out, "town", jget(town, "board"));
    if (eq(id, "mine")) return eq(where, "mine") ? false : at(out, "town", 36, 3);
    if (eq(id, "smelt") || eq(id, "machine")) return npcAt(s, "Mike", out);
    if (eq(id, "coop")) return npcAt(s, "Jack", out);
    if (eq(id, "forest")) return eq(where, "forest") ? false : at(out, "forest", 20, 2);
    if (eq(id, "festival")) return isFestival(s) ? atP(out, "town", jget(town, "gacha")) : false;
    return false;
}

static bool coachDone(J *s, const char *id, int i) {
    if (eq(id, "machine")) {
        if (i == 0) return hasMachine(s, "furnace") || placed2(s, "chest", "sprinkler");
        if (i == 1) return eq(str(s, "tool"), "build") || anyNonBelt(s);
        return anyNonBelt(s);
    }
    double arm = stat(s, "armMoves"), pw = stat(s, "poweredWork"), routed = stat(s, "routed");
    if (eq(id, "line")) {
        if (i == 0) return placed2(s, "chest", "belt") || arm > 0;
        if (i == 1) return wiredArm(s) || arm > 0;
        return arm > 0;
    }
    if (eq(id, "power")) {
        if (i == 0) return placed2(s, "processor", "miner");
        if (i == 1) return anyPowered(s) || pw > 0;
        return pw > 0 || placed2(s, "miner", NULL);
    }
    if (eq(id, "logistics")) {
        if (i == 0) return placed2(s, "splitter", "tunnel") || routed > 0;
        return routed > 0;
    }
    return true;
}

// ---------- exports ----------
int activeQuests(J *s, J **out, int max) {
    if (!has(s, "tutorialDone")) return 0;
    J *done = jget(jget(s, "quests"), "done");
    int n = 0;
    JFOR(q, table("QUESTS")) {
        if (has(done, str(q, "id")) || (str(q, "after") && !has(done, str(q, "after")))) continue;
        if (n < max) out[n++] = q;
    }
    return n;
}

// First unfinished coach step of the first active quest that has one (factory walkthrough).
J *coachStep(J *s) {
    J *qs[32]; int n = activeQuests(s, qs, 32);
    for (int k = 0; k < n; k++) {
        int i = 0;
        JFOR(c, jget(qs[k], "coach")) {
            if (!coachDone(s, str(qs[k], "id"), i++)) {
                J *r = cJSON_CreateObject();
                cJSON_AddItemToObject(r, "quest", cJSON_Duplicate(qs[k], true));
                cJSON_AddItemToObject(r, "step", cJSON_Duplicate(c, true));
                return r;
            }
        }
    }
    return NULL;
}

// Rewards land in the inventory; returns a readable list.
static J *grant(J *s, J *q) {
    J *r = jget(q, "reward"), *out = cJSON_CreateArray();
    char b[128];
    if (num(r, "money")) { earn(s, num(r, "money")); snprintf(b, sizeof b, "%d coins", (int)num(r, "money")); cJSON_AddItemToArray(out, cJSON_CreateString(b)); }
    if (num(r, "tickets")) { setnum(s, "tickets", num(s, "tickets") + num(r, "tickets")); snprintf(b, sizeof b, "%d capsule ticket(s)", (int)num(r, "tickets")); cJSON_AddItemToArray(out, cJSON_CreateString(b)); }
    JFOR(it, jget(r, "items")) {
        give(s, it->string, numv(it));
        snprintf(b, sizeof b, "%d× %s", (int)numv(it), str(jget(table("ITEMS"), it->string), "name"));
        cJSON_AddItemToArray(out, cJSON_CreateString(b));
    }
    JFOR(it, jget(r, "decor")) {
        J *d = child(s, "decor");
        setnum(d, it->string, num(d, it->string) + numv(it));
        cJSON_AddItemToArray(out, cJSON_CreateString(str(jget(table("FURNITURE"), it->string), "name")));
    }
    if (num(r, "friend")) {
        J *fs = child(s, "friends"), *f = jget(fs, str(q, "from"));
        if (!f) { f = cJSON_Parse("{\"talked\":0,\"gifted\":0,\"claimed\":[]}"); cJSON_AddItemToObject(fs, str(q, "from"), f); }
        setnum(f, "pts", num(f, "pts") + num(r, "friend"));
    }
    setbool(child(child(s, "quests"), "done"), str(q, "id"), true);
    return out;
}
static J *result(J *q, J *reward) {
    J *r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "quest", cJSON_Duplicate(q, true));
    if (reward) cJSON_AddItemToObject(r, "reward", reward);
    else cJSON_AddTrueToObject(r, "missing");
    return r;
}

// Quests that finished on their own since last time.
int checkQuests(J *s, J **done, int max) {
    J *qs[32], *hit[32]; int n = activeQuests(s, qs, 32), k = 0;
    for (int i = 0; i < n; i++) { bool hc; if (check(s, str(qs[i], "id"), &hc) && k < max) hit[k++] = qs[i]; }
    for (int i = 0; i < k; i++) done[i] = result(hit[i], grant(s, hit[i]));
    return k;
}

// Hand in a delivery quest when talking to its villager.
J *deliverQuest(J *s, const char *name) {
    J *qs[32]; int n = activeQuests(s, qs, 32);
    J *q = NULL;
    for (int i = 0; i < n && !q; i++) if (jget(qs[i], "deliver") && eq(str(qs[i], "from"), name)) q = qs[i];
    if (!q) return NULL;
    J *d = jget(q, "deliver")->child;
    const char *item = d->string; int need = numv(d);
    if (eq(item, "crop")) {
        if (crops(s) < need) return result(q, NULL);
        int left = need;
        JFOR(id, table("CROP_IDS")) while (left && take(s, id->valuestring, 1)) left--;
    } else if (!take(s, item, need)) return result(q, NULL);
    return result(q, grant(s, q));
}
