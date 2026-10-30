// Canvas state (transform stack, alpha, op), image loading, and the CPU rasterizer. The rasterizer draws
// every offscreen canvas and, on PC, the screens too. On 3DS the screens go to cv_3ds.c.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cv.h"

#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef uint8_t u8;

// ---------- images ----------
typedef struct { char *path; Img *im; } Cached;
static Cached *cache; static int ncache;

Img *img_new(int w, int h) {
    Img *im = calloc(1, sizeof *im);
    im->w = w; im->h = h;
    im->px = calloc((size_t)w * h, 4);
    im->dirty = true;
    return im;
}

void img_free(Img *im) {
    if (!im) return;
    gpu_free(im);
    free(im->px);
    free(im);
}

Img *img_load(const char *path) { return img_load_ex(path, NULL); }
Img *img_decode(const char *path) {   // uncached: the caller owns it
    int w, h, ch;
    u8 *px = stbi_load(plat_path(path), &w, &h, &ch, 4);
    if (!px) return NULL;
    Img *im = calloc(1, sizeof *im);
    im->w = w; im->h = h; im->px = px; im->dirty = true;
    return im;
}
Img *img_load_ex(const char *path, bool *fresh) {
    for (int i = 0; i < ncache; i++) if (!strcmp(cache[i].path, path)) return cache[i].im;
    if (fresh) *fresh = true;
    int w, h, ch;
    u8 *px = stbi_load(plat_path(path), &w, &h, &ch, 4);
    Img *im = NULL;
    if (px) { im = calloc(1, sizeof *im); im->w = w; im->h = h; im->px = px; im->dirty = true; }
    cache = realloc(cache, (ncache + 1) * sizeof *cache);
    cache[ncache++] = (Cached){ strdup(path), im };
    return im;
}

// ---------- state ----------
typedef struct { float m[6]; float alpha; int op; uint32_t tint; } St;
static St st, stack[64];
static int depth;
static Img *target;

void cv_target(Img *t) {
    target = t; depth = 0;
    st = (St){ { 1, 0, 0, 1, 0, 0 }, 1, OP_OVER };
    if (t->screen) gpu_target(t);
}
// Offscreen drawing in the middle of a frame (light map, baked canvases): swap the whole canvas state.
static struct { Img *t; St st, stack[64]; int depth; } saved[4];
static int nsaved;
void cv_push_target(Img *t) {
    saved[nsaved].t = target; saved[nsaved].st = st; saved[nsaved].depth = depth;
    memcpy(saved[nsaved].stack, stack, sizeof stack);
    nsaved++;
    target = t; depth = 0;
    st = (St){ { 1, 0, 0, 1, 0, 0 }, 1, OP_OVER };
}
void cv_pop_target(void) {
    nsaved--;
    target = saved[nsaved].t; st = saved[nsaved].st; depth = saved[nsaved].depth;
    memcpy(stack, saved[nsaved].stack, sizeof stack);
}
void cv_save(void) { if (depth < 64) stack[depth++] = st; }
void cv_restore(void) { if (depth) st = stack[--depth]; }
static void mul(float a, float b, float c, float d, float e, float f) {   // m = m * [a c e; b d f]
    float *m = st.m, n[6] = {
        m[0] * a + m[2] * b, m[1] * a + m[3] * b, m[0] * c + m[2] * d, m[1] * c + m[3] * d,
        m[0] * e + m[2] * f + m[4], m[1] * e + m[3] * f + m[5] };
    memcpy(m, n, sizeof n);
}
void cv_translate(float x, float y) { mul(1, 0, 0, 1, x, y); }
void cv_scale(float x, float y) { mul(x, 0, 0, y, 0, 0); }
void cv_rotate(float a) { float c = cosf(a), s = sinf(a); mul(c, s, -s, c, 0, 0); }
void cv_set_transform(float a, float b, float c, float d, float e, float f) { float n[6] = { a, b, c, d, e, f }; memcpy(st.m, n, sizeof n); }
void cv_alpha(float a) { st.alpha = a < 0 ? 0 : a > 1 ? 1 : a; }
void cv_op(int op) { st.op = op; }
void cv_tint(uint32_t rgba) { st.tint = rgba; }

