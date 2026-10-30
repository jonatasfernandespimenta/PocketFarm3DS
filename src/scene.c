// Ponder scenes and drawScene (main.js): a sandbox factory drawn inside a menu (menu_canvas).
#include <stdio.h>
#include "api.h"

void armForget(double id);
void game_drawFlatPiece(J *m);
void game_drawBeltItems(J **list, int n);
double game_machineY(J *st, J *m);

// ---------- Ponder (like Create's): the guide card plays a tiny factory with the real rules and sprites ----------
// Pieces drop in one by one with their name as caption, then the line runs for a while and the scene starts over.
// src chests never run dry; sink chests keep only a few items.
static const char *PONDER_JSON = "{"
    "\"machine\":{\"w\":3,\"h\":2,\"pieces\":[[\"furnace\",0,0,\"right\"],[\"chest\",2,1,\"right\",\"sink\"]],\"hand\":[\"copper_ore\",\"coal\"],"
        "\"caption\":\"Ore + coal go in, a bar comes out the front\"},"
    "\"line\":{\"w\":7,\"h\":2,\"pieces\":[[\"chest\",0,1,\"right\",[\"copper_ore\",\"coal\"]],[\"arm\",1,1,\"right\"],[\"furnace\",2,0,\"right\"],"
        "[\"belt\",4,1,\"right\"],[\"belt\",5,1,\"right\"],[\"chest\",6,1,\"right\",\"sink\"]],\"caption\":\"The arm takes from behind, puts in front\"},"
    "\"power\":{\"w\":8,\"h\":2,\"pieces\":[[\"solar\",0,0,\"right\"],[\"chest\",2,1,\"right\",[\"egg\"]],[\"arm\",3,1,\"right\"],[\"processor\",4,0,\"right\"],"
        "[\"belt\",6,1,\"right\"],[\"chest\",7,1,\"right\",\"sink\"]],\"caption\":\"The solar panel powers machines near it\"},"
    "\"logistics\":{\"w\":5,\"h\":3,\"pieces\":[[\"chest\",0,1,\"right\",[\"copper_bar\"]],[\"arm\",1,1,\"right\"],[\"belt\",2,1,\"right\"],[\"splitter\",3,1,\"right\"],"
        "[\"belt\",3,0,\"right\"],[\"belt\",3,2,\"right\"],[\"chest\",4,0,\"right\",\"sink\"],[\"chest\",4,2,\"right\",\"sink\"]],"
        "\"caption\":\"The splitter sends one item to each side\"},"
    "\"storage\":{\"w\":8,\"h\":4,\"pieces\":[[\"chest\",0,3,\"right\",[\"wheat\",\"corn\"]],[\"arm\",1,3,\"right\"],[\"belt\",2,3,\"right\"],[\"silo\",3,2,\"down\"],"
        "[\"arm\",5,3,\"right\"],[\"belt\",6,3,\"right\"],[\"chest\",7,3,\"right\",\"sink\"]],\"caption\":\"Cargo goes in at the orange mouth, an arm takes it out at the grey one\"},"
    "\"sorter\":{\"w\":6,\"h\":2,\"pieces\":[[\"chest\",0,1,\"right\",[\"milk\",\"wool\"]],[\"arm\",1,1,\"right\"],[\"belt\",2,1,\"right\"],"
        "[\"sorter\",3,1,\"right\",null,{\"filter\":\"wool\"}],[\"belt\",4,1,\"right\"],[\"chest\",5,1,\"right\",\"sink\"],[\"chest\",3,0,\"right\",\"sink\"]],"
        "\"caption\":\"The sorter turns the wool to the arrow's side; milk goes straight on\"},"
    "\"auto\":{\"w\":8,\"h\":2,\"pieces\":[[\"solar\",0,0,\"right\"],[\"chest\",2,1,\"right\",[\"milk\"]],[\"arm\",3,1,\"right\"],[\"cheesemaker\",4,0,\"right\"],"
        "[\"belt\",6,1,\"right\"],[\"chest\",7,1,\"right\",\"sink\"]],\"caption\":\"The Cheese Maker curdles and presses on its own. Take it to the Cellar\"}"
