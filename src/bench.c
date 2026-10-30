// Workshop screens (bench.js): the puzzle list and the bench (grid + pieces + Rodar). Rules are in workshop.c.
// The bench is a full bottom-screen overlay (hud_setOverlay): touch the grid / buttons, or D-pad cursor + A,
// L/R pick the piece, X Rodar/Parar, Y speed, B back.
#include <stdio.h>
#include "workshop.h"

static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

static const char *TIERS[] = { "", "First machines", "Production line", "Power!", "Logistics" };
static const char *starText(int n, char *b, int sz) {
    int o = 0; b[0] = 0;
    for (int i = 0; i < n; i++) o += snprintf(b + o, sz - o, ":star:");
    if (n < 3) snprintf(b + o, sz - o, " %d/3", n);
    return b;
}
static J *record(const char *id) {
    if (eq(id, "daily")) {
        char d[11]; today(d);
        J *dl = jget(state, "daily");
        return eq(str(dl, "date"), d) ? dl : NULL;
    }
    return jget(child(state, "workshop"), id);
}
static void ddmm(const char *date, char out[6]) { snprintf(out, 6, "%.2s/%.2s", date + 8, date + 5); }

const char *scoreText(J *sc, char *buf, int n) {
    int o = 0; buf[0] = 0;
    for (int i = 0; i < num(sc, "s"); i++) o += snprintf(buf + o, n - o, ":star:");
    snprintf(buf + o, n - o, " · %d steps · %d pieces", (int)num(sc, "t"), (int)num(sc, "p"));
    return buf;
}
J *myScore(J *s) {
    J *d = jget(s, "daily");
    char t[11]; today(t);
    if (!eq(str(d, "date"), t) || !num(d, "stars")) return NULL;
    J *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "s", num(d, "stars")); cJSON_AddNumberToObject(r, "t", num(d, "steps")); cJSON_AddNumberToObject(r, "p", num(d, "pieces"));
    return r;
}

// ---------- bench state ----------
static struct {
    J *pz, *layout, *s, *rival;     // owned
    int pzIndex;                    // PUZZLES index, -1 = daily
    bool running, fast;
    int steps, tool, cx, cy;
    double acc, doneT;
    const char *tools[8]; int ntools; // allow keys + "erase"
} B;
static J *bpLast;   // the blueprint handed to shops_blueprintMenu (valid until the next one)

static void bench(int index, J *rival);
static void solved(void);
static void solvedDaily(void);

static void reset(void) { B.running = false; B.steps = 0; cJSON_Delete(B.s); B.s = ws_build(B.pz, B.layout); }

void workshopMenu(void);
static bool rowDaily(void *a) { (void)a; playDaily(NULL); return false; }
static bool rowPuzzle(void *a) { bench(*(int *)a, NULL); return false; }
static bool rowMenu(void *a) { (void)a; workshopMenu(); return false; }

void workshopMenu(void) {
    int tier = factoryTier(state);
    char b[160], d[11], dm[6], sub[160];
    menu_begin(":wrench: Mike's Workshop");
    menu_text("Mike: \"The parts here are mine, so mess up all you want. Build the line, hit Run and see if it works.\"");
    today(d); ddmm(d, dm);
    snprintf(b, sizeof b, "Daily Challenge · %s", dm);
    J *my = myScore(state);
    if (my) scoreText(my, sub, sizeof sub); else snprintf(sub, sizeof sub, "Same for everyone today. Compare with friends!");
    cJSON_Delete(my);
    Row *r = menu_row(b, sub, rowDaily, NULL, 0); row_icon(r, "trophy"); row_stay(r);
    int i = 0;
    JFOR(p, table("PUZZLES")) {
        J *rec = record(str(p, "id"));
        bool open = tier >= num(p, "tier");
        char ic[48] = "lock";
        J *last = jget(p, "allow") ? cJSON_GetArrayItem(jget(p, "allow"), jlen(jget(p, "allow")) - 1) : NULL;
        if (open && last) snprintf(ic, sizeof ic, "item:machine_%s", last->string);
        snprintf(b, sizeof b, "%d. %s", i + 1, str(p, "name"));
        int t = num(p, "tier");
        if (!open) snprintf(sub, sizeof sub, "Unlocks at stage %d: %s", t, TIERS[t < 5 ? t : 0]);
        else if (rec) starText(num(rec, "stars"), sub, sizeof sub);
        else snprintf(sub, sizeof sub, "New!");
        r = menu_row(b, sub, open ? rowPuzzle : NULL, &i, sizeof i);
        row_icon(r, ic); row_stay(r);
        if (!open) row_disabled(r);
        i++;
    }
    menu_row("Close", NULL, NULL, NULL, 0);
    menu_end();
}

