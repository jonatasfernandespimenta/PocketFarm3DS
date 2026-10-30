// Progressive, mostly wordless tutorial (tutorial.js STEPS, ported by hand) and main.js updateTutorial(): the
// objective chip, the quest tracker, the pulsing tool and the target the arrow points at.
#include <stdio.h>
#include "api.h"

void game_popup(void (*fn)(void *), const void *arg, size_t n);   // game.c: runs when nothing else is up

static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

// A step's hint: { tool, icon, target: {map, x, y}, ui }
typedef struct { bool on; const char *tool; char icon[32]; Hint target; bool menuBtn; } TutHint;

static Hint tgt(const char *map, int x, int y) { Hint h = { true }; snprintf(h.map, sizeof h.map, "%s", map); h.x = x; h.y = y; return h; }
static Hint tgtP(const char *map, J *p) { return tgt(map, num(p, "x"), num(p, "y")); }
static J *farmMap(void) { return jget(table("MAPS"), "farm"); }
static J *townMap(void) { return jget(table("MAPS"), "town"); }
static void xy(const char *k, int *x, int *y) { *x = atoi(k); const char *c = strchr(k, ','); *y = c ? atoi(c + 1) : 0; }

static int ncrops(J *s) { int n = 0; JFOR(t, jget(s, "soil")) n += !!str(t, "crop"); return n; }
// firstSoil(state, pred): pred 0 = no crop, 1 = crop and dry
static Hint firstSoil(J *s, int pred) {
    JFOR(t, jget(s, "soil")) {
        bool ok = pred == 0 ? !str(t, "crop") : str(t, "crop") && !has(t, "wet");
        if (ok) { int x, y; xy(t->string, &x, &y); return tgt("farm", x, y); }
    }
    return (Hint){ 0 };
}
static Hint bedOrDoor(void) { Tile t; return game_bedTile(&t) ? tgt("house", t.x, t.y) : tgtP("farm", jget(farmMap(), "door")); }

static bool stepDone(J *s, int i) {
    J *st = jget(s, "stats");
    switch (i) {
    case 0: return jlen(jget(s, "soil")) > 0;
    case 1: return ncrops(s) > 0 || num(st, "harvested") > 0;
    case 2: { JFOR(t, jget(s, "soil")) if (str(t, "crop") && has(t, "wet")) return true; return num(s, "day") > 1; }
    case 3: return num(st, "cleared") > 0;
    case 4: return num(s, "day") > 1;
    case 5: return num(st, "harvested") > 0;
    case 6: return num(st, "earned") > 0;
    case 7: return has(jget(s, "flags"), "town");
    case 8: return jlen(jget(jget(s, "contracts"), "active")) > 0 || num(st, "contracts") > 0;
    case 9: return jlen(jget(s, "critters")) > 0;
    case 10: return has(s, "mascot");
    }
    return true;
}
static TutHint H(const char *tool, const char *icon, Hint t) { TutHint h = { true, tool }; snprintf(h.icon, sizeof h.icon, "%s", icon); h.target = t; return h; }
static TutHint stepHint(J *s, int i) {
    switch (i) {
    case 0: return H("hoe", "tool:hoe", tgtP("farm", jget(farmMap(), "plot")));
    case 1: return H("seeds", "item:seed_carrot", firstSoil(s, 0));
    case 2: return H("can", "tool:can", firstSoil(s, 1));
    case 3: {
        const char *tool; Tile t; char ic[32];
        if (!game_nearestDebris(&tool, &t)) return (TutHint){ 0 };
        snprintf(ic, sizeof ic, "tool:%s", tool);
        return H(tool, ic, tgt("farm", t.x, t.y));
    }
    case 4: return H(NULL, "sprite:moon", bedOrDoor());
    case 5: {
        J *dry = NULL; int n = 0;
        JFOR(t, jget(s, "soil")) {
            if (!str(t, "crop")) continue;
            n++;
            if (isReady(t)) { int x, y; xy(t->string, &x, &y); return H("hand", "tool:hand", tgt("farm", x, y)); }
            if (!dry && !has(t, "wet")) dry = t;
        }
        if (dry) { int x, y; xy(dry->string, &x, &y); return H("can", "tool:can", tgt("farm", x, y)); }
        if (!n) { Hint f = firstSoil(s, 0); return H("seeds", "item:seed_carrot", f.has ? f : tgtP("farm", jget(farmMap(), "plot"))); }
        return H(NULL, "sprite:moon", bedOrDoor());
    }
    case 6: return H(NULL, "sprite:coin", tgtP("farm", jget(farmMap(), "sellBox")));
    case 7: return H(NULL, "sprite:go", tgt("town", 2, 14));
    case 8: return H(NULL, "sprite:board", tgtP("town", jget(townMap(), "board")));
    case 9: return H(NULL, "sprite:gacha", tgtP("town", jget(townMap(), "gacha")));
    case 10: { TutHint h = H(NULL, "critter", (Hint){ 0 }); h.menuBtn = true; return h; }
    }
    return (TutHint){ 0 };
}