"}";
static J *PONDER;
#define RUN 16
#define DROP 0.6
static double dropOf(J *sc) { return fmin(DROP, 6.0 / jlen(jget(sc, "pieces"))); }   // big blueprints drop faster

// The sandbox canvas inside an open menu (Ponder demo or Workshop bench): the step hook runs on every sim step
// while its canvas is still being drawn (the web checks c.isConnected; here: drawScene ran in the last half second).
static void (*hook)(void);
static double lastDraw;
void scene_set(void (*stepFn)(void)) { hook = stepFn; lastDraw = gtime; }
void scene_step(void) {
    if (!hook) return;
    if (gtime - lastDraw > 0.5) { hook = NULL; return; }
    hook();
}

static struct { J *sc, *s; double t, last; } P;

static void ponderReset(void) {
    P.t = 0;
    cJSON_Delete(P.s);
    P.s = cJSON_Parse("{\"time\":720,\"stats\":{},\"inv\":{},\"critters\":{},\"money\":0,\"machines\":[]}");
    J *ms = jget(P.s, "machines");
    int i = 0;
    JFOR(pc, jget(P.sc, "pieces")) {
        J *m = cJSON_Parse("{\"input\":{},\"out\":[],\"fuel\":0,\"progress\":0,\"cur\":null,\"item\":null}");
        setnum(m, "id", -1 - i);
        setstr(m, "type", jat(pc, 0)->valuestring);
        setnum(m, "x", numv(jat(pc, 1))); setnum(m, "y", numv(jat(pc, 2)));
        setstr(m, "dir", jat(pc, 3)->valuestring);
        if (jat(pc, 4) && !cJSON_IsNull(jat(pc, 4))) cJSON_AddItemToObject(m, "role", cJSON_Duplicate(jat(pc, 4), true));
        JFOR(kv, jat(pc, 5)) cJSON_AddItemToObject(m, kv->string, cJSON_Duplicate(kv, true));   // extra fields (a sorter's filter…)
        cJSON_AddItemToArray(ms, m);
        armForget(-1 - i);
        i++;
    }
}

static int queued(J *m) { int n = 0; JFOR(k, jget(m, "input")) n += numv(k); return n; }

static void ponderStep(void) {
    J *s = P.s;
    if (!s || P.t < jlen(jget(P.sc, "pieces")) * dropOf(P.sc)) return;
    JFOR(m, jget(s, "machines")) {
        J *role = jget(m, "role"), *out = childarr(m, "out");
        if (cJSON_IsArray(role) && jlen(out) < 2) {
            int fed = num(m, "fed") + 1;
            setnum(m, "fed", fed);
            cJSON_AddItemToArray(out, cJSON_CreateString(jat(role, fed % jlen(role))->valuestring));
        }
        if (cJSON_IsString(role) && !strcmp(role->valuestring, "sink") && jlen(out) > 3) cJSON_DeleteItemFromArray(out, 0);
    }
    J *hand = jget(P.sc, "hand");
    if (hand) {
        J *f = jat(jget(s, "machines"), 0);
        if (!str(f, "cur") && !queued(f)) insert(s, f, jat(hand, 0)->valuestring);
        if (!num(f, "fuel")) insert(s, f, jat(hand, 1)->valuestring);
    }
    int sold, items;
    step(s, NULL, &sold, &items);
}

static double ponderDrop(J *m) {   // m.id = -1 - index
    double a = (P.t + (num(m, "id") + 1) * dropOf(P.sc)) / 0.3;
    return a < 1 ? -jround((1 - a) * (1 - a) * 14) : 0;
}

// the canvas is scaled to the menu body: 2× at most, never wider than the body or taller than ~half the screen
#define SCENE_MAXW 290
#define SCENE_MAXH 120
double sceneScale(int w, int h) { return fmin(2, fmin(SCENE_MAXW / (w * 16.0), SCENE_MAXH / ((h + 1) * 16.0))); }