// Today's challenge; `rival` = a friend's score to beat (from a score link). Copied: the caller keeps its tree.
void playDaily(J *rival) {
    if (!factoryTier(state)) { toast("The Daily Challenge opens with Mike's Workshop (after the Coop)."); return; }
    bench(-1, rival);
}

// ---------- the bench screen ----------
#define SW 320
#define SH 240
#define INK 0x3f2832ff
#define CREAM 0xfff3dcff
static void nine(const char *src, int x, int y, int w, int h, int l, int t, int r, int b) {
    Img *im = I(src);
    int iw = im->w, ih = im->h, mw = iw - l - r, mh = ih - t - b;
    cv_image(im, 0, 0, l, t, x, y, l, t);
    cv_image(im, l, 0, mw, t, x + l, y, w - l - r, t);
    cv_image(im, iw - r, 0, r, t, x + w - r, y, r, t);
    cv_image(im, 0, t, l, mh, x, y + t, l, h - t - b);
    cv_image(im, l, t, mw, mh, x + l, y + t, w - l - r, h - t - b);
    cv_image(im, iw - r, t, r, mh, x + w - r, y + t, r, h - t - b);
    cv_image(im, 0, ih - b, l, b, x, y + h - b, l, b);
    cv_image(im, l, ih - b, mw, b, x + l, y + h - b, w - l - r, b);
    cv_image(im, iw - r, ih - b, r, b, x + w - r, y + h - b, r, b);
}
static bool tapped(int x, int y, int w, int h) { return in.touchDown && in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h; }
static bool button(const char *src, const char *label, int x, int y, int w, uint32_t color) {
    nine(src, x, y, w, 24, 5, 5, 5, 8);
    text_draw("px", label, x + (w - text_width("px", label)) / 2, y + 3, color, 0);
    return tapped(x, y, w, 24);
}
static double noDrop(J *m) { (void)m; return 0; }

static int grid(int *gx, int *gy) {   // scale and top-left of the scene (a spare row on top for tall sprites)
    int w = num(B.pz, "w"), h = num(B.pz, "h");
    int sc = w * 32 <= SW - 16 && (h + 1) * 32 <= 128 ? 2 : 1;
    *gx = (SW - w * 16 * sc) / 2; *gy = 28 + (128 - (h + 1) * 16 * sc) / 2;
    return sc;
}

static void act(int x, int y) {
    if (x < 0 || y < 0 || x >= num(B.pz, "w") || y >= num(B.pz, "h")) return;
    if (B.running) { toast("Stop the line to move pieces."); return; }
    J *hit = machineAt(B.s, x, y);
    const char *tool = B.tools[B.tool];
    if (hit && has(hit, "fixed")) { toast("That's Mike's piece: it stays put."); return; }
    if (hit) {
        int i = 0;
        JFOR(m, jget(B.s, "machines")) { if (m == hit) break; i += !has(m, "fixed"); }
        J *p = jat(B.layout, i);
        if (eq(tool, "erase")) cJSON_DeleteItemFromArray(B.layout, i);
        else cJSON_ReplaceItemInArray(p, 3, cJSON_CreateString(DIR_NAME[ROTATE[dirFrom(jat(p, 3)->valuestring)]]));
    } else if (eq(tool, "erase")) return;
    else if (ws_canPut(B.pz, B.s, B.layout, tool, x, y)) {
        J *p = cJSON_CreateArray();
        cJSON_AddItemToArray(p, cJSON_CreateString(tool));
        cJSON_AddItemToArray(p, cJSON_CreateNumber(x)); cJSON_AddItemToArray(p, cJSON_CreateNumber(y));
        cJSON_AddItemToArray(p, cJSON_CreateString("right"));
        cJSON_AddItemToArray(B.layout, p);
    } else {
        if (num(jget(B.pz, "allow"), tool) - ws_used(B.layout, tool)) toast("Doesn't fit there.");
        else toast("Out of %s pieces.", str(jget(table("MACHINES"), tool), "name"));
        return;
    }
    sfx("pop");
    reset();
}
static void runToggle(void) { if (B.running) reset(); else { reset(); B.running = true; } }