void cv_image(Img *im, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh) {
    if (!im || sw <= 0 || sh <= 0 || dw == 0 || dh == 0) return;
    if (target->screen) gpu_image(im, sx, sy, sw, sh, dx, dy, dw, dh, st.m, st.alpha, st.op, st.tint);
    else soft_image(target, im, sx, sy, sw, sh, dx, dy, dw, dh, st.m, st.alpha, st.op, st.tint);
}
void cv_draw(Img *im, float dx, float dy) { if (im) cv_image(im, 0, 0, im->w, im->h, dx, dy, im->w, im->h); }
void cv_fill(float x, float y, float w, float h, uint32_t rgba) {
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    if (target->screen) gpu_fill(x, y, w, h, st.m, st.alpha, st.op, rgba, NULL);
    else soft_fill(target, x, y, w, h, st.m, st.alpha, st.op, rgba, NULL);
}
void cv_fill_pattern(Img *pat, float x, float y, float w, float h) {
    if (!pat) return;
    if (target->screen) gpu_fill(x, y, w, h, st.m, st.alpha, st.op, 0, pat);
    else soft_fill(target, x, y, w, h, st.m, st.alpha, st.op, 0, pat);
}
void cv_clear(void) {
    if (target->screen) { float id[6] = { 1, 0, 0, 1, 0, 0 }; gpu_fill(0, 0, target->w, target->h, id, 1, -1, 0, NULL); }
    else { memset(target->px, 0, (size_t)target->w * target->h * 4); target->dirty = true; }
}

// ---------- CPU rasterizer ----------
static inline u8 clamp8(float v) { return v <= 0 ? 0 : v >= 255 ? 255 : (u8)(v + 0.5f); }

// One straight-alpha source pixel (alpha already scaled by globalAlpha) onto d.
static inline void comp(u8 *d, int sr, int sg, int sb, int sa, int op) {
    if (op == OP_OVER) {
        if (!sa) return;
        int da = d[3];
        if (sa == 255) { d[0] = sr; d[1] = sg; d[2] = sb; d[3] = 255; return; }
        if (da == 255) {
            int ia = 255 - sa;
            d[0] = (sr * sa + d[0] * ia + 127) / 255;
            d[1] = (sg * sa + d[1] * ia + 127) / 255;
            d[2] = (sb * sa + d[2] * ia + 127) / 255;
            return;
        }
        int oa = sa + da * (255 - sa) / 255;
        if (!oa) return;
        int k = da * (255 - sa) / 255;
        d[0] = (sr * sa + d[0] * k) / oa; d[1] = (sg * sa + d[1] * k) / oa; d[2] = (sb * sa + d[2] * k) / oa;
        d[3] = oa;
        return;
    }
    // the rare ops: premultiplied float math, straight from the compositing spec
    float as = sa / 255.f, ab = d[3] / 255.f;
    float cs[3] = { sr / 255.f * as, sg / 255.f * as, sb / 255.f * as };
    float cb[3] = { d[0] / 255.f * ab, d[1] / 255.f * ab, d[2] / 255.f * ab };
    float co[3], ao;
    switch (op) {
    case OP_LIGHTER: ao = fminf(1, as + ab); for (int i = 0; i < 3; i++) co[i] = fminf(1, cs[i] + cb[i]); break;
    case OP_MULTIPLY: ao = as + ab * (1 - as); for (int i = 0; i < 3; i++) co[i] = cs[i] * (1 - ab) + cb[i] * (1 - as) + cs[i] * cb[i]; break;
    case OP_ATOP: ao = ab; for (int i = 0; i < 3; i++) co[i] = cs[i] * ab + cb[i] * (1 - as); break;
    case OP_DST_IN: ao = ab * as; for (int i = 0; i < 3; i++) co[i] = cb[i] * as; break;
    default: return;
    }
    if (ao <= 0) { d[0] = d[1] = d[2] = d[3] = 0; return; }
    for (int i = 0; i < 3; i++) d[i] = clamp8(co[i] / ao * 255);
    d[3] = clamp8(ao * 255);
}

typedef struct {
    int kind;                    // 0 color, 1 image, 2 pattern, 3 radial gradient
    u8 rgba[4];
    const u8 *src; int sw, sh;
    float sx, sy, kx, ky;        // image: user coords -> source texel
    const float *g; int gn;      // radial: cx, cy, r, then (offset, r, g, b, a 0..255) * gn
    uint32_t tint;
} Paint;