static void ponderDraw(int x, int y, int w, void *arg) {
    double dt = P.last < 0 ? 0 : gtime - P.last;
    P.last = gtime;
    J *sc = P.sc;
    int n = jlen(jget(sc, "pieces")), W = num(sc, "w"), H = num(sc, "h");
    double D = dropOf(sc);
    if (!P.s || (P.t += dt) > n * D + RUN) ponderReset();
    J *shown[64]; int ns = 0, i = 0;
    JFOR(m, jget(P.s, "machines")) { if (P.t >= i * D && ns < 64) shown[ns++] = m; i++; }
    double k = sceneScale(W, H);
    cv_save();
    cv_translate(x + (w - W * 16 * k) / 2, y);
    cv_scale(k, k);
    drawScene(0, 0, P.s, W, H, shown, ns, ponderDrop, NULL, 0);
    cv_restore();
    int placing = (int)fmin(n - 1, floor(P.t / D));
    const char *text = P.t < n * D && ns ? str(mdef(shown[placing < ns ? placing : ns - 1]), "name") : str(sc, "caption");
    if (text) text_draw("body", text, x + (w - text_width("body", text)) / 2, y + (H + 1) * 16 * k + 2, 0x3f2832ff, 0);
}

// id of a guide scene, or a scene object { w, h, pieces, caption } (blueprint previews; copied). Adds the canvas
// to the menu being built.
void scene_ponder(const char *id, J *sc) {
    if (!PONDER) PONDER = cJSON_Parse(PONDER_JSON);
    J *src = id ? jget(PONDER, id) : sc;
    if (!src) return;
    cJSON_Delete(P.sc); cJSON_Delete(P.s);
    P.sc = cJSON_Duplicate(src, true); P.s = NULL; P.t = 0; P.last = -1;
    scene_set(ponderStep);
    int W = num(P.sc, "w"), H = num(P.sc, "h");
    menu_canvas((H + 1) * 16 * sceneScale(W, H) + text_lineh("body") + 4, ponderDraw, NULL, 0);
}

// Draws a sandbox factory: checkered ground, then pieces like on the farm. The canvas is w*16 × (h+1)*16 with its
// top-left at (x, y) in the current transform (a spare row on top for tall sprites); scale with cv_scale first.
static J *sortSt;
static int cmpY(const void *a, const void *b) {
    double d = game_machineY(sortSt, *(J **)a) - game_machineY(sortSt, *(J **)b);
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}
void drawScene(int x, int y, J *s, int w, int h, J **shown, int nshown, double (*drop)(J *m), Tile *mark, int nmark) {
    lastDraw = gtime;
    J *all[128];
    if (!shown) { nshown = 0; JFOR(m, jget(s, "machines")) if (nshown < 128) all[nshown++] = m; shown = all; }
    cv_save();
    cv_translate(x, y + 16);
    for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) cv_fill(xx * 16, yy * 16, 16, 16, (xx + yy) % 2 ? 0x63c74dff : 0x5ab845ff);
    for (int i = 0; i < nmark; i++) cv_fill(mark[i].x * 16, mark[i].y * 16, 16, 16, css("rgba(38,43,68,0.18)"));
    for (int i = 0; i < nshown; i++) if (isFlat(shown[i])) { cv_save(); cv_translate(0, drop ? drop(shown[i]) : 0); game_drawFlatPiece(shown[i]); cv_restore(); }
    game_drawBeltItems(shown, nshown);
    J *solid[128]; int n = 0;
    for (int i = 0; i < nshown && n < 128; i++) if (!isFlat(shown[i])) solid[n++] = shown[i];
    // insertion sort: stable like Array.sort
    sortSt = s;
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && cmpY(&solid[j - 1], &solid[j]) > 0; j--) { J *t = solid[j]; solid[j] = solid[j - 1]; solid[j - 1] = t; }
    for (int i = 0; i < n; i++) { cv_save(); cv_translate(0, drop ? drop(solid[i]) : 0); drawMachine(solid[i]); cv_restore(); }
    cv_restore();
}
