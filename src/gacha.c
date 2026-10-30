// gacha.js: the capsule machine scene, a full-screen bottom-screen overlay. The machine is drawn pixel by pixel
// (no anti-aliased shapes) into a small 144×180 canvas shown at 1×, like the web's integer-scaled canvas.
// Flow: idle → coin → crank → drop → wait (tap) → shake → open → reveal → card
// The DOM bits (prompt, odds, result card, close button) are drawn beside it: landscape layout of style.css.
#include <stdio.h>
#include "api.h"
#include "audio.h"


void hud_nine(const char *src, int x, int y, int w, int h, int l, int t, int r, int b);   // hud.c

#define W 144
#define H 180
// palette (C in gacha.js)
#define Ck 0x2a2440ff
#define Cb 0x5d6aa6ff
#define Cd 0x47528aff
#define Cdd 0x353e6eff
#define Cm 0x7fe6c8ff
#define Cw 0xf4e9f8ff
#define Cv 0xdcc8eaff
#define Cr 0xc0285eff
#define Cp 0xf0cde0ff
#define Cpd 0xd9a8c6ff
#define Cy 0xf2c14eff
#define Ct 0x3fc1b0ff
#define Co 0xf09a36ff
#define Cg 0xcfe3a6ff
#define Cc 0x6b3d8fff
#define CAP 0xeef6f8ff
#define SHADOW 0x2a24402e   // rgba(42,36,64,0.18)
#define WHITE 0xffffffff
// UI (style.css)
#define INK 0x3f2832ff
#define BROWN 0x743f39ff
#define RED 0xe43b44ff
#define GREEN 0x3e8948ff
#define TANHI 0xead4aaff
#define CREAM 0xfff3dcff

static const struct { int cx; uint32_t col; } SECTION[3] = { { 44, Ct }, { 72, Cr }, { 100, Cy } };
#define CRANK_X 90
#define CRANK_Y 112
#define CHUTE_X 48
#define CHUTE_Y 136
#define FLOOR 172
#define CAP_R 9

enum { IDLE, COIN, CRANK, DROP, WAIT, SHAKE, OPEN, REVEAL, CARD };
static const double DUR[] = { 0, 0.55, 1.0, 1.7, 0, 0.6, 0.45, 1.3, 0 };
static const int NEXT[] = { 0, CRANK, DROP, WAIT, 0, OPEN, REVEAL, CARD, 0 };

typedef struct { double x, y, vx, vy, life; uint32_t color; } Particle;
static struct {
    int phase; double t, time;
    J *result;                   // buyCapsule's { critter, isNew, price }
    Particle p[400]; int np;
    const char *msg; double msgT;
    bool air, rolled;
    double cardT, scroll, drag0; int touchY0; bool dragging;
    char bonusText[160];
} scene;
static char setRef[16];
static bool hasSet;
static Img *G;                    // the 144×180 machine canvas
static Img *cardImg; static bool cardDirty; static int cardH, btnMascotY, btnAgainY;   // the result card, laid out offscreen

static double lerp(double a, double b, double k) { return a + (b - a) * k; }
static double easeOut(double k) { return 1 - pow(1 - k, 3); }
static double easeInOut(double k) { return k < 0.5 ? 4 * k * k * k : 1 - pow(-2 * k + 2, 3) / 2; }

static J *critter(void) { return jget(scene.result, "critter"); }
static const char *rarityId(void) { return str(critter(), "rarity"); }
static J *rarity(void) { return jget(table("RARITY"), rarityId()); }
static int price(void) { return hasSet ? num(DATA, "FESTIVAL_CAPSULE_PRICE") : capsulePrice(state); }
static void onChange(void) { changed(); persist(); }

// ---------- pixel primitives (straight into the canvas) ----------
// n / 255 truncated toward zero, exact for |n| ≤ 65k (ARM11 has no divide instruction)
static inline int div255(int n) { int m = n < 0 ? -n : n, q = (m + 1 + (m >> 8)) >> 8; return n < 0 ? -q : q; }
static void blend(int x, int y, uint32_t c, double a) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint8_t *d = G->px + (y * W + x) * 4;
    int sa = (int)((c & 255) * a + 0.5);
    if (sa <= 0) return;
    if (d[3] == 255) {   // opaque destination (the machine): integer lerp
        for (int i = 0; i < 3; i++) { int sc = (c >> (24 - 8 * i)) & 255; d[i] += div255((sc - d[i]) * sa + 127); }
        return;
    }
    double s = sa / 255.0, da = d[3] / 255.0, oa = s + da * (1 - s);
    for (int i = 0; i < 3; i++) {
        double sc = (c >> (24 - 8 * i)) & 255;
        d[i] = (uint8_t)((sc * s + d[i] * da * (1 - s)) / oa + 0.5);
    }
    d[3] = (uint8_t)(oa * 255 + 0.5);
}
static void Ra(double x, double y, int w, int h, uint32_t col, double a) {
    int x0 = jround(x), y0 = jround(y);
    if (a >= 1 && (col & 255) == 255) {   // opaque: plain stores (most of the machine), clipped
        int xa = x0 < 0 ? 0 : x0, xb = x0 + w > W ? W : x0 + w, ya = y0 < 0 ? 0 : y0, yb = y0 + h > H ? H : y0 + h;
        uint32_t v = __builtin_bswap32(col);
        for (int y = ya; y < yb; y++) { uint32_t *d = (uint32_t *)G->px + y * W; for (int x = xa; x < xb; x++) d[x] = v; }
        return;
    }
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) blend(x0 + i, y0 + j, col, a);
}
static void R(double x, double y, int w, int h, uint32_t col) { Ra(x, y, w, h, col, 1); }

