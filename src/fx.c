// Visual effects (fx.js): particles, items flying to the player, screen shake, tap marker, tile cursor,
// day/night lighting with light sources, seasonal ambience (petals, leaves, snow, fireflies).
#include <stdio.h>
#include "game.h"

static double rnd(double a, double b) { return a + frand() * (b - a); }

enum { CHIP, PUFF, DROP, SPARK };
typedef struct { int k; double x, y, vx, vy, t, t0, g; uint32_t color; } Part;
typedef struct { char item[24]; double x, y, vx, vy, t; bool done; } Flyer;
typedef struct { double x, y, t; } Tap;
typedef struct { bool fly; int kind; double x, y, vx, vy, ph; uint32_t c; } Amb;

static Part parts[512]; static int nparts;
static Flyer flyers[64]; static int nflyers;
static Tap taps[8]; static int ntaps;
static Amb amb[96]; static int namb;
static double shakeT, shakeAmp;

static void addPart(Part p) { if (nparts < 512) parts[nparts++] = p; }

// ---------- one-shot particles ----------
void fx_chips(double x, double y, uint32_t color, int n) {
    for (int i = 0; i < n; i++) { double vx = rnd(-35, 35), vy = rnd(-80, -30); addPart((Part){ CHIP, x, y, vx, vy, rnd(0.5, 0.8), 0, 160, color }); }
}
void fx_puff(double x, double y, uint32_t color, int n) {
    for (int i = 0; i < n; i++) { double px = x + rnd(-5, 5), py = y + rnd(-2, 3), vx = rnd(-14, 14), vy = rnd(-14, -4); addPart((Part){ PUFF, px, py, vx, vy, 0.45, 0.45, 0, color }); }
}
void fx_splash(double x, double y, int n) {
    for (int i = 0; i < n; i++) { double px = x + rnd(-4, 4), vx = rnd(-30, 30), vy = rnd(-70, -35); addPart((Part){ DROP, px, y, vx, vy, rnd(0.35, 0.55), 0, 260, 0 }); }
}
void fx_sparkle(double x, double y, int n, uint32_t color) {
    for (int i = 0; i < n; i++) { double px = x + rnd(-8, 8), py = y + rnd(-10, 2), vy = rnd(-16, -6); addPart((Part){ SPARK, px, py, 0, vy, rnd(0.4, 0.8), 0.8, 0, color }); }
}

// An item pops up from (x, y) and then homes in on the player.
void fx_fly(const char *item, double x, double y) {
    if (nflyers == 64) return;
    Flyer *f = &flyers[nflyers++];
    *f = (Flyer){ .x = x, .y = y, .vx = rnd(-30, 30), .vy = -90 };
    snprintf(f->item, sizeof f->item, "%s", item);
}

void fx_shake(double amp, double dur) { shakeAmp = fmax(shakeAmp, amp); shakeT = fmax(shakeT, dur); }
void fx_shakeOffset(int *x, int *y) {
    if (shakeT > 0) { *x = jround(rnd(-1, 1) * shakeAmp); *y = jround(rnd(-1, 1) * shakeAmp); }
    else *x = *y = 0;
}
void fx_tap(double x, double y) { if (ntaps < 8) taps[ntaps++] = (Tap){ x, y, 0 }; }

void fx_update(double dt) {
    if (shakeT > 0 && (shakeT -= dt) <= 0) shakeAmp = 0;
    int n = 0;
    for (int i = 0; i < nparts; i++) {
        Part *p = &parts[i];
        p->x += p->vx * dt; p->y += p->vy * dt; p->t -= dt;
        if (p->g) p->vy += p->g * dt;
        if (p->k == PUFF) { p->vx *= 0.9; p->vy *= 0.9; }
        if (p->t > 0) parts[n++] = *p;
    }
    nparts = n;
    n = 0;
    for (int i = 0; i < nflyers; i++) {
        Flyer *f = &flyers[i];
        f->t += dt;
        if (f->t < 0.28) { f->x += f->vx * dt; f->y += f->vy * dt; f->vy += 300 * dt; }
        else {
            double k = fmin(1, dt * (8 + (f->t - 0.28) * 40));
            f->x += (player.x - f->x) * k; f->y += (player.y - 14 - f->y) * k;
            if (hypot(player.x - f->x, player.y - 14 - f->y) < 4) f->done = true;
        }
        if (!f->done && f->t < 1.6) flyers[n++] = *f;
    }
    nflyers = n;
    n = 0;
    for (int i = 0; i < ntaps; i++) if ((taps[i].t += dt) < 0.4) taps[n++] = taps[i];
    ntaps = n;
}