static inline bool sample(const Paint *p, float ux, float uy, int *r, int *g, int *b, int *a) {
    const u8 *s;
    if (p->kind == 0) { *r = p->rgba[0]; *g = p->rgba[1]; *b = p->rgba[2]; *a = p->rgba[3]; return true; }
    if (p->kind == 3) {   // concentric radial gradient, stops interpolated unpremultiplied
        const float *q = p->g, *st = q + 3;
        float t = sqrtf((ux - q[0]) * (ux - q[0]) + (uy - q[1]) * (uy - q[1])) / q[2];
        int i = 1;
        while (i < p->gn && t > st[i * 5]) i++;
        const float *s0 = st + (i - 1) * 5, *s1 = st + (i < p->gn ? i : p->gn - 1) * 5;
        float k = t <= s0[0] ? 0 : s1[0] > s0[0] ? fminf(1, (t - s0[0]) / (s1[0] - s0[0])) : 1;
        *r = s0[1] + (s1[1] - s0[1]) * k + 0.5f; *g = s0[2] + (s1[2] - s0[2]) * k + 0.5f;
        *b = s0[3] + (s1[3] - s0[3]) * k + 0.5f; *a = s0[4] + (s1[4] - s0[4]) * k + 0.5f;
        return true;
    }
    if (p->kind == 1) {
        int tx = (int)floorf(p->sx + ux * p->kx), ty = (int)floorf(p->sy + uy * p->ky);
        if (tx < 0 || ty < 0 || tx >= p->sw || ty >= p->sh) return false;
        s = p->src + (ty * p->sw + tx) * 4;
        if (p->tint) { *r = p->tint >> 24; *g = p->tint >> 16 & 255; *b = p->tint >> 8 & 255; *a = s[3] * (p->tint & 255) / 255; return true; }
    } else {
        int tx = (int)floorf(ux) % p->sw, ty = (int)floorf(uy) % p->sh;
        if (tx < 0) tx += p->sw;
        if (ty < 0) ty += p->sh;
        s = p->src + (ty * p->sw + tx) * 4;
    }
    *r = s[0]; *g = s[1]; *b = s[2]; *a = s[3];
    return true;
}

// Paints user-space rect (x,y,w,h) through matrix m onto dst. Axis-aligned transforms step per row;
// rotations fall back to per-pixel inverse mapping. Pixel centers decide coverage, like the browser.
static void raster(Img *dst, float x, float y, float w, float h, const float *m, float alpha, int op, const Paint *p) {
    if (w <= 0 || h <= 0) return;
    int dw = dst->w, dh = dst->h;
    float a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5];
    float det = a * d - b * c;
    if (fabsf(det) < 1e-9f) return;
    float ia = d / det, ib = -b / det, ic = -c / det, id = a / det;
    float ie = (c * f - d * e) / det, iff = (b * e - a * f) / det;
    int x0, y0, x1, y1;
    if (op == OP_DST_IN) { x0 = 0; y0 = 0; x1 = dw; y1 = dh; }   // destination-in also wipes what it doesn't cover
    else {
        float xs[4] = { x, x + w, x, x + w }, ys[4] = { y, y, y + h, y + h };
        float mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
        for (int i = 0; i < 4; i++) {
            float X = a * xs[i] + c * ys[i] + e, Y = b * xs[i] + d * ys[i] + f;
            mnx = fminf(mnx, X); mxx = fmaxf(mxx, X); mny = fminf(mny, Y); mxy = fmaxf(mxy, Y);
        }
        x0 = (int)floorf(mnx + 0.5f); y0 = (int)floorf(mny + 0.5f);
        x1 = (int)floorf(mxx + 0.5f); y1 = (int)floorf(mxy + 0.5f);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > dw) x1 = dw;
        if (y1 > dh) y1 = dh;
    }
    int ga = (int)(alpha * 255 + 0.5f);
    bool axis = fabsf(b) < 1e-6f && fabsf(c) < 1e-6f;
    for (int py = y0; py < y1; py++) {
        u8 *row = dst->px + py * dw * 4;
        float cy = py + 0.5f;
        if (axis) {
            float uy = id * cy + iff;
            bool rowIn = uy >= y && uy < y + h;
            if (!rowIn && op != OP_DST_IN) continue;
            for (int px = x0; px < x1; px++) {
                float ux = ia * (px + 0.5f) + ie;
                int r, g, bb, sa;
                if (rowIn && ux >= x && ux < x + w && sample(p, ux, uy, &r, &g, &bb, &sa)) comp(row + px * 4, r, g, bb, sa * ga / 255, op);
                else if (op == OP_DST_IN) comp(row + px * 4, 0, 0, 0, 0, op);
            }
        } else {
            for (int px = x0; px < x1; px++) {
                float cx = px + 0.5f;
                float ux = ia * cx + ic * cy + ie, uy = ib * cx + id * cy + iff;
                int r, g, bb, sa;
                if (ux >= x && ux < x + w && uy >= y && uy < y + h && sample(p, ux, uy, &r, &g, &bb, &sa)) comp(row + px * 4, r, g, bb, sa * ga / 255, op);
                else if (op == OP_DST_IN) comp(row + px * 4, 0, 0, 0, 0, op);
            }
        }
    }
    dst->dirty = true;
}