// Filled pixel ellipse, row by row.
static void disc(double cxd, double cyd, int rx, uint32_t col, int ry) {
    if (ry <= 0) return;   // JS: 0/0 = NaN → fillRect draws nothing
    int cx = jround(cxd), cy = jround(cyd);
    for (int dy = -ry; dy <= ry; dy++) {
        int dx = jround(rx * sqrt(fmax(0, 1 - (double)dy / ry * dy / ry)));
        R(cx - dx, cy + dy, dx * 2 + 1, 1, col);
    }
}
#define DISC(cx, cy, r, col) disc(cx, cy, r, col, r)

// Outlined box: 1px ink border around a fill.
static void box(double x0, double y0, double x1, double y1, uint32_t fill) {
    R(x0, y0, x1 - x0, y1 - y0, Ck);
    R(x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2, fill);
}

// Thick pixel line by stamping squares along it.
static void line(double x0, double y0, double x1, double y1, int thick, uint32_t col) {
    int n = ceil(hypot(x1 - x0, y1 - y0)), h = thick / 2;
    for (int i = 0; i <= n; i++) R(x0 + (x1 - x0) * i / n - h, y0 + (y1 - y0) * i / n - h, thick, thick, col);
}

static uint32_t shade(uint32_t col) { return col == Ct ? 0x2c9688ff : col == Cr ? 0x8e1d47ff : col == Cy ? 0xc9952fff : Ck; }

// Two-tone capsule: light top, colored bottom, ink rim, split along `rot`.
// open > 0 lifts and tilts the top half away.
static void capsule(double cxd, double cyd, int r, uint32_t col, double rot, double open) {
    int cx = jround(cxd), cy = jround(cyd);
    typedef struct { int x, y; double a; int only; } Half;   // only: -1 both, 0 bottom, 1 top
    Half halves[2] = { { cx, cy, rot, open ? 0 : -1 }, { jround(cx + open * 6), jround(cy - open * 18), rot - open * 0.8, 1 } };
    int nh = open ? 2 : 1;
    for (int k = 0; k < nh; k++) {
        double ux = sin(halves[k].a), uy = -cos(halves[k].a);
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
            double dist = hypot(dx, dy);
            if (dist > r + 0.3) continue;
            bool top = dx * ux + dy * uy > 0;
            if (halves[k].only == 1 && !top) continue;
            if (halves[k].only == 0 && top) continue;
            uint32_t c2 = top ? CAP : col;
            if (dist > r - 0.9) c2 = Ck;
            else if (top && dx < -r * 0.25 && dy < -r * 0.25 && dist > r * 0.45) c2 = WHITE;
            else if (!top && fabs(dx * ux + dy * uy) < 1) c2 = shade(col);
            R(halves[k].x + dx, halves[k].y + dy, 1, 1, c2);
        }
    }
}

static uint32_t capsuleColor(void) { const char *r = rarityId(); return !strcmp(r, "rare") ? Cr : !strcmp(r, "legendary") ? Cy : Ct; }

// ---------- scene ----------
static void burst(double x, double y, int n, uint32_t color) {
    for (int i = 0; i < n && scene.np < 400; i++) {
        double a = frand() * M_PI * 2, s = 20 + frand() * 60;
        scene.p[scene.np++] = (Particle){ x, y, cos(a) * s, sin(a) * s - 30, 0.6 + frand() * 0.8, color };
    }
}

static bool dimOk;
static void reset(void) {
    dimOk = false;   // the dimmed cache holds the last capsule's color
    if (scene.result) cJSON_Delete(scene.result);
    memset(&scene, 0, sizeof scene);
}

static void go(int phase) { scene.phase = phase; scene.t = 0; }

static void showCard(void) {
    snprintf(scene.bonusText, sizeof scene.bonusText, "As mascot: %s", str(table("BONUSES"), str(critter(), "bonus")));
    cardDirty = true; scene.cardT = 0; scene.scroll = 0;
}