void fx_drawWorld(void) {
    Img *tapImg = I("assets/ui/tap.png");
    for (int i = 0; i < ntaps; i++) cv_image(tapImg, fmin(3, floor(taps[i].t / 0.1)) * 16, 0, 16, 16, jround(taps[i].x) - 8, jround(taps[i].y) - 8, 16, 16);
    for (int i = 0; i < nparts; i++) {
        Part *p = &parts[i];
        double x = jround(p->x), y = jround(p->y);
        if (p->k == CHIP) cv_fill(x, y, 2, 2, p->color);
        else if (p->k == DROP) { cv_fill(x, y, 1, 2, 0x9fe2ffff); cv_fill(x, y, 1, 1, 0xffffffff); }
        else if (p->k == PUFF) {
            double a = p->t / p->t0, r = jround(2 + (1 - a) * 3);
            cv_alpha(a * 0.8);
            cv_fill(x - r, y - r + 1, r * 2, r * 2 - 2, p->color);
            cv_fill(x - r + 1, y - r, r * 2 - 2, r * 2, p->color);
            cv_alpha(1);
        } else {
            int on = (int)floor(p->t * 20) % 2;
            cv_fill(x, y - 1, 1, 3, p->color); cv_fill(x - 1, y, 3, 1, p->color);
            if (on) cv_fill(x, y, 1, 1, 0xffffffff);
        }
    }
    for (int i = 0; i < nflyers; i++) drawItem(flyers[i].item, jround(flyers[i].x) - 8, jround(flyers[i].y) - 8);
}

// Soft oval shadow that grounds characters.
void fx_shadow(double x, double y, double w) {
    x = jround(x); y = jround(y);
    uint32_t c = css("rgba(42,36,64,0.28)");
    cv_fill(x - w / 2 + 2, y - 2, w - 4, 4, c);
    cv_fill(x - w / 2, y - 1, w, 2, c);
}

// Pulsing corner brackets around the targeted tile (w×h tiles).
void fx_cursor(int tx, int ty, double time, bool ok, int w, int h) {
    int o = jround(sin(time * 6) * 0.8 + 0.2);
    int x0 = tx * 16 - o, y0 = ty * 16 - o, x1 = (tx + w) * 16 + o - 1, y1 = (ty + h) * 16 + o - 1, L = 4;
    uint32_t cols[2] = { 0x3f2832ff, ok ? 0xffffffff : 0xff6b74ff };
    int corners[4][4] = { { x0, y0, 1, 1 }, { x1, y0, -1, 1 }, { x0, y1, 1, -1 }, { x1, y1, -1, -1 } };
    for (int pass = 0; pass < 2; pass++) {
        int d = pass == 0 ? 1 : 0;
        for (int i = 0; i < 4; i++) {
            int sx = corners[i][2], sy = corners[i][3], bx = corners[i][0] + d * sx, by = corners[i][1] + d * sy;
            cv_fill(sx > 0 ? bx : bx - L + 1, by, L, 1, cols[pass]);
            cv_fill(bx, sy > 0 ? by : by - L + 1, 1, L, cols[pass]);
        }
    }
}

// ---------- lighting ----------
// Tint the scene by multiplying with a light map: the map is the ambient color, and every light source adds a
// warm radial spot. Built at half resolution so the light falls off in chunky pixel steps.
static const double KEYS[9][4] = { { 0, 52, 58, 120 }, { 5 * 60, 60, 66, 130 }, { 6 * 60, 235, 190, 180 }, { 7 * 60, 255, 255, 255 },
    { 16.5 * 60, 255, 255, 255 }, { 18 * 60, 255, 205, 160 }, { 19.5 * 60, 120, 112, 175 }, { 21 * 60, 72, 78, 145 }, { 26 * 60, 52, 58, 120 } };