static bool benchFrame(double dt) {
    int w = num(B.pz, "w"), h = num(B.pz, "h");
    // the line runs one step per factory STEP (4 per STEP at 4x)
    B.acc += dt;
    while (B.acc >= STEP) {
        B.acc -= STEP;
        for (int i = 0; i < (B.fast ? 4 : 1) && B.running; i++) {
            ws_tick(B.s); B.steps++;
            WsGoal g[16]; bool done;
            ws_progress(B.pz, B.s, g, 16, &done);
            if (done) { B.running = false; B.doneT = 0.7; }
        }
    }
    if (B.doneT > 0 && (B.doneT -= dt) <= 0) { B.doneT = 0; if (B.pzIndex < 0) solvedDaily(); else solved(); return false; }

    // buttons
    if (in.down & BTN_B) { sfx("click"); workshopMenu(); return false; }
    if (in.down & BTN_LEFT) B.cx = B.cx > 0 ? B.cx - 1 : 0;
    if (in.down & BTN_RIGHT) B.cx = B.cx < w - 1 ? B.cx + 1 : w - 1;
    if (in.down & BTN_UP) B.cy = B.cy > 0 ? B.cy - 1 : 0;
    if (in.down & BTN_DOWN) B.cy = B.cy < h - 1 ? B.cy + 1 : h - 1;
    if (in.down & BTN_A) act(B.cx, B.cy);
    if (in.down & BTN_L) { B.tool = (B.tool + B.ntools - 1) % B.ntools; sfx("click"); }
    if (in.down & BTN_R) { B.tool = (B.tool + 1) % B.ntools; sfx("click"); }
    if (in.down & BTN_X) { sfx("click"); runToggle(); }
    if (in.down & BTN_Y) { sfx("click"); B.fast = !B.fast; }

    // header
    cv_fill(0, 0, SW, SH, 0x193c3eff);
    nine("assets/ui/btn.png", 4, 2, SW - 8, 24, 5, 5, 5, 8);
    char t[160];
    if (B.pzIndex < 0) snprintf(t, sizeof t, ":trophy: Daily Challenge · %s", str(B.pz, "name"));
    else snprintf(t, sizeof t, ":wrench: %s", str(B.pz, "name"));
    text_draw("px", t, 10, 5, INK, SW - 50);
    if (button("assets/ui/btn.png", "?", SW - 30, 2, 24, INK)) { sfx("click"); toast("%s", str(B.pz, "hint")); }

    // grid: the sandbox drawn like the farm, Mike's cells shaded, the D-pad cursor on top
    int gx, gy, sc = grid(&gx, &gy), cs = 16 * sc;
    J *shown[64]; int ns = 0;
    JFOR(m, jget(B.s, "machines")) if (ns < 64) shown[ns++] = m;
    Tile mark[64]; int nm = 0;
    JFOR(p, jget(B.pz, "fixed")) {
        int n = num(jget(table("MACHINES"), jat(p, 0)->valuestring), "size");
        for (int dy = 0; dy < n; dy++) for (int dx = 0; dx < n; dx++) if (nm < 64) mark[nm++] = (Tile){ numv(jat(p, 1)) + dx, numv(jat(p, 2)) + dy };
    }
    cv_save();
    cv_translate(gx, gy);
    cv_scale(sc, sc);
    drawScene(0, 0, B.s, w, h, shown, ns, noDrop, mark, nm);
    cv_restore();
    if (!B.running && (int)(gtime * 3) % 3) {
        int x = gx + B.cx * cs, y = gy + (B.cy + 1) * cs;
        uint32_t c = 0xfee761ff;
        cv_fill(x, y, cs, 2, c); cv_fill(x, y + cs - 2, cs, 2, c); cv_fill(x, y, 2, cs, c); cv_fill(x + cs - 2, y, 2, cs, c);
    }
    if (tapped(gx, gy + cs, w * cs, h * cs)) {
        B.cx = (in.tx - gx) / cs; B.cy = (in.ty - gy) / cs - 1;
        act(B.cx, B.cy);
    }

    // status: goals, pieces, steps (and the rival's score)
    {
        WsGoal g[16]; bool done;
        int n = ws_progress(B.pz, B.s, g, 16, &done), o = 0;
        char st[400];
        o += snprintf(st, sizeof st, "Goal: ");
        for (int i = 0; i < n; i++) {
            if (eq(g[i].item, "money")) o += snprintf(st + o, sizeof st - o, "%s:coin: %d/%d", i ? " · " : "", g[i].have, g[i].need);
            else o += snprintf(st + o, sizeof st - o, "%s%s %d/%d", i ? " · " : "", str(jget(table("ITEMS"), g[i].item), "name"), g[i].have, g[i].need);
        }
        o += snprintf(st + o, sizeof st - o, " · Pieces %d · Steps %d", jlen(B.layout), B.steps);
        if (B.rival) {
            char s[32] = ""; int k = 0;
            for (int i = 0; i < num(B.rival, "s"); i++) k += snprintf(s + k, sizeof s - k, ":star:");
            snprintf(st + o, sizeof st - o, " · %s: %s %d steps %d pieces", str(B.rival, "n") ? str(B.rival, "n") : "", s, (int)num(B.rival, "t"), (int)num(B.rival, "p"));
        }
        text_draw("small", st, 8, 158, CREAM, SW - 16);
    }

    // palette: the allowed pieces (with how many are left) and the eraser
    for (int i = 0; i < B.ntools; i++) {
        int x = 8 + i * 34, y = 180;
        const char *tl = B.tools[i];
        if (i == B.tool) nine("assets/ui/btn-gold-down.png", x, y, 30, 30, 5, 5, 5, 5);
        else nine("assets/ui/paper-small.png", x, y, 30, 30, 4, 4, 4, 4);
        char ic[48];
        if (eq(tl, "erase")) drawIcon("trash", x + 7, y + 7, 1);
        else {
            snprintf(ic, sizeof ic, "item:machine_%s", tl);
            drawIcon(ic, x + 7, y + 5, 1);
            char c[8]; snprintf(c, sizeof c, "%d", (int)num(jget(B.pz, "allow"), tl) - ws_used(B.layout, tl));
            text_draw("small", c, x + 28 - text_width("small", c), y + 19, INK, 0);
        }
        if (tapped(x, y, 30, 30)) { B.tool = i; sfx("click"); }
    }
    text_draw("small", "L/R piece · A place/rotate", 8 + B.ntools * 34 + 4, 186, 0xb9a58aff, SW - (8 + B.ntools * 34 + 12));

    // controls
    int y = 214;
    if (button("assets/ui/btn-red.png", B.running ? "Stop" : "Run", 8, y, 70, CREAM)) { sfx("click"); runToggle(); }
    if (button("assets/ui/btn.png", B.fast ? "Speed 4x" : "Speed 1x", 82, y, 70, INK)) { sfx("click"); B.fast = !B.fast; }
    if (button("assets/ui/btn.png", "Clear", 156, y, 70, INK)) { sfx("click"); cJSON_Delete(B.layout); B.layout = cJSON_CreateArray(); reset(); }
    if (button("assets/ui/btn-down.png", "Back", 230, y, 82, INK)) { sfx("click"); workshopMenu(); return false; }
    return true;
}