static void start(void) {
    J *res = buyCapsule(state, NULL, hasSet ? setRef : NULL);
    if (!res) { scene.msg = "Not enough coins!"; scene.msgT = 1.5; sfx("miss"); return; }
    onChange();
    scene.result = res;
    go(COIN);
    sfx("coin");
}

static void onTap(void) {
    if (scene.phase == IDLE) start();
    else if (scene.phase == WAIT) { go(SHAKE); sfx("rattle"); }
}

typedef struct { double x, y, rot; } Pos;
static Pos capsulePos(void) {
    double t = scene.t, rest = FLOOR - CAP_R;
    if (scene.phase == DROP) {
        double k = t / DUR[DROP];
        if (k < 0.15) return (Pos){ CHUTE_X, lerp(CHUTE_Y, CHUTE_Y + 8, k / 0.15), 0 };
        double f = (k - 0.15) / 0.85;
        double bounce = fabs(cos(f * M_PI * 3.5)) * 34 * (1 - f) * (1 - f);
        double x = lerp(CHUTE_X, 72, easeOut(f));
        return (Pos){ x, fmin(rest, lerp(CHUTE_Y + 8, rest, fmin(1, f * 4))) - bounce, (x - CHUTE_X) / CAP_R };
    }
    if (scene.phase == WAIT) return (Pos){ 72, rest, (72.0 - CHUTE_X) / CAP_R + jround(sin(scene.time * 4) * 2) * 0.06 };
    if (scene.phase == SHAKE) return (Pos){ 72 + jround(sin(t * 60) * 2 * (1 - t / DUR[SHAKE])), rest, sin(t * 50) * 0.25 };
    return (Pos){ 72, rest, 0 };
}

static void update(double dt) {
    scene.t += dt; scene.time += dt; scene.msgT -= dt; scene.cardT += dt;
    double d = DUR[scene.phase];
    if (d && scene.t >= d) {
        int next = NEXT[scene.phase];
        go(next);
        if (next == CRANK) sfxOpt("crank", DUR[CRANK], NULL);
        if (next == DROP) { sfx("clunk"); scene.air = false; scene.rolled = false; }
        if (next == OPEN) { burst(72, FLOOR - CAP_R, 18, capsuleColor()); sfx("pu"); }
        if (next == REVEAL) sfxOpt("tada", 0, rarityId());
        if (next == CARD) showCard();
    }
    // capsule sounds follow the animation: a "blub" each time it lands, then a little roll
    if (scene.phase == DROP) {
        double k = scene.t / DUR[DROP], y = capsulePos().y, rest = FLOOR - CAP_R;
        if (y < rest - 2) scene.air = true;
        else if (scene.air && y >= rest - 0.5) { scene.air = false; sfxOpt("bounce", fmax(0.25, 1 - k), NULL); }
        if (k > 0.72 && !scene.rolled) { scene.rolled = true; sfxOpt("roll", 0.45, NULL); }
    }
    if (scene.phase == REVEAL && frand() < 0.05) sfx("twinkle");
    if (scene.phase == REVEAL && frand() < 0.5) burst(72 + (frand() - 0.5) * 70, 90 + (frand() - 0.5) * 70, 1, 0xfff6c8ff);
    int n = 0;
    for (int i = 0; i < scene.np; i++) {
        Particle *p = &scene.p[i];
        p->x += p->vx * dt; p->y += p->vy * dt; p->vy += 40 * dt; p->life -= dt;
        if (p->life > 0) scene.p[n++] = *p;
    }
    scene.np = n;
}