void fx_ambientColor(double minutes, int out[3]) {
    double m = fmod(minutes, 26 * 60);
    for (int i = 1; i < 9; i++) {
        if (m <= KEYS[i][0]) {
            double t = (m - KEYS[i - 1][0]) / (KEYS[i][0] - KEYS[i - 1][0]);
            for (int c = 0; c < 3; c++) out[c] = jround(KEYS[i - 1][c + 1] + (KEYS[i][c + 1] - KEYS[i - 1][c + 1]) * t);
            return;
        }
    }
    for (int c = 0; c < 3; c++) out[c] = KEYS[8][c + 1];
}

// lights in screen pixels. glow adds a faint warm halo on top.
// The light map is drawn on the CPU (small) and multiplied on with the GPU; the glow halos are one baked
// gradient drawn with additive blending and globalAlpha (same result: the stops just scale by alpha).
void fx_lighting(int vw, int vh, const int tint[3], Light *lights, int n) {
    if (tint[0] >= 254 && tint[1] >= 254 && tint[2] >= 254) return;
    static Img *buf, *glow;
    int w = (vw + 1) / 2, h = (vh + 1) / 2;
    if (!buf || buf->w != w || buf->h != h) { img_free(buf); buf = img_new(w, h); }
    // tint fill, then each light added ('lighter' onto an opaque buffer: d = min(1, d + c·a)), straight on the pixels
    uint32_t *px = (uint32_t *)buf->px, fill;
    uint8_t *f8 = (uint8_t *)&fill;
    f8[0] = tint[0]; f8[1] = tint[1]; f8[2] = tint[2]; f8[3] = 255;
    for (int i = 0; i < w * h; i++) px[i] = fill;
    for (int i = 0; i < n; i++) {
        float x = lights[i].x / 2, y = lights[i].y / 2, r = lights[i].r / 2, a = lights[i].a ? lights[i].a : 0.7;
        float st[3][5] = { { 0, 255, 222, 165, a * 255 }, { 0.4f, 240, 170, 110, a * 0.42f * 255 }, { 1, 200, 140, 100, 0 } };
        int y0 = fmaxf(0, floorf(y - r + 0.5f)), y1 = fminf(h, floorf(y + r + 0.5f)), x0 = fmaxf(0, floorf(x - r + 0.5f)), x1 = fminf(w, floorf(x + r + 0.5f));
        for (int py = y0; py < y1; py++) for (int qx = x0; qx < x1; qx++) {
            float dx = qx + 0.5f - x, dy = py + 0.5f - y, t = sqrtf(dx * dx + dy * dy) / r;
            if (t >= 1) continue;   // last stop is transparent
            const float *s0 = t <= 0.4f ? st[0] : st[1], *s1 = t <= 0.4f ? st[1] : st[2];
            float k = (t - s0[0]) / (s1[0] - s0[0]);
            int sa = s0[4] + (s1[4] - s0[4]) * k + 0.5f;
            if (!sa) continue;
            float as = sa / 255.f;
            uint8_t *d = (uint8_t *)&px[py * w + qx];
            for (int c = 0; c < 3; c++) {
                int sc = s0[c + 1] + (s1[c + 1] - s0[c + 1]) * k + 0.5f;
                float co = fminf(1, sc / 255.f * as + d[c] / 255.f);
                d[c] = co * 255 + 0.5f;
            }
        }
    }
    img_touch(buf);
    cv_op(OP_MULTIPLY);
    cv_image(buf, 0, 0, w, h, 0, 0, w * 2, h * 2);
    if (!glow) {   // 128px halo: rgba(255,170,80,1) → rgba(255,150,60,0) at the rim
        glow = img_new(128, 128);
        cv_push_target(glow);
        float g[] = { 64, 64, 64, 0, 255, 170, 80, 255, 1, 255, 150, 60, 0 };
        cv_fill_radial(0, 0, 128, 128, g, 2);
        cv_pop_target();
    }
    cv_op(OP_LIGHTER);
    double dark = 1 - tint[2] / 255.0;
    for (int i = 0; i < n; i++) {
        if (!lights[i].glow) continue;
        double r = lights[i].r * 0.35;
        cv_alpha(0.22 * dark + 0.05);
        cv_image(glow, 0, 0, 128, 128, lights[i].x - r, lights[i].y - r, r * 2, r * 2);
    }
    cv_alpha(1);
    cv_op(OP_OVER);
}