// tutorial.js currentStep: index of the first unfinished step, or -1 when the tutorial is over
static int currentStep(J *s) {
    if (has(s, "tutorialDone")) return -1;
    int n = jlen(table("STEPS"));
    for (int i = 0; i < n; i++) if (!stepDone(s, i)) return i;
    setbool(s, "tutorialDone", true);
    return -1;
}

// icon specs as hud.c's drawIcon takes them (main.js iconNode): 'critter' = the first owned critter
static void iconSpec(const char *icon, char *out, int n) {
    if (eq(icon, "critter")) {
        J *c = NULL;
        JFOR(k, table("CRITTERS")) { char id[16]; snprintf(id, sizeof id, "%d", (int)num(k, "id")); if (num(jget(state, "critters"), id)) { c = k; break; } }
        if (!c) c = jat(table("CRITTERS"), 0);
        snprintf(out, n, "critter:%d", (int)num(c, "id"));
    } else if (!icon || !icon[0] || strchr(icon, ':')) snprintf(out, n, "%s", icon ? icon : "");
    else snprintf(out, n, "sprite:%s", icon);
}

static char tutKey[256], questKey[512];

static void guidePopup(void *arg) { shops_factoryGuide((const char *)arg); }

Hint tutorial_update(const char *mapId, double px, double py) {
    J *s = state;
    int ti = currentStep(s);
    J *steps = table("STEPS");
    // factory walkthrough: the coach step of an active factory quest shows like a tutorial step
    J *coach = ti < 0 ? coachStep(s) : NULL;
    bool cur = ti >= 0 || coach;
    char curId[256] = "", curLabel[160] = "";
    TutHint hint = { 0 };
    if (ti >= 0) {
        snprintf(curId, sizeof curId, "%s", str(jat(steps, ti), "id"));
        snprintf(curLabel, sizeof curLabel, "%s", str(jat(steps, ti), "label"));
        hint = stepHint(s, ti);
    } else if (coach) {
        J *q = jget(coach, "quest"), *st = jget(coach, "step");
        snprintf(curId, sizeof curId, "coach-%s%s", str(q, "id"), str(st, "label"));
        snprintf(curLabel, sizeof curLabel, "%s", str(st, "label"));
        hint = (TutHint){ true, NULL };
        snprintf(hint.icon, sizeof hint.icon, "%s", str(st, "icon") ? str(st, "icon") : "");
        static char tool[16];   // outlives `coach`
        snprintf(tool, sizeof tool, "%s", str(st, "tool") ? str(st, "tool") : "");
        hint.tool = tool[0] ? tool : NULL;
    }
    J *qs[32]; int nq = activeQuests(s, qs, 32);
    if (ti < 0) for (int i = 0; i < nq; i++) {
        const char *g = str(qs[i], "guide");
        char k[48];
        if (!g) continue;
        snprintf(k, sizeof k, "guide_%s", g);
        if (has(jget(s, "flags"), k)) continue;
        setbool(child(s, "flags"), k, true);
        char arg[32]; snprintf(arg, sizeof arg, "%s", g);
        game_popup(guidePopup, arg, sizeof arg);
    }
    J *quest = !cur && nq ? qs[0] : NULL;
    char qlabel[200] = "", pb[48];
    if (quest) {
        const char *p = questProgress(s, quest, pb, sizeof pb);
        snprintf(qlabel, sizeof qlabel, "%s%s%s", str(quest, "title"), p ? " · " : "", p ? p : "");
    }
    char glabel[160] = "";
    J *goal = !cur && !quest ? nextGoal(s, glabel, sizeof glabel) : NULL;
    char k[256];
    if (cur) snprintf(k, sizeof k, "%s", curId);
    else if (quest) snprintf(k, sizeof k, "quest:%s", qlabel);
    else if (goal) snprintf(k, sizeof k, "goal:%s", glabel);
    else snprintf(k, sizeof k, "done");

    // quest tracker: refresh when any active quest's progress changes
    {
        char qkey[512] = "", progs[3][48];
        const char *titles[32], *prog[32];
        int n = cur ? 0 : nq;
        for (int i = 0; i < n; i++) {
            char b[48];
            const char *p = questProgress(s, qs[i], b, sizeof b);
            size_t l = strlen(qkey);
            snprintf(qkey + l, sizeof qkey - l, "%s%s%s", i ? "|" : "", str(qs[i], "id"), p ? p : "");
            titles[i] = str(qs[i], "title");
            prog[i] = NULL;
            if (i < 3 && p) { snprintf(progs[i], sizeof progs[i], "%s", p); prog[i] = progs[i]; }
        }
        if (strcmp(qkey, questKey)) {
            snprintf(questKey, sizeof questKey, "%s", qkey);
            hud_setQuests(titles, prog, n);
        }
    }
    if (strcmp(k, tutKey)) {
        if (tutKey[0] && strcmp(tutKey, "done") && !strchr(tutKey, ':')) { sfx("catch"); floatText(px, py - 34, ":check:"); }
        snprintf(tutKey, sizeof tutKey, "%s", k);
        // the chip: tutorial / coach steps and village goals (quests live in the tracker)
        if (cur || goal) {
            char ic[48];
            if (cur) iconSpec(hint.icon, ic, sizeof ic); else snprintf(ic, sizeof ic, "home");
            hud_objective(ic, cur ? curLabel : glabel, !cur || coach);
        } else hud_objective(NULL, NULL, false);
    }
    cJSON_Delete(coach);

    Hint target = { 0 };
    const char *tool = NULL; bool menuBtn = false;
    if (cur) { if (hint.on) { target = hint.target; tool = hint.tool; menuBtn = hint.menuBtn; } }
    else if (quest) questHint(s, quest, &target);
    // On another map game.c points at the exit instead. ponytail: the web also drops the tool pulse then (when
    // that map has an exit toward it); we drop it whenever the target is elsewhere.
    if (target.has && !eq(target.map, mapId)) tool = NULL;
    hud_hint(tool && !eq(str(s, "tool"), tool) ? tool : NULL, menuBtn);
    return target;
}

void tutorial_tapObjective(void) {
    if (!strncmp(tutKey, "coach-", 6)) {
        J *c = coachStep(state);
        if (!c) return;
        char g[32]; snprintf(g, sizeof g, "%s", str(jget(c, "quest"), "guide") ? str(jget(c, "quest"), "guide") : "");
        cJSON_Delete(c);
        shops_factoryGuide(g);
    }
    else if (!strncmp(tutKey, "quest:", 6)) shops_questLog();
    else if (!strncmp(tutKey, "goal:", 5)) shops_projectsMenu();
}