// ---------- scene drawing ----------
static void drawGachaMachine(int sx) {
    int x0 = 28 + sx, x1 = 116 + sx;
    disc(72, 158, 50, SHADOW, 4);   // floor shadow

    // top cap with mint highlights
    box(x0 + 2, 6, x1 - 2, 19, Cb);
    R(x0 + 6, 9, 14, 2, Cm); R(x0 + 22, 9, 3, 2, Cm); R(x1 - 30, 9, 22, 3, Cm); R(x1 - 12, 12, 6, 1, Cm);
    R(x0 + 3, 17, x1 - x0 - 6, 1, Cd);

    // window: lavender glass, 3 sections with price dots and capsules
    box(x0, 18, x1, 71, Cw);
    R(x0 + 1, 58, x1 - x0 - 2, 12, Cv);
    R(x0 + 4, 22, 10, 1, WHITE); R(x0 + 4, 23, 4, 1, WHITE);
    int jig = scene.phase == CRANK ? 1 : 0;
    static const int CAPS[4][2] = { { -7, 61 }, { 6, 61 }, { -1, 50 }, { 8, 49 } };
    static const double ROTS[4] = { 0.3, -0.4, 0, M_PI };
    for (int i = 0; i < 3; i++) {
        int cx = SECTION[i].cx + sx;
        DISC(cx, 27, 4, Ck); DISC(cx, 27, 3, SECTION[i].col); R(cx - 1, 25, 1, 1, WHITE);
        for (int j = 0; j < 4; j++) {
            double jx = jig * jround(sin(scene.time * 40 + i * 3 + j)), jy = jig * jround(cos(scene.time * 31 + j * 2));
            capsule(cx + CAPS[j][0] + jx, CAPS[j][1] + jy, 6, SECTION[i].col, ROTS[(i + j) % 4], 0);
        }
    }
    for (int dx = 58; dx <= 86; dx += 28) { R(dx + sx, 19, 1, 51, Cv); R(dx + 1 + sx, 19, 1, 51, WHITE); }

    // body
    box(x0 + 2, 70, x1 - 2, 151, Cb);
    R(x0 + 3, 71, x1 - x0 - 6, 2, Cm);
    R(x0 + 3, 143, x1 - x0 - 6, 7, Cd);
    R(x0 + 8, 144, 18, 2, Cm); R(x1 - 30, 144, 20, 2, Cm);

    // toy tray with pink label
    R(x0 + 10, 80, 24, 5, Cp); R(x0 + 12, 82, 20, 1, Cpd);
    box(x0 + 8, 87, x0 + 36, 99, Cd);
    R(x0 + 9, 88, 26, 2, Cm);
    R(x0 + 21, 91, 3, 4, Co); R(x0 + 19, 94, 7, 1, Co); R(x0 + 20, 95, 5, 1, Co); R(x0 + 21, 96, 3, 1, Co);

    // display + coin slot + refund
    box(x0 + 46, 77, x0 + 72, 87, Cg);
    R(x0 + 47, 78, 24, 1, 0xe8f2cfff);
    DISC(x0 + 80, 82, 6, Ck); DISC(x0 + 80, 82, 5, Cc); R(x0 + 80, 78, 1, 9, Ck);
    box(x0 + 82, 96, x0 + 86, 116, Cp);

    // crank: magenta ring, orange arrows, pink disc, rotating handle
    int cx = CRANK_X + sx, cy = CRANK_Y;
    DISC(cx, cy, 16, Ck);
    DISC(cx, cy, 15, Cr);
    R(cx - 12, cy - 10, 5, 2, Co); R(cx + 8, cy + 9, 5, 2, Co);
    DISC(cx, cy, 11, Ck);
    DISC(cx, cy, 10, Cp);
    DISC(cx, cy, 7, Cpd);
    double a = (scene.phase == CRANK ? easeInOut(scene.t / DUR[CRANK]) * M_PI * 2 : 0) - M_PI / 4;
    double hx = cos(a) * 8, hy = sin(a) * 8;
    line(cx - hx, cy - hy, cx + hx, cy + hy, 5, Ck);
    line(cx - hx, cy - hy, cx + hx, cy + hy, 3, Cw);

    // chute (capsule exit)
    box(x0 + 8, 120, x0 + 34, 146, Ck);
    R(x0 + 9, 121, 24, 24, Cdd);
    R(x0 + 9, 121, 24, 3, 0x262c52ff);
    R(x0 + 10, 140, 22, 4, Cd);

    // feet
    R(x0 + 4, 151, 10, 4, Ck); R(x1 - 14, 151, 10, 4, Ck);
}

static void drawCoin(double k) {
    double x = lerp(10, 108, easeOut(k)), y = lerp(176, 82, easeOut(k)) - sin(k * M_PI) * 26;
    int rx = fmax(1, jround(5 * fabs(cos(k * 14))));
    disc(x, y, rx, Ck, 5);
    disc(x, y, fmax(0, rx - 1), Cy, 4);
}

static void drawCapsuleOut(void) {
    int ph = scene.phase;
    if (ph < DROP) return;
    Pos p = capsulePos();
    double open = ph == OPEN ? easeOut(scene.t / DUR[OPEN]) : ph == REVEAL || ph == CARD ? 1 : 0;
    disc(p.x, FLOOR + 1, 9, SHADOW, 2);
    capsule(p.x, p.y, CAP_R, capsuleColor(), open ? 0 : p.rot, open);
    if (ph == OPEN && scene.t < DUR[OPEN] / 2) for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) blend(x, y, 0xfffae6ff, 0.7);
}