// ---------- ambience ----------
// kind: spring | summer | fall | winter | mine | NULL; night adds fireflies outdoors.
typedef struct { const char *id; int n; const char *colors[4]; int ncol; double vy[2], sway; int size[2]; } AmbDef;
static const AmbDef AMB[] = {
    { "spring", 18, { "#ffc2d4", "#f7a1bd", "#fff3dc" }, 3, { 8, 16 }, 10, { 2, 1 } },
    { "summer", 10, { "#fff3a8", "#ffffff" }, 2, { -3, 4 }, 6, { 1, 1 } },
    { "fall", 18, { "#f77622", "#e4a672", "#c4462a", "#feae34" }, 4, { 10, 18 }, 14, { 2, 2 } },
    { "winter", 40, { "#ffffff", "#e8f1ff" }, 2, { 10, 22 }, 6, { 1, 1 } },
    { "mine", 16, { "#ead4aa" }, 1, { -2, 2 }, 3, { 1, 1 } },
};
static int ambIndex(const char *k) { for (int i = 0; k && i < 5; i++) if (!strcmp(AMB[i].id, k)) return i; return -1; }

void fx_updateAmbient(double dt, const char *kindName, bool night, double cx, double cy, int vw, int vh) {
    int kind = ambIndex(kindName);
    const AmbDef *def = kind >= 0 ? &AMB[kind] : NULL;
    int want = def ? def->n : 0, flies = night ? 14 : 0, n = 0, leaves = 0, nfly = 0;
    for (int i = 0; i < namb; i++) {
        Amb *a = &amb[i];
        bool in = a->x > cx - 20 && a->x < cx + vw + 20 && a->y > cy - 30 && a->y < cy + vh + 20;
        if (in && (a->fly ? night : a->kind == kind)) amb[n++] = *a;
    }
    namb = n;
    for (int i = 0; i < namb; i++) amb[i].fly ? nfly++ : leaves++;
    while (leaves < want && namb < 96) {
        bool fromTop = def->vy[0] > 4 && namb > 2;
        Amb a = { .kind = kind };
        a.x = cx + rnd(-10, vw + 10);
        a.y = fromTop ? cy - rnd(2, 20) : cy + rnd(0, vh);
        a.vx = rnd(-4, 4); a.vy = rnd(def->vy[0], def->vy[1]); a.ph = rnd(0, 6);
        a.c = css(def->colors[(int)floor(rnd(0, def->ncol))]);
        amb[namb++] = a; leaves++;
    }
    while (nfly < flies && namb < 96) {
        Amb a = { .fly = true, .kind = -1 };
        a.x = cx + rnd(0, vw); a.y = cy + rnd(0, vh); a.vx = rnd(-6, 6); a.vy = rnd(-6, 6); a.ph = rnd(0, 6);
        amb[namb++] = a; nfly++;
    }
    for (int i = 0; i < namb; i++) {
        Amb *a = &amb[i];
        a->ph += dt;
        if (a->fly) { a->vx += rnd(-20, 20) * dt; a->vy += rnd(-20, 20) * dt; a->vx *= 0.98; a->vy *= 0.98; a->x += a->vx * dt; a->y += a->vy * dt; }
        else { a->x += (a->vx + sin(a->ph * 1.6) * AMB[a->kind].sway) * dt; a->y += a->vy * dt; }
    }
}

void fx_drawAmbient(void) {
    for (int i = 0; i < namb; i++) {
        Amb *a = &amb[i];
        if (a->fly) continue;
        const int *s = AMB[a->kind].size;
        bool flip = sin(a->ph * 3) > 0;
        cv_fill(jround(a->x), jround(a->y), flip ? s[0] : s[1], flip ? s[1] : s[0], a->c);
    }
}

// Fireflies glow, so they go on top of the lighting (screen space).
void fx_drawFireflies(double cx, double cy) {
    cv_op(OP_LIGHTER);
    for (int i = 0; i < namb; i++) {
        Amb *a = &amb[i];
        if (!a->fly) continue;
        double on = (sin(a->ph * 2.2) + 1) / 2;
        if (on < 0.25) continue;
        double x = jround(a->x - cx), y = jround(a->y - cy);
        cv_fill(x - 1, y - 1, 3, 3, 0xc8ff7800 | (uint32_t)(on * 0.35 * 255 + 0.5));
        cv_fill(x, y, 1, 1, 0xf0ffaa00 | (uint32_t)(on * 255 + 0.5));
    }
    cv_op(OP_OVER);
}