// index: PUZZLES index, -1 = today's challenge
static void bench(int index, J *rival) {
    cJSON_Delete(B.pz); cJSON_Delete(B.layout); cJSON_Delete(B.s);
    if (rival != B.rival) { cJSON_Delete(B.rival); B.rival = rival ? cJSON_Duplicate(rival, true) : NULL; }
    if (index < 0) { char d[11]; today(d); B.pz = ws_daily(d); }
    else B.pz = cJSON_Duplicate(jat(table("PUZZLES"), index), true);
    B.pzIndex = index;
    J *rec = record(str(B.pz, "id")), *lay = jget(rec, "layout");
    B.layout = lay ? cJSON_Duplicate(lay, true) : cJSON_CreateArray();
    B.s = NULL;
    B.fast = false; B.acc = 0; B.doneT = 0; B.tool = 0; B.cx = B.cy = 0;
    B.ntools = 0;
    JFOR(a, jget(B.pz, "allow")) if (B.ntools < 7) B.tools[B.ntools++] = a->string;
    B.tools[B.ntools++] = "erase";
    reset();
    hud_closeMenu();
    hud_setOverlay(benchFrame);
}

// ---------- results ----------
static bool rowNext(void *a) { bench(*(int *)a, NULL); return false; }
static bool rowRetry(void *a) { (void)a; bench(B.pzIndex, B.rival); return false; }
static bool rowShare(void *a) { (void)a; shops_shareScore(); return false; }
static bool rowFriends(void *a) { (void)a; shops_friendsMenu(); return false; }
static bool rowBlueprint(void *a) {
    (void)a;
    cJSON_Delete(bpLast);
    bpLast = cJSON_CreateObject();
    char n[160]; snprintf(n, sizeof n, "Workshop: %s", str(B.pz, "name"));
    cJSON_AddStringToObject(bpLast, "name", n);
    J *ps = cJSON_AddArrayToObject(bpLast, "pieces");
    JFOR(p, jget(B.pz, "fixed")) {
        J *c = cJSON_CreateArray();
        for (int i = 0; i < 4; i++) cJSON_AddItemToArray(c, cJSON_Duplicate(jat(p, i), true));
        cJSON_AddItemToArray(ps, c);
    }
    JFOR(p, B.layout) cJSON_AddItemToArray(ps, cJSON_Duplicate(p, true));
    shops_blueprintMenu(bpLast, false);
    return false;
}
static void asBlueprint(void) { Row *r = menu_row("Save as blueprint", "to build this line on the farm", rowBlueprint, NULL, 0); row_icon(r, "book"); row_stay(r); }