// The machine dims, then hard-edged rays in the rarity color turn behind the critter (two brightness steps).
static double glowK(void) { return scene.phase == REVEAL ? fmin(1, scene.t / 0.35) : 1; }
static void dimMachine(void) {
    // source-atop: dim only the machine, not the empty canvas
    int a = (int)(0.6 * glowK() * 256);
    for (int i = 0; i < W * H; i++) {
        uint8_t *d = G->px + i * 4;
        if (!d[3]) continue;
        d[0] += ((42 - d[0]) * a) >> 8; d[1] += ((36 - d[1]) * a) >> 8; d[2] += ((64 - d[2]) * a) >> 8;
    }
}
static void drawRays(void) {
    uint32_t col = css(str(rarity(), "color"));
    double k = glowK();
    // rays: each 2x2 block's angle (1/65536 of a ray width, π/10) and squared distance never change: integers only
    static int ang[H / 2][W / 2], d2[H / 2][W / 2]; static bool ready;
    if (!ready) { for (int y = 0; y < H; y += 2) for (int x = 0; x < W; x += 2) { ang[y / 2][x / 2] = (int)floor(atan2(y - 88, x - 72) / (M_PI / 10) * 65536); d2[y / 2][x / 2] = (x - 72) * (x - 72) + (y - 88) * (y - 88); } ready = true; }
    int rot = (int)fmod(floor(scene.time * 0.6 / (M_PI / 10) * 65536), 2 * 65536);   // only the parity matters
    double r0 = 72 * k;
    int r2 = (int)floor(r0 * r0), in2 = (int)ceil(r0 * 0.55 * r0 * 0.55);   // d2 ≤ r0², d2 ≥ (0.55·r0)²
    int sa[2] = { (int)((col & 255) * 0.6 + 0.5), (int)((col & 255) * 0.3 + 0.5) }, sc[3] = { col >> 24, (col >> 16) & 255, (col >> 8) & 255 };
    int y0 = (int)fmax(0, 88 - r0) & ~1, y1 = fmin(H, 88 + r0 + 2), x0 = (int)fmax(0, 72 - r0) & ~1, x1 = fmin(W, 72 + r0 + 2);
    for (int y = y0; y < y1; y += 2) for (int x = x0; x < x1; x += 2) {
        int dd = d2[y / 2][x / 2];
        if (dd > r2 || (((ang[y / 2][x / 2] + rot) >> 16) & 1)) continue;
        int a = sa[dd >= in2];
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
            uint8_t *d = G->px + ((y + j) * W + x + i) * 4;
            if (d[3] == 255) for (int c = 0; c < 3; c++) d[c] += div255((sc[c] - d[c]) * a + 127);
            else blend(x + i, y + j, col, a / (double)(col & 255));
        }
    }
    DISC(72, 88, jround(36 * k), 0xfff3dc2e);   // rgba(255,243,220,0.18)
    DISC(72, 88, jround(26 * k), 0xfff3dc40);   // 0.25
    DISC(72, 88, jround(16 * k), 0xffffff59);   // 0.35
}

static void drawCritter(void) {
    J *c = critter();
    Img *spr = I(str(c, "sprite"));
    if (!spr) return;
    double k = scene.phase == REVEAL ? easeOut(fmin(1, scene.t / DUR[REVEAL])) : 1;
    int bob = scene.phase == CARD ? jround(sin(scene.time * 2.5) * 2) : 0;
    int y = jround(lerp(FLOOR - 12, 88, k)) + bob;
    int frames = num(c, "frames") ? num(c, "frames") : 2, size = num(c, "size") ? num(c, "size") : 32;
    int s = fmax(1, jround(lerp(1, fmin(4, 96.0 / size), k)));   // integer scale only
    int f = (int)floor(scene.time * 4) % frames, dw = size * s;
    // nearest-neighbor upscale of each frame done once per scale (the soft rasterizer's scaled path is slow on 3DS)
    static Img *cache[8]; static const Img *cSpr; static int cS;
    if (cSpr != spr || cS != s) { for (int i = 0; i < 8; i++) { img_free(cache[i]); cache[i] = NULL; } cSpr = spr; cS = s; }
    if (f < 8 && !cache[f]) {
        Img *im = cache[f] = img_new(dw, dw);
        for (int yy = 0; yy < dw; yy++) for (int xx = 0; xx < dw; xx++) *img_at(im, xx, yy) = *img_at(spr, f * size + xx / s, yy / s);
    }
    cv_push_target(G);
    if (f < 8) cv_draw(cache[f], jround(72 - dw / 2.0), jround(y - dw / 2.0));
    else cv_image(spr, f * size, 0, size, size, jround(72 - dw / 2.0), jround(y - dw / 2.0), dw, dw);
    cv_pop_target();
}

