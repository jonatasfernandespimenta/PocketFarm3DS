// Procedural pixel-art ground painter (terrain.js): grass patches, dirt paths with organic edges, cave floor.
//   g = grass · G = dark grass (forest floor) · d = dirt path · c = cave floor · w = water
//   r = rock (cliff face on its lower edge, dark top above) · h = cave mouth carved into a rock face
// Water pixels are left transparent in `ground` and opaque in `mask`, so animated water can be drawn under it.
// Same integer hash and double math as the JS, so the ground comes out pixel-identical.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

typedef struct { uint8_t r, g, b; } RGB;
static RGB hex(const char *h) { uint32_t c = css(h); return (RGB){ c >> 24, c >> 16, c >> 8 }; }

static const double BAYER[16] = { 0 / 16., 8 / 16., 2 / 16., 10 / 16., 12 / 16., 4 / 16., 14 / 16., 6 / 16., 3 / 16., 11 / 16., 1 / 16., 9 / 16., 15 / 16., 7 / 16., 13 / 16., 5 / 16. };

double thash(int x, int y, int s) {
    uint32_t h = (uint32_t)((int64_t)x * 374761393 + (int64_t)y * 668265263 + (int64_t)s * 2147483647);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) / 4294967296.0;
}
static double noise(double x, double y, int s) {
    double xi = floor(x), yi = floor(y), xf = x - xi, yf = y - yi;
    double u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    int X = (int)xi, Y = (int)yi;
    double a = thash(X, Y, s), b = thash(X + 1, Y, s), c = thash(X, Y + 1, s), d = thash(X + 1, Y + 1, s);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

// palette slots
enum { g0, g1, g2, g3, g4, g5, d0, d1, d2, d3, d4, d5, c0, c1, c2, c3, c4, r0, r1, r2, r3, r4, rt, hole, hole2, fy, fw, fp, b0, b1, b2, foam, NPAL };
static const char *BASE[NPAL] = {
    "#193c3e", "#265c42", "#3e8948", "#55a24f", "#86c35a", "#b7dd73",
    "#6d483b", "#9c6754", "#b86f50", "#d99359", "#e4a672", "#f0c08e",
    "#241c22", "#352b33", "#45393f", "#54464a", "#655556",
    "#23202e", "#3a3d52", "#525f7a", "#6c7c9d", "#9aa8c4", "#2f3144", "#0e0a10", "#1d1724",
    "#f6e27a", "#fff4e0", "#f0a0c0",
    "#3f2832", "#6d483b", "#9c6754", "#bfefff",
};
// grass colours per season (the rest of the palette stays). Winter = snow.
static const char *SEASON_GRASS[3][6] = {
    { "#173a2c", "#2a6a3c", "#46993f", "#68b74b", "#9fd45b", "#d4ee85" },   // summer
    { "#3a2a1e", "#5e5228", "#8a7b2e", "#b28f3a", "#d6a84c", "#f0cd7e" },   // fall
    { "#7d8fa8", "#a9bad0", "#cfdcea", "#e6eef7", "#f5f9fd", "#ffffff" },   // winter
};

// painting context (paintTerrain's closures)
static char **rows; static int W, H, PW, PH, seed;
static RGB P[NPAL];
static uint8_t *out, *kind;

static char at(double px, double py) {
    int tx = (int)fmax(0, fmin(W - 1, floor(px / 16))), ty = (int)fmax(0, fmin(H - 1, floor(py / 16)));
    return rows[ty][tx];
}
// Domain-warped lookup: edges between terrains wobble instead of following the tile grid.
static char warped(double px, double py) {
    double ox = (noise(px * 0.09, py * 0.09, seed) - 0.5) * 9;
    double oy = (noise(px * 0.09 + 50, py * 0.09 + 50, seed) - 0.5) * 9;
    return at(px + ox, py + oy);
}
#define FACE 13
static bool rockAt(int px, int py) {   // the bottom edge is ragged by a pixel or two so cliffs don't look ruled
    int j = (int)jround((noise(px * 0.35, 3, seed + 41) - 0.5) * 3);
    char t = at(px, py + j);
    return t == 'r' || t == 'h';
}
static int W01(int tx, int ty) { return rows[ty < 0 ? 0 : ty > H - 1 ? H - 1 : ty][tx < 0 ? 0 : tx > W - 1 ? W - 1 : tx] == 'w'; }
// Shoreline: "is water" blended between tile centers and cut at 0.5 with a little noise: round ponds.
static bool water(int px, int py) {
    double fx = px / 16.0 - 0.5, fy = py / 16.0 - 0.5, x0 = floor(fx), y0 = floor(fy), u = fx - x0, v = fy - y0;
    int X = (int)x0, Y = (int)y0;
    double top = W01(X, Y) * (1 - u) + W01(X + 1, Y) * u, bot = W01(X, Y + 1) * (1 - u) + W01(X + 1, Y + 1) * u;
    return top * (1 - v) + bot * v + (noise(px * 0.1, py * 0.1, seed + 21) - 0.5) * 0.3 > 0.5;
}
static void put(int x, int y, RGB c) {
    if (x < 0 || y < 0 || x >= PW || y >= PH || kind[y * PW + x] == 4) return;
    uint8_t *o = out + (y * PW + x) * 4;
    o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = 255;
}
static void shade(int x, int y, RGB c, int a) { uint8_t *o = out + (y * PW + x) * 4; o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = a; }
static bool rockEdge(int px, int py, int r) { return !rockAt(px + r, py) || !rockAt(px - r, py) || !rockAt(px, py - r); }
static bool shore(int px, int py, int r) { return !water(px + r, py) || !water(px - r, py) || !water(px, py + r) || !water(px, py - r); }
static bool isDirt(char t) { return t == 'd'; }
static bool nearGrass(int px, int py, int r) { return !isDirt(warped(px + r, py)) || !isDirt(warped(px - r, py)) || !isDirt(warped(px, py + r)) || !isDirt(warped(px, py - r)); }

void paintTerrain(char **trows, int th, int tseed, const char *seasonId, Img **ground, Img **maskOut) {
    rows = trows; H = th; W = strlen(rows[0]); seed = tseed;
    for (int i = 0; i < NPAL; i++) P[i] = hex(BASE[i]);
    int si = !strcmp(seasonId, "summer") ? 0 : !strcmp(seasonId, "fall") ? 1 : !strcmp(seasonId, "winter") ? 2 : -1;
    if (si >= 0) for (int i = 0; i < 6; i++) P[g0 + i] = hex(SEASON_GRASS[si][i]);
    bool winter = si == 2, masked = false, cave = false;
    for (int y = 0; y < H; y++) { if (strchr(rows[y], 'w')) masked = true; if (strchr(rows[y], 'c')) cave = true; }
    PW = W * 16; PH = H * 16;
    Img *img = img_new(PW, PH), *mask = masked ? img_new(PW, PH) : NULL;
    out = img->px;
    kind = calloc(PW * PH, 1);   // 1 = grass-ish, 2 = dirt, 3 = cave, 4 = water, 5 = rock

    for (int py = 0; py < PH; py++) {
        for (int px = 0; px < PW; px++) {
            double b = BAYER[(py & 3) * 4 + (px & 3)];
            if (rockAt(px, py)) {
                kind[py * PW + px] = 5;
                int d = 1;
                while (d <= FACE + 1 && rockAt(px, py + d)) d++;
                RGB c;
                if (d <= FACE) {
                    // cliff face: vertical streaks, ledges, bright rim on top, dark foot
                    if (d <= 1) c = P[r0];
                    else if (d >= FACE - 1) c = d == FACE ? P[r4] : P[r3];
                    else {
                        bool ledge = (py + (int)floor(noise(px * 0.08, 7, seed + 42) * 5)) % 5 == 0;
                        double v = noise(px * 0.4, py * 0.07, seed + 43) + (b - 0.5) * 0.3;
                        c = ledge ? P[r1] : v > 0.62 ? P[r3] : v < 0.3 ? P[r1] : P[r2];
                        if (thash(px, py, seed + 44) < 0.015) c = P[r4];
                    }
                } else {
                    // rock top: dark and lumpy, outlined where it meets open ground
                    if (rockEdge(px, py, 1)) c = P[r0];
                    else if (rockEdge(px, py, 2)) c = P[r2];
                    else {
                        double v = noise(px * 0.15, py * 0.15, seed + 45) + (b - 0.5) * 0.25;
                        c = v > 0.58 ? P[r1] : P[rt];
                        if (thash(px, py, seed + 46) < 0.004) c = P[r2];
                    }
                }
                put(px, py, c);
                continue;
            }
            if (masked && water(px, py)) {
                // inside the pond: transparent (animated water shows through), foam and depth near the shore
                kind[py * PW + px] = 4;
                mask->px[(py * PW + px) * 4 + 3] = 255;
                if (shore(px, py, 1)) shade(px, py, P[foam], 230);
                else if (shore(px, py, 3)) shade(px, py, P[b0], 70);
                else if (shore(px, py, 5) && b > 0.5) shade(px, py, P[b0], 40);
                continue;
            }
            if (masked) {
                // land touching water: dark rim; a brown earth bank where the water is below
                if (water(px + 1, py) || water(px - 1, py) || water(px, py - 1) || water(px, py + 1)) { put(px, py, P[b0]); kind[py * PW + px] = 2; continue; }
                if (water(px, py + 2) || water(px, py + 3)) { put(px, py, (py + px) % 5 ? P[b2] : P[b1]); kind[py * PW + px] = 2; continue; }
                if (water(px, py + 4)) { put(px, py, P[b1]); kind[py * PW + px] = 2; continue; }
            }
            char t = warped(px, py);
            if (t == 'r' || t == 'h' || t == 'w') t = at(px, py);   // warp pulled in a neighbour: use this tile's own ground
            if (t == 'r' || t == 'h' || t == 'w') t = cave ? 'c' : 'g';
            bool underRock = rockAt(px, py - 1) || rockAt(px, py - 2) || (rockAt(px, py - 4) && b > 0.5);
            if (underRock) { put(px, py, t == 'c' ? P[c0] : t == 'd' ? P[d0] : P[g0]); kind[py * PW + px] = t == 'c' ? 3 : 1; continue; }
            if (t == 'c') {
                double v = noise(px * 0.06, py * 0.06, seed + 3) * 0.75 + noise(px * 0.25, py * 0.25, seed + 4) * 0.25;
                int lvl = (int)fmax(0, fmin(2, floor(v * 3 + (b - 0.5) * 0.5)));
                RGB c = P[c2 + lvl];
                if (thash(px, py, seed + 5) < 0.02) c = P[c1];
                put(px, py, c); kind[py * PW + px] = 3;
            } else if (isDirt(t)) {
                RGB c;
                if (nearGrass(px, py, 1)) c = P[d1];
                else if (nearGrass(px, py, 2)) c = P[d2];
                else {
                    double v = noise(px * 0.07, py * 0.07, seed + 1);
                    c = v + (b - 0.5) * 0.25 < 0.38 ? P[d3] : P[d4];
                    double hsh = thash(px, py, seed + 2);
                    if (hsh < 0.012) c = P[d2];            // pebble
                    else if (hsh < 0.016) c = P[d5];       // glint
                }
                put(px, py, c); kind[py * PW + px] = 2;
            } else {
                bool dark = t == 'G';
                double v = noise(px * 0.035, py * 0.035, seed) * 0.6 + noise(px * 0.11, py * 0.11, seed + 9) * 0.4;
                int lvl = (int)floor(v * 3.2 + (b - 0.5) * 0.45) - (dark ? 1 : 0);
                lvl = lvl < 0 ? 0 : lvl > 3 ? 3 : lvl;
                static const int G[4] = { g1, g2, g3, g4 };
                put(px, py, P[G[dark ? (lvl < 2 ? lvl : 2) : (lvl > 1 ? lvl : 1)]]);
                kind[py * PW + px] = 1;
            }
        }
    }

    // Second pass: grass blades, little flowers, shadow under grass overhanging dirt.
    for (int py = 0; py < PH; py++) {
        for (int px = 0; px < PW; px++) {
            int k = kind[py * PW + px];
            if (k == 1) {
                int below = kind[(py + 1 < PH ? py + 1 : PH - 1) * PW + px];
                if (below == 2) { put(px, py + 1, P[d0]); continue; }   // grass lip casts a dark edge on dirt
                double h = thash(px, py, seed + 11);
                char tile = rows[py / 16][px / 16];
                if (h < 0.035) {                                        // blade: two dark strokes and a bright tip
                    bool dark = tile == 'G';
                    put(px, py, dark ? P[g0] : P[g1]); put(px + 2, py, dark ? P[g0] : P[g1]);
                    put(px + 1, py + 1, dark ? P[g0] : P[g1]);
                    put(px, py - 1, dark ? P[g2] : P[g4]); put(px + 2, py - 1, dark ? P[g2] : P[g4]);
                } else if (h < 0.0395) {                                // light tuft highlight
                    put(px, py, P[g5]); put(px + 1, py, P[g4]);
                } else if (!winter && h < 0.0405 && tile == 'g') {
                    static const int F[3] = { fy, fw, fp };
                    put(px, py, P[F[(int)floor(thash(px, py, 99) * 3)]]); put(px, py + 1, P[g1]);   // tiny flower
                }
            } else if (k == 2 && py > 0 && kind[(py - 1) * PW + px] == 1 && thash(px, py, seed + 13) < 0.35) {
                put(px, py, P[g2]);                                     // grass creeping over the path edge
                if (thash(px, py, seed + 14) < 0.5) put(px, py + 1, P[g1]);
            }
        }
    }
    for (int ty = 0; ty < H; ty++) {
        for (int tx = 0; tx < W; tx++) {
            if (rows[ty][tx] != 'h' || (tx > 0 && rows[ty][tx - 1] == 'h')) continue;
            int tx1 = tx;
            while (tx1 + 1 < W && rows[ty][tx1 + 1] == 'h') tx1++;
            int x0 = tx * 16 + 2, x1 = tx1 * 16 + 13, yb = ty * 16 + 15, archH = 13;
            double cx = (x0 + x1) / 2.0, rx = (x1 - x0) / 2.0;
            for (int x = x0 - 1; x <= x1 + 1; x++) {
                double k = fmin(1, fabs(x - cx) / (rx + 1));
                int top = (int)jround(yb - archH + archH * 0.55 * (1 - sqrt(1 - k * k)) * 1.8);
                for (int y = top - 1; y <= yb; y++) {
                    bool inside = x >= x0 && x <= x1 && y >= top;
                    if (!inside) { put(x, y, P[r4]); continue; }       // bright rim around the arch
                    put(x, y, y < top + 2 || x < x0 + 2 || x > x1 - 2 ? P[hole2] : P[hole]);
                }
            }
        }
    }
    free(kind);
    *ground = img;
    *maskOut = mask;
}

// 16 frames of a 64x64 tileable water texture: drifting light streaks + twinkles.
void waterFrames(Img *frames[16]) {
    RGB base = hex("#1f8fe0"), deep = hex("#1173c4"), mid = hex("#3aa6ef"), hi = hex("#7fd0ff"), white = hex("#e8f8ff");
    for (int f = 0; f < 16; f++) {
        Img *c = img_new(64, 64);
        double t = f / 16.0 * M_PI * 2;
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            double v = sin(x / 64.0 * M_PI * 4 + t + sin(y / 64.0 * M_PI * 2) * 1.5) * 0.6 + sin(y / 64.0 * M_PI * 6 - t) * 0.4;
            double b = BAYER[(y & 3) * 4 + (x & 3)];
            RGB col = v + (b - 0.5) * 0.25 > 0.62 ? hi : v > 0.38 ? mid : v < -0.72 ? deep : base;
            if (thash(x, y, 77) < 0.006 && sin(t * 2 + thash(x, y, 78) * 6.28) > 0.85) col = white;
            uint8_t *o = c->px + (y * 64 + x) * 4;
            o[0] = col.r; o[1] = col.g; o[2] = col.b; o[3] = 255;
        }
        frames[f] = c;
    }
}