static void resultRows(int n, int pieces, int steps) {
    char b[96], s[64];
    J *par = jget(B.pz, "par");
    int o = 0;
    for (int i = 0; i < n; i++) o += snprintf(s + o, sizeof s - o, ":star:");
    // ponytail: the web draws the missing stars dimmed; here they're just left out
    menu_text("%s", s);
    Row *r = menu_row("Goal met", NULL, NULL, NULL, 0); row_icon(r, "check"); row_disabled(r);
    snprintf(b, sizeof b, "%d pieces", pieces); snprintf(s, sizeof s, "for a star: max %d", (int)num(par, "pieces"));
    r = menu_row(b, s, NULL, NULL, 0); row_icon(r, pieces <= num(par, "pieces") ? "check" : "heart_empty"); row_disabled(r);
    snprintf(b, sizeof b, "%d steps", steps); snprintf(s, sizeof s, "for a star: max %d", (int)num(par, "steps"));
    r = menu_row(b, s, NULL, NULL, 0); row_icon(r, steps <= num(par, "steps") ? "check" : "heart_empty"); row_disabled(r);
}

// Result: stars, best score and the one-time rewards (tokens on the first solve, a ticket on the first ★★★).
static void solved(void) {
    J *st = state;
    int pieces = jlen(B.layout), steps = B.steps, n = ws_stars(B.pz, pieces, steps);
    const char *id = str(B.pz, "id");
    J *old = record(id);
    char rw[2][64]; int nr = 0;
    if (!num(old, "stars")) { setnum(st, "tokens", num(st, "tokens") + 3); snprintf(rw[nr++], 64, "3 tokens"); }
    if (n == 3 && !has(old, "got3")) { setnum(st, "tickets", num(st, "tickets") + 1); snprintf(rw[nr++], 64, ":ticket: 1 capsule ticket"); }
    J *rec = cJSON_CreateObject();
    cJSON_AddNumberToObject(rec, "stars", fmax(n, num(old, "stars")));
    cJSON_AddBoolToObject(rec, "got3", has(old, "got3") || n == 3);
    cJSON_AddNumberToObject(rec, "pieces", cJSON_IsNumber(jget(old, "pieces")) ? fmin(pieces, num(old, "pieces")) : pieces);
    cJSON_AddNumberToObject(rec, "steps", cJSON_IsNumber(jget(old, "steps")) ? fmin(steps, num(old, "steps")) : steps);
    cJSON_AddItemToObject(rec, "layout", cJSON_Duplicate(B.layout, true));
    J *ws = child(st, "workshop");
    delkey(ws, id);
    cJSON_AddItemToObject(ws, id, rec);
    setnum(child(st, "stats"), "workshop", jlen(ws));
    sfx(n == 3 ? "tada" : "coin");
    changed();
    menu_begin("Line done!");
    resultRows(n, pieces, steps);
    if (nr) menu_text("Reward: %s%s%s", rw[0], nr > 1 ? " and " : "", nr > 1 ? rw[1] : "");
    int next = B.pzIndex + 1;
    J *np = jat(table("PUZZLES"), next);
    if (np && factoryTier(st) >= num(np, "tier")) {
        char b[96]; snprintf(b, sizeof b, "Next: %s", str(np, "name"));
        row_stay(menu_row(b, NULL, rowNext, &next, sizeof next));
    }
    asBlueprint();
    row_stay(menu_row("Try again", NULL, rowRetry, NULL, 0));
    row_stay(menu_row("Back", NULL, rowMenu, NULL, 0));
    menu_end();
}