// The machine and, once the reveal glow is full, the dimmed machine with the open capsule never change: they are
// drawn once and copied (the 3DS can't afford the pixel-by-pixel redraw every frame).
static uint8_t baseCache[W * H * 4], dimCache[W * H * 4];
static bool baseOk;
static void drawGacha(void) {
    int ph = scene.phase;
    bool full = (ph == REVEAL && glowK() >= 1) || ph == CARD;
    if (full && dimOk) memcpy(G->px, dimCache, sizeof dimCache);
    else {
        if (ph == CRANK) { memset(G->px, 0, W * H * 4); drawGachaMachine(jround(sin(scene.time * 50))); }
        else if (baseOk) memcpy(G->px, baseCache, sizeof baseCache);
        else { memset(G->px, 0, W * H * 4); drawGachaMachine(0); memcpy(baseCache, G->px, sizeof baseCache); baseOk = true; }
        if (ph == COIN) drawCoin(scene.t / DUR[COIN]);
        drawCapsuleOut();
        if (ph == REVEAL || ph == CARD) dimMachine();
        if (full) { memcpy(dimCache, G->px, sizeof dimCache); dimOk = true; }
    }
    if (ph == REVEAL || ph == CARD) drawRays();
    for (int i = 0; i < scene.np; i++) {
        Particle *p = &scene.p[i];
        int s = p->life > 0.4 ? 2 : 1;
        R(p->x - s, p->y, s * 2 + 1, 1, p->color);
        R(p->x, p->y - s, 1, s * 2 + 1, p->color);
    }
    if (ph == REVEAL || ph == CARD) drawCritter();
    img_touch(G);
}

// ---------- the DOM around the canvas ----------
#define CX 4                // canvas on the left, 1×
#define CY 30
#define PX 152              // right panel: prompt, odds, card
#define PW 164
#define CARD_Y 32
#define CARD_MAXH (236 - CARD_Y)
#define CLOSE_X (320 - 28)
#define CLOSE_Y 4

static void money(char *b, int n, int v) {   // toLocaleString('en-US')
    char t[24]; snprintf(t, sizeof t, "%d", v);
    int len = strlen(t), o = 0;
    for (int i = 0; i < len && o < n - 2; i++) { b[o++] = t[i]; if ((len - i - 1) % 3 == 0 && i < len - 1 && t[i] != '-') b[o++] = ','; }
    b[o] = 0;
}

// Centered text wrapped at maxw (word by word); returns the height.
static int centered(const char *font, const char *s, int cx, int y, int maxw, uint32_t color, uint32_t shadow) {
    int lh = text_lineh(font), y0 = y;
    char line[256] = "", trial[256];
    const char *p = s;
    while (*p) {
        const char *sp = strchr(p, ' ');
        int wl = sp ? sp - p : (int)strlen(p);
        snprintf(trial, sizeof trial, "%s%s%.*s", line, line[0] ? " " : "", wl, p);
        if (line[0] && text_width(font, trial) > maxw) {
            int w = text_width(font, line);
            if (shadow) text_draw(font, line, cx - w / 2, y + 1, shadow, 0);
            text_draw(font, line, cx - w / 2, y, color, 0);
            y += lh;
            snprintf(line, sizeof line, "%.*s", wl, p);
        } else snprintf(line, sizeof line, "%s", trial);
        p += wl;
        while (*p == ' ') p++;
    }
    if (line[0]) {
        int w = text_width(font, line);
        if (shadow) text_draw(font, line, cx - w / 2, y + 1, shadow, 0);
        text_draw(font, line, cx - w / 2, y, color, 0);
        y += lh;
    }
    return y - y0;
}

static Img *bgImg[2];
static void drawBackground(bool revealing) {
    Img **b = &bgImg[revealing];
    if (!*b) {   // .gacha: flat color + offset dot grid (radial-gradient 24px, half scale here)
        *b = img_new(320, 240);
        uint32_t bg = revealing ? 0x3f2f52ff : 0xecd6eeff, dot = revealing ? 0x57436eff : 0xdcc0e2ff;
        for (int y = 0; y < 240; y++) for (int x = 0; x < 320; x++) {
            bool on = (x % 12 < 2 && y % 12 < 2 && (x / 12 + y / 12) % 2 == 0);
            *img_at(*b, x, y) = __builtin_bswap32(on ? dot : bg);
        }
        img_touch(*b);
    }
    cv_draw(*b, 0, 0);
}

static void drawPrompt(void) {
    int cx = PX + PW / 2, y = 92 + jround(-2 + 2 * cos(gtime * 2 * M_PI / 1.2)) - 2;   // bob
    char b[160];
    if (scene.msgT > 0) { centered("px", scene.msg, cx, y, PW - 8, INK, WHITE); return; }
    if (scene.phase == IDLE) {
        if (hasSet) {
            const char *name = "";
            JFOR(se, table("SEASONS")) if (!strcmp(str(se, "id"), setRef)) name = str(se, "name");
            snprintf(b, sizeof b, "%s Machine", name);
            centered("px", b, cx, y - 20, PW - 8, RED, WHITE);
        }
        int t = num(state, "tickets"), pr = price();
        if (t > 0) {
            y += centered("px", "Tap the crank · 1 ticket", cx, y, PW - 8, INK, WHITE);
            snprintf(b, sizeof b, "(you have %d)", t);
            centered("px", b, cx, y, PW - 8, RED, WHITE);
        } else {
            snprintf(b, sizeof b, "Tap the crank · %d coins", pr);
            y += centered("px", b, cx, y, PW - 8, INK, WHITE);
            if (pr < 100) centered("px", "(1st of the day!)", cx, y, PW - 8, RED, WHITE);
        }
    } else if (scene.phase == WAIT) centered("px", "Tap the capsule!", cx, y, PW - 8, INK, WHITE);
}

