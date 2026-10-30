// Hand cooking stations (cooking.js). A station keeps its state in m.st. Work stations (mill, kitchen, tub, press)
// take an input into a queue and turn one into a product every WORK_TAPS interactions ("press A repeatedly").
// Oven and stove cook on a timer in game minutes; bread left in the oven too long burns.
// STATIONS is the dumped table: { type: { input, work, verb, mix, bake, minutes, burnAfter, fuel, needs } }.
#include <stdio.h>
#include "game.h"

#define WORK_STEP 0.2     // 5 taps per item
#define STATION_BATCH 10  // ponytail: a fixed batch keeps the menu reachable (all queued work turns interaction into taps)

static J *sdef(J *m) { return jget(table("STATIONS"), str(m, "type")); }
static double absMin(J *s) { return (num(s, "day") - 1) * 1440 + num(s, "time"); }
static J *st(J *m) { return child(m, "st"); }

// Queue up to STATION_BATCH of `id` from the backpack and warehouses. Returns how many went in.
int addInput(J *s, J *m, const char *id) {
    int n = countAll(s, id);
    if (n > STATION_BATCH) n = STATION_BATCH;
    if (n) { takeAll(s, id, n); J *q = child(st(m), "queue"); setnum(q, id, num(q, id) + n); }
    return n;
}
int stationQueued(J *m) { int n = 0; JFOR(it, jget(jget(m, "st"), "queue")) n += numv(it); return n; }

// One tap of work. Returns the product when one is finished (else NULL); *progress gets the tap progress.
const char *work(J *s, J *m, double *progress) {
    J *def = sdef(m), *t = st(m), *q = jget(t, "queue"), *id = NULL;
    JFOR(k, q) if (numv(k) > 0) { id = k; break; }
    if (progress) *progress = 0;
    if (!str(def, "work") || !id) return NULL;
    setnum(t, "progress", num(t, "progress") + WORK_STEP);
    if (progress) *progress = num(t, "progress");
    if (num(t, "progress") < 1 - 1e-9) return NULL;
    setnum(t, "progress", 0);
    setnum(q, id->string, numv(id) - 1);
    if (!num(q, id->string)) delkey(q, id->string);
    give(s, str(def, "work"), 1);
    if (progress) *progress = 1;
    return str(def, "work");
}

// Kitchen: 1 flour + 1 water -> 1 raw dough.
bool mix(J *s, J *m) {
    J *r = jget(sdef(m), "mix");
    if (!r || countAll(s, "flour") < num(r, "flour") || countAll(s, "water") < num(r, "water")) return false;
    takeAll(s, "flour", num(r, "flour")); takeAll(s, "water", num(r, "water")); give(s, str(r, "out"), 1);
    return true;
}

// Oven / stove: start cooking `id`. Returns an error, or NULL.
const char *startCook(J *s, J *m, const char *id) {
    J *def = sdef(m), *t = st(m);
    if (has(t, "cooking")) return "Something is already cooking.";
    const char *out = str(jget(def, "bake"), id);
    if (!out || !countAll(s, id)) return "Missing the ingredient.";
    J *fuels = jget(def, "fuel");
    const char *fuel = NULL;
    JFOR(f, fuels) if (countAll(s, f->valuestring)) { fuel = f->valuestring; break; }
    if (fuels && !fuel) return "Missing wood or coal.";
    const char *needs = str(def, "needs");
    if (needs && !countAll(s, needs)) return !strcmp(needs, "rennet") ? "Missing rennet." : "Missing an ingredient.";
    takeAll(s, id, 1); if (fuel) takeAll(s, fuel, 1); if (needs) takeAll(s, needs, 1);
    J *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "id", id); cJSON_AddStringToObject(c, "out", out);
    cJSON_AddNumberToObject(c, "ready", absMin(s) + num(def, "minutes"));
    if (jget(t, "cooking")) cJSON_ReplaceItemInObjectCaseSensitive(t, "cooking", c); else cJSON_AddItemToObject(t, "cooking", c);
    return NULL;
}

// "idle", "cooking", "ready" or "burnt".
const char *cookState(J *s, J *m) {
    J *c = jget(jget(m, "st"), "cooking"), *def = sdef(m);
    if (!cJSON_IsObject(c)) return "idle";
    double t = absMin(s);
    if (t < num(c, "ready")) return "cooking";
    return num(def, "burnAfter") && t > num(c, "ready") + num(def, "burnAfter") ? "burnt" : "ready";
}
int minutesLeft(J *s, J *m) { return fmax(0, ceil(num(jget(jget(m, "st"), "cooking"), "ready") - absMin(s))); }

// Take out what's done (burnt bread if it sat too long). Returns the item, or NULL if not ready.
const char *collectCooked(J *s, J *m) {
    const char *k = cookState(s, m);
    if (!strcmp(k, "idle") || !strcmp(k, "cooking")) return NULL;
    static char out[32];
    snprintf(out, sizeof out, "%s", !strcmp(k, "burnt") ? "burnt_bread" : str(jget(jget(m, "st"), "cooking"), "out"));
    if (!strcmp(out, "bread")) { J *stt = child(s, "stats"); setnum(stt, "baked", num(stt, "baked") + 1); }
    setstr(jget(m, "st"), "cooking", NULL);
    give(s, out, 1);
    return out;
}

// Tub: must + empty bottle + cork -> bottled must.
bool bottle(J *s, J *m) {
    if (strcmp(str(m, "type"), "tub") || !countAll(s, "must") || !countAll(s, "bottle") || !countAll(s, "cork")) return false;
    takeAll(s, "must", 1); takeAll(s, "bottle", 1); takeAll(s, "cork", 1); give(s, "bottled_must", 1);
    return true;
}

// What a picked-up station gives back (a new object; the caller frees it).
J *stationContents(J *m) {
    J *q = jget(jget(m, "st"), "queue"), *out = cJSON_IsObject(q) ? cJSON_Duplicate(q, true) : cJSON_CreateObject();
    J *c = jget(jget(m, "st"), "cooking");
    if (cJSON_IsObject(c) && str(c, "id")) setnum(out, str(c, "id"), num(out, str(c, "id")) + 1);   // ponytail: an interrupted bake/cook gives the ingredient back
    return out;
}