void soft_image(Img *dst, Img *im, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, const float m[6], float alpha, int op, uint32_t tint) {
    Paint p = { .kind = 1, .src = im->px, .sw = im->w, .sh = im->h, .kx = sw / dw, .ky = sh / dh, .tint = tint };
    p.sx = sx - dx * p.kx; p.sy = sy - dy * p.ky;
    // clamp the source rect to the image: the destination shrinks with it, like the browser does
    float cx0 = fmaxf(sx, 0), cy0 = fmaxf(sy, 0), cx1 = fminf(sx + sw, im->w), cy1 = fminf(sy + sh, im->h);
    if (cx1 <= cx0 || cy1 <= cy0) return;
    float x = dx + (cx0 - sx) / p.kx, y = dy + (cy0 - sy) / p.ky, w = (cx1 - cx0) / p.kx, h = (cy1 - cy0) / p.ky;
    // negative sizes (flipped draws) normalize to a positive rect; sampling keeps the mapping
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    // fast path: unscaled copy at a whole-pixel offset (baking, compositing sheets). Same pixels as raster().
    float X = m[0] * x + m[4], Y = m[3] * y + m[5];
    if (m[0] == 1 && m[3] == 1 && !m[1] && !m[2] && p.kx == 1 && p.ky == 1 && X == floorf(X) && Y == floorf(Y) && w == floorf(w) && h == floorf(h)
        && cx0 == floorf(cx0) && cy0 == floorf(cy0) && op != OP_DST_IN && !tint) {
        int ga = (int)(alpha * 255 + 0.5f), ix = X, iy = Y, iw = w, ih = h, sx0 = cx0, sy0 = cy0;
        for (int r = 0; r < ih; r++) {
            int ty = iy + r;
            if (ty < 0 || ty >= dst->h) continue;
            const u8 *srow = im->px + ((sy0 + r) * im->w + sx0) * 4;
            u8 *drow = dst->px + ty * dst->w * 4;
            for (int c = 0; c < iw; c++) {
                int tx = ix + c;
                if (tx < 0 || tx >= dst->w) continue;
                const u8 *sp = srow + c * 4;
                if (op == OP_OVER && !sp[3]) continue;
                comp(drow + tx * 4, sp[0], sp[1], sp[2], ga == 255 ? sp[3] : sp[3] * ga / 255, op);
            }
        }
        dst->dirty = true;
        return;
    }
    raster(dst, x, y, w, h, m, alpha, op, &p);
}

void soft_fill(Img *dst, float x, float y, float w, float h, const float m[6], float alpha, int op, uint32_t rgba, Img *pattern) {
    Paint p = { 0 };
    if (pattern) { p.kind = 2; p.src = pattern->px; p.sw = pattern->w; p.sh = pattern->h; }
    else { p.rgba[0] = rgba >> 24; p.rgba[1] = rgba >> 16; p.rgba[2] = rgba >> 8; p.rgba[3] = rgba; }
    raster(dst, x, y, w, h, m, alpha, op, &p);
}

// ---------- CSS colors (fillStyle strings from the data tables) ----------
#include <stdio.h>
uint32_t css(const char *s) {
    if (!s) return 0;
    if (s[0] == '#') {
        unsigned v = (unsigned)strtoul(s + 1, NULL, 16);
        size_t n = strlen(s + 1);
        if (n == 3) return ((v >> 8 & 15) * 17) << 24 | ((v >> 4 & 15) * 17) << 16 | ((v & 15) * 17) << 8 | 255;
        return v << 8 | 255;
    }
    float r = 0, g = 0, b = 0, a = 1;
    if (sscanf(s, "rgba(%f,%f,%f,%f)", &r, &g, &b, &a) >= 3 || sscanf(s, "rgb(%f,%f,%f)", &r, &g, &b) == 3)
        return (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8 | (uint32_t)(a * 255 + 0.5f);
    if (!strcmp(s, "white")) return 0xffffffff;
    if (!strcmp(s, "black")) return 0x000000ff;
    if (!strcmp(s, "cyan")) return 0x00ffffff;
    return 0;
}

void cv_fill_radial(float x, float y, float w, float h, const float *grad, int nstops) {
    if (target->screen) return;   // ponytail: gradients only ever go to offscreen canvases (see fx.c)
    Paint p = { .kind = 3, .g = grad, .gn = nstops };
    raster(target, x, y, w, h, st.m, st.alpha, st.op, &p);
}