static void drawOdds(bool revealing) {
    char b[200] = "", m[24];
    JFOR(r, table("RARITY")) { char t[48]; snprintf(t, sizeof t, "%s %d%% · ", str(r, "name"), (int)num(r, "weight")); strcat(b, t); }
    money(m, sizeof m, num(state, "money"));
    snprintf(b + strlen(b), sizeof b - strlen(b), "You have %s coins", m);
    centered("small", b, PX + PW / 2, 236 - 2 * text_lineh("small"), PW - 4, revealing ? TANHI : BROWN, 0);
}

// The result card, laid out top to bottom into an offscreen image (so it can scroll if it's taller than the screen).
static void layoutCard(bool big) {
    if (!cardImg) cardImg = img_new(PW, 512);
    J *c = critter(), *r = rarity();
    bool isNew = cJSON_IsTrue(jget(scene.result, "isNew"));
    char b[200], key[12];
    snprintf(key, sizeof key, "%d", (int)num(c, "id"));
    int w = PW - 16, cx = PW / 2, y = 6;
    cv_push_target(cardImg);
    cv_clear();
    // rarity chip
    const char *rn = str(r, "name");
    int cw = text_width("px", rn) + 16;
    cv_fill(cx - cw / 2, y, cw, 18, INK);
    cv_fill(cx - cw / 2 + 1, y + 1, cw - 2, 16, css(str(r, "color")));
    cv_alpha(0.35); text_draw("px", rn, cx - cw / 2 + 8, y + 1, 0x000000ff, 0); cv_alpha(1);
    text_draw("px", rn, cx - cw / 2 + 8, y, WHITE, 0);
    y += 21;
    // name: big when it fits
    const char *name = str(c, "name");
    int nw = text_width("px", name);
    if (big && nw * 2 <= w) {
        cv_save(); cv_translate(cx - nw, y); cv_scale(2, 2);
        text_draw("px", name, 0, 1, TANHI, 0); text_draw("px", name, 0, 0, INK, 0);
        cv_restore();
        y += text_lineh("px") * 2 - 4;
    } else y += centered("px", name, cx, y, w, INK, TANHI);
    snprintf(b, sizeof b, "\"%s\"", str(c, "desc"));
    y += centered("small", b, cx, y, w, BROWN, 0) + 4;
    if (isNew) y += centered("px", "NEW! Added to the album", cx, y, w, RED, 0);
    else { snprintf(b, sizeof b, "Duplicate (you have %d) · worth tokens or trades", (int)num(jget(state, "critters"), key)); y += centered("small", b, cx, y, w, BROWN, 0); }
    y += 4;
    y += centered("small", scene.bonusText, cx, y, w, GREEN, 0) + 4;
    // first critter ever: explain what they're for
    if (isNew && jlen(jget(state, "critters")) <= 1) {
        static const char *FIRST[3] = {
            ":book: Album — it's kept in your album (Menu). Collect them all!",
            ":star: Mascot — pick one to follow you and give a bonus.",
            ":rotate: Duplicates — become tokens (10 = 1 ticket at the Inn) or trades with friends.",
        };
        int hh = 0;
        for (int i = 0; i < 3; i++) hh += text_draw("small", FIRST[i], -1000, 0, 0, w - 12) + 1;   // measure (off-canvas)
        hud_nine("assets/ui/paper-small.png", 8, y, w, hh + 8, 4, 4, 4, 4);
        int yy = y + 4;
        for (int i = 0; i < 3; i++) yy += text_draw("small", FIRST[i], 14, yy, INK, w - 12) + 1;
        y += hh + 12;
    }
    // buttons
    btnMascotY = -1;
    if (num(state, "mascot") != num(c, "id") || !cJSON_IsNumber(jget(state, "mascot"))) {
        btnMascotY = y;
        hud_nine("assets/ui/btn.png", 8, y, w, 24, 5, 5, 5, 8);
        const char *l = ":star: Use as mascot";
        text_draw("px", l, cx - text_width("px", l) / 2, y + 3, INK, 0);
        y += 27;
    }
    btnAgainY = y;
    if (num(state, "tickets") > 0) snprintf(b, sizeof b, "One more · 1 ticket");
    else snprintf(b, sizeof b, "One more · %d", price());
    hud_nine("assets/ui/btn-red.png", 8, y, w, 24, 5, 5, 5, 8);
    text_draw("px", b, cx - text_width("px", b) / 2, y + 4, 0x9e2835ff, 0);
    text_draw("px", b, cx - text_width("px", b) / 2, y + 3, WHITE, 0);
    y += 30;
    cv_pop_target();
    img_touch(cardImg);
    cardH = y;
    cardDirty = false;
}