// Daily result: keeps today's best, 3 tokens for the first solve of the day (+2 for the first ★★★), and the share button.
static void solvedDaily(void) {
    J *st = state;
    int pieces = jlen(B.layout), steps = B.steps, n = ws_stars(B.pz, pieces, steps);
    J *sc = cJSON_CreateObject();
    cJSON_AddNumberToObject(sc, "s", n); cJSON_AddNumberToObject(sc, "t", steps); cJSON_AddNumberToObject(sc, "p", pieces);
    J *rec = record("daily"), *old = num(rec, "stars") ? cJSON_Duplicate(rec, true) : NULL;   // a saved layout alone isn't a score
    char rw[2][64]; int nr = 0;
    if (!old) { setnum(st, "tokens", num(st, "tokens") + 3); snprintf(rw[nr++], 64, "3 tokens"); }
    if (n == 3 && !has(old, "got3")) { setnum(st, "tokens", num(st, "tokens") + 2); snprintf(rw[nr++], 64, "+2 tokens for 3 stars"); }
    bool best = true;
    if (old) {
        J *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "s", num(old, "stars")); cJSON_AddNumberToObject(o, "t", num(old, "steps")); cJSON_AddNumberToObject(o, "p", num(old, "pieces"));
        best = better(sc, o);
        cJSON_Delete(o);
    }
    J *d;
    if (best) {
        d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "date", str(B.pz, "date"));
        cJSON_AddNumberToObject(d, "stars", n); cJSON_AddNumberToObject(d, "steps", steps); cJSON_AddNumberToObject(d, "pieces", pieces);
        cJSON_AddItemToObject(d, "layout", cJSON_Duplicate(B.layout, true));
        cJSON_AddBoolToObject(d, "got3", has(old, "got3") || n == 3);
    } else { d = cJSON_Duplicate(old, true); setbool(d, "got3", has(old, "got3") || n == 3); }
    delkey(st, "daily");
    cJSON_AddItemToObject(st, "daily", d);
    sfx(n == 3 ? "tada" : "coin");
    changed();
    char t[64], dm[6];
    ddmm(str(B.pz, "date"), dm);
    snprintf(t, sizeof t, "Daily Challenge · %s", dm);
    menu_begin(t);
    resultRows(n, pieces, steps);
    if (B.rival) {
        const char *rn = str(B.rival, "n") ? str(B.rival, "n") : "";
        if (better(sc, B.rival)) menu_text("You beat %s!", rn); else menu_text("%s is still ahead.", rn);
    }
    if (old && !best) { J *my = myScore(st); char b[96]; menu_text("Your best today is still: %s", my ? scoreText(my, b, sizeof b) : ""); cJSON_Delete(my); }
    if (nr) menu_text("Reward: %s%s%s", rw[0], nr > 1 ? " and " : "", nr > 1 ? rw[1] : "");
    Row *r = menu_row("Send score", "challenge a friend to beat it", rowShare, NULL, 0); row_icon(r, "gift"); row_stay(r);
    asBlueprint();
    row_stay(menu_row("Try again", NULL, rowRetry, NULL, 0));
    row_stay(menu_row("Friends", NULL, rowFriends, NULL, 0));
    menu_end();
    cJSON_Delete(old); cJSON_Delete(sc);
}