// Tilled soil drawn in the same style as the terrain: furrows, rough edges, joined to tilled neighbours.
// Index = n | s<<1 | e<<2 | w<<3 (neighbour is also soil).
static void makeSoil(Img *out16[16], const char *pal4[4]) {
    RGB pal[4];
    for (int i = 0; i < 4; i++) pal[i] = hex(pal4[i]);
    for (int m = 0; m < 16; m++) {
        int n = m & 1, s = m & 2, e = m & 4, w = m & 8;
        Img *c = img_new(16, 16);
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            int d = 99;
            if (!n && y < d) d = y;
            if (!s && 15 - y < d) d = 15 - y;
            if (!e && 15 - x < d) d = 15 - x;
            if (!w && x < d) d = x;
            int j = thash(x, y, 31) < 0.5 ? 0 : 1;
            if (d < j) continue;                                          // ragged edge
            if ((!n && !w && x + y < 2) || (!n && !e && 15 - x + y < 2) || (!s && !w && x + 15 - y < 2) || (!s && !e && 30 - x - y < 2)) continue;
            RGB col;
            if (d < j + 1) col = (!s && 15 - y == d) ? pal[0] : pal[1];   // outline, darker at the bottom
            else if (y % 4 == 2 && thash(x, y, 32) > 0.2) col = pal[1];  // furrow
            else if (y % 4 == 3) col = pal[3];                           // furrow highlight
            else col = thash(x, y, 33) < 0.07 ? pal[3] : pal[2];
            uint8_t *o = c->px + (y * 16 + x) * 4;
            o[0] = col.r; o[1] = col.g; o[2] = col.b; o[3] = 255;
        }
        out16[m] = c;
    }
}
void soilTiles(Img *dry[16], Img *wet[16]) {
    makeSoil(dry, (const char *[]){ "#4a2f27", "#6d483b", "#8f5d47", "#b07657" });
    makeSoil(wet, (const char *[]){ "#2a1c22", "#3f2832", "#553630", "#6d483b" });
}