static void chooseMascot(void) {
    J *c = critter();
    setnum(state, "mascot", num(c, "id"));
    onChange();
    snprintf(scene.bonusText, sizeof scene.bonusText, "%s now follows you! %s", str(c, "name"), str(table("BONUSES"), str(c, "bonus")));
    cardDirty = true;
    sfx("pop");
}
static void again(void) { reset(); start(); }

// sdmc:/3ds/PocketFarm/debug: log the scene's CPU time per phase
static void profScene(double t0) {
    static int ph = -1, n; static double sum, mx;
    double d = profNow() - t0;
    if (scene.phase != ph || n >= 120) {
        if (n && plat_debug()) plog("gacha phase %d: scene %.2f ms avg, %.2f max (%d frames)", ph, sum / n, mx, n);
        ph = scene.phase; n = 0; sum = mx = 0;
    }
    n++; sum += d; if (d > mx) mx = d;
}

static bool open_;
static void closeGacha(void) { reset(); open_ = false; onChange(); }

// card: returns true if the input was used
static bool cardInput(void) {
    int h = cardH < CARD_MAXH ? cardH : CARD_MAXH;
    int maxScroll = cardH - h;
    if (in.held & BTN_DOWN) scene.scroll += 4;
    if (in.held & BTN_UP) scene.scroll -= 4;
    if (in.touchDown) { scene.touchY0 = in.ty; scene.drag0 = scene.scroll; scene.dragging = false; }
    if (in.touch && !in.touchDown && abs(in.ty - scene.touchY0) > 6) { scene.dragging = true; scene.scroll = scene.drag0 - (in.ty - scene.touchY0); }
    scene.scroll = fmax(0, fmin(maxScroll, scene.scroll));
    if (in.down & BTN_A) { sfx("click"); again(); return true; }
    if (in.down & (BTN_Y | BTN_X) && btnMascotY >= 0) { chooseMascot(); return true; }
    if (in.touchUp && !scene.dragging && in.tx >= PX + 8 && in.tx < PX + PW - 8) {
        int cy = in.ty - CARD_Y + (int)scene.scroll;
        if (btnMascotY >= 0 && cy >= btnMascotY && cy < btnMascotY + 24) { chooseMascot(); return true; }
        if (cy >= btnAgainY && cy < btnAgainY + 24) { sfx("click"); again(); return true; }
    }
    return false;
}

static void drawCard(void) {
    if (cardDirty) { layoutCard(true); if (cardH > CARD_MAXH) layoutCard(false); }   // big name only if it all fits
    int h = cardH < CARD_MAXH ? cardH : CARD_MAXH;
    double k = fmin(1, scene.cardT / 0.35), e = 1 - pow(1 - k, 3);
    int dx = jround(30 * (1 - e));
    cv_alpha(k);
    hud_nine("assets/ui/btn.png", PX + dx, CARD_Y, PW, h, 5, 5, 5, 8);
    cv_image(cardImg, 0, scene.scroll + 2, PW, h - 8, PX + dx, CARD_Y + 2, PW, h - 8);
    cv_alpha(1);
    if (cardH > h) {   // scrollbar
        int bh = h * h / cardH, by = CARD_Y + (h - bh) * scene.scroll / (cardH - h);
        cv_fill(PX + PW - 5, CARD_Y + 4, 2, h - 8, 0xb86f50ff);
        cv_fill(PX + PW - 5, by + 4, 2, bh - 8, BROWN);
    }
}

static bool frame(double dt) {
    if (!open_) return false;
    dt = fmin(0.05, dt);
    // input
    bool closeTap = in.touchDown && in.tx >= CLOSE_X && in.tx < CLOSE_X + 24 && in.ty >= CLOSE_Y && in.ty < CLOSE_Y + 24;
    if (in.down & BTN_B || closeTap) { sfx("click"); closeGacha(); return false; }
    if (scene.phase == CARD) cardInput();
    else if (in.down & BTN_A || in.touchDown) onTap();
    // update + draw
    double T0 = profNow();
    update(dt);
    drawGacha();
    profScene(T0);
    bool revealing = scene.phase == REVEAL || scene.phase == CARD;
    drawBackground(revealing);
    cv_draw(G, CX, CY);
    if (scene.phase == CARD) drawCard();
    else { drawPrompt(); drawOdds(revealing); }
    hud_nine("assets/ui/btn.png", CLOSE_X, CLOSE_Y, 24, 24, 5, 5, 5, 8);
    drawIcon("close", CLOSE_X + 4, CLOSE_Y + 3, 1);
    return true;
}

void gacha_open(const char *set) {
    hasSet = set != NULL;
    snprintf(setRef, sizeof setRef, "%s", set ? set : "");
    if (!G) G = img_new(W, H);
    reset();
    open_ = true;
    hud_setOverlay(frame);
}
