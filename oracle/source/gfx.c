// Software Canvas2D core. Every canvas is a straight-alpha RGBA8 ArrayBuffer owned by JS (so
// getImageData/putImageData are free); these natives do the per-pixel work JS is too slow for.
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef uint8_t u8;
typedef uint32_t u32;

enum { OP_OVER, OP_LIGHTER, OP_MULTIPLY, OP_ATOP, OP_DST_IN, OP_CLEAR };

static inline u8 clamp8(float v) { return v <= 0 ? 0 : v >= 255 ? 255 : (u8)(v + 0.5f); }

// Composites one straight-alpha source pixel (alpha already scaled by globalAlpha) onto d.
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
    if (op == OP_CLEAR) { d[0] = d[1] = d[2] = d[3] = 0; return; }
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

// ---------- paint sources ----------
typedef struct {
    int kind;                    // 0 color, 1 image (blit), 2 pattern, 3 radial gradient
    u8 rgba[4];
    const u8 *src; int sw, sh;   // image / pattern pixels
    float sx, sy, kx, ky;        // image: user coords -> source texel
    const double *g; int gn;     // radial: x0,y0,r0,x1,y1,r1, then (offset,r,g,b,a)*gn
} Paint;

static inline bool sample(const Paint *p, float ux, float uy, int *r, int *g, int *b, int *a) {
    switch (p->kind) {
    case 0: *r = p->rgba[0]; *g = p->rgba[1]; *b = p->rgba[2]; *a = p->rgba[3]; return true;
    case 1: {
        int tx = (int)floorf(p->sx + ux * p->kx), ty = (int)floorf(p->sy + uy * p->ky);
        if (tx < 0 || ty < 0 || tx >= p->sw || ty >= p->sh) return false;
        const u8 *s = p->src + (ty * p->sw + tx) * 4;
        *r = s[0]; *g = s[1]; *b = s[2]; *a = s[3]; return true;
    }
    case 2: {
        int tx = (int)floorf(ux) % p->sw, ty = (int)floorf(uy) % p->sh;
        if (tx < 0) tx += p->sw;
        if (ty < 0) ty += p->sh;
        const u8 *s = p->src + (ty * p->sw + tx) * 4;
        *r = s[0]; *g = s[1]; *b = s[2]; *a = s[3]; return true;
    }
    default: {
        // two-circle radial gradient, the common concentric-ish case: t from distance blend
        const double *q = p->g;
        float dx = ux - q[3], dy = uy - q[4];
        float r0 = q[2], r1 = q[5];
        float t = r1 > r0 ? (sqrtf(dx * dx + dy * dy) - r0) / (r1 - r0) : 0;
        const double *st = q + 6;
        if (t <= st[0] || p->gn == 1) { *r = st[1]; *g = st[2]; *b = st[3]; *a = st[4]; return true; }
        for (int i = 1; i < p->gn; i++) {
            const double *s0 = st + (i - 1) * 5, *s1 = st + i * 5;
            if (t <= s1[0]) {
                float k = s1[0] > s0[0] ? (t - s0[0]) / (s1[0] - s0[0]) : 1;
                *r = s0[1] + (s1[1] - s0[1]) * k; *g = s0[2] + (s1[2] - s0[2]) * k;
                *b = s0[3] + (s1[3] - s0[3]) * k; *a = s0[4] + (s1[4] - s0[4]) * k;
                return true;
            }
        }
        const double *sl = st + (p->gn - 1) * 5;
        *r = sl[1]; *g = sl[2]; *b = sl[3]; *a = sl[4]; return true;
    }
    }
}

// Paints user-space rect (x,y,w,h) through matrix m onto dst. Axis-aligned transforms (nearly every
// draw in the game) step per row/column; rotations fall back to per-pixel inverse mapping.
static void raster(u8 *dst, int dw, int dh, float x, float y, float w, float h, const double *m,
                   float alpha, int op, const Paint *p) {
    if (w <= 0 || h <= 0) return;
    float a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5];
    float det = a * d - b * c;
    if (fabsf(det) < 1e-9f) return;
    float ia = d / det, ib = -b / det, ic = -c / det, id = a / det;
    float ie = (c * f - d * e) / det, iff = (b * e - a * f) / det;
    int x0, y0, x1, y1;
    if (op == OP_DST_IN) { x0 = 0; y0 = 0; x1 = dw; y1 = dh; }   // destination-in also wipes everything it doesn't cover
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
        u8 *row = dst + py * dw * 4;
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
}

// ---------- natives ----------
static u8 *buf(JSContext *ctx, JSValueConst v, size_t need) {
    size_t n; u8 *p = JS_GetArrayBuffer(ctx, &n, v);
    return p && n >= need ? p : NULL;
}
static double num(JSContext *ctx, JSValueConst v) { double d = 0; JS_ToFloat64(ctx, &d, v); return d; }
static void mat(JSContext *ctx, JSValueConst v, double m[6]) {
    for (int i = 0; i < 6; i++) { JSValue e = JS_GetPropertyUint32(ctx, v, i); m[i] = num(ctx, e); JS_FreeValue(ctx, e); }
}

// blit(dst, dw, dh, src, sw, sh, sx, sy, sW, sH, dx, dy, dW, dH, matrix[6], alpha, op)
static JSValue n_blit(JSContext *ctx, JSValueConst this, int argc, JSValueConst *v) {
    int dw = num(ctx, v[1]), dh = num(ctx, v[2]), sw = num(ctx, v[4]), sh = num(ctx, v[5]);
    u8 *dst = buf(ctx, v[0], (size_t)dw * dh * 4), *src = buf(ctx, v[3], (size_t)sw * sh * 4);
    if (!dst || !src) return JS_ThrowTypeError(ctx, "blit: bad buffer");
    float sx = num(ctx, v[6]), sy = num(ctx, v[7]), sW = num(ctx, v[8]), sH = num(ctx, v[9]);
    float dx = num(ctx, v[10]), dy = num(ctx, v[11]), dW = num(ctx, v[12]), dH = num(ctx, v[13]);
    double m[6]; mat(ctx, v[14], m);
    if (sW <= 0 || sH <= 0) return JS_UNDEFINED;
    Paint p = { .kind = 1, .src = src, .sw = sw, .sh = sh, .kx = sW / dW, .ky = sH / dH };
    p.sx = sx - dx * p.kx; p.sy = sy - dy * p.ky;
    // clamp the source rect to the image: the user rect shrinks with it, like the browser does
    float cx0 = fmaxf(sx, 0), cy0 = fmaxf(sy, 0), cx1 = fminf(sx + sW, sw), cy1 = fminf(sy + sH, sh);
    raster(dst, dw, dh, dx + (cx0 - sx) / p.kx, dy + (cy0 - sy) / p.ky, (cx1 - cx0) / p.kx, (cy1 - cy0) / p.ky,
           m, num(ctx, v[15]), num(ctx, v[16]), &p);
    return JS_UNDEFINED;
}

// fill(dst, dw, dh, x, y, w, h, matrix[6], alpha, op, rgba32 | Float64Array gradient | {data,w,h} pattern)
static JSValue n_fill(JSContext *ctx, JSValueConst this, int argc, JSValueConst *v) {
    int dw = num(ctx, v[1]), dh = num(ctx, v[2]);
    u8 *dst = buf(ctx, v[0], (size_t)dw * dh * 4);
    if (!dst) return JS_ThrowTypeError(ctx, "fill: bad buffer");
    double m[6]; mat(ctx, v[7], m);
    Paint p = { 0 };
    JSValueConst paint = v[10];
    if (JS_IsNumber(paint)) {
        u32 c = (u32)num(ctx, paint);
        p.rgba[0] = c >> 24; p.rgba[1] = c >> 16; p.rgba[2] = c >> 8; p.rgba[3] = c;
    } else {
        size_t off, len, bpe; JSValue ab = JS_GetTypedArrayBuffer(ctx, paint, &off, &len, &bpe);
        if (!JS_IsException(ab)) {
            size_t n; u8 *raw = JS_GetArrayBuffer(ctx, &n, ab);
            JS_FreeValue(ctx, ab);
            p.kind = 3; p.g = (const double *)(raw + off); p.gn = (len / 8 - 6) / 5;
            if (p.gn < 1) return JS_UNDEFINED;
        } else {
            JS_FreeValue(ctx, JS_GetException(ctx));
            JSValue d = JS_GetPropertyStr(ctx, paint, "data"), pw = JS_GetPropertyStr(ctx, paint, "width"), ph = JS_GetPropertyStr(ctx, paint, "height");
            p.kind = 2; p.sw = num(ctx, pw); p.sh = num(ctx, ph);
            p.src = buf(ctx, d, (size_t)p.sw * p.sh * 4);
            JS_FreeValue(ctx, d); JS_FreeValue(ctx, pw); JS_FreeValue(ctx, ph);
            if (!p.src || p.sw <= 0 || p.sh <= 0) return JS_UNDEFINED;
        }
    }
    raster(dst, dw, dh, num(ctx, v[3]), num(ctx, v[4]), num(ctx, v[5]), num(ctx, v[6]), m, num(ctx, v[8]), num(ctx, v[9]), &p);
    return JS_UNDEFINED;
}

// decodePNG(ArrayBuffer) -> { width, height, data: ArrayBuffer RGBA } | null
static JSValue n_decodePNG(JSContext *ctx, JSValueConst this, int argc, JSValueConst *v) {
    size_t n; u8 *p = JS_GetArrayBuffer(ctx, &n, v[0]);
    if (!p) return JS_NULL;
    int w, h, ch;
    u8 *px = stbi_load_from_memory(p, n, &w, &h, &ch, 4);
    if (!px) return JS_NULL;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "width", JS_NewInt32(ctx, w));
    JS_SetPropertyStr(ctx, o, "height", JS_NewInt32(ctx, h));
    JS_SetPropertyStr(ctx, o, "data", JS_NewArrayBufferCopy(ctx, px, (size_t)w * h * 4));
    stbi_image_free(px);
    return o;
}

// present(buf, w, h, screen 0=top 1=bottom, bgRGB)
static JSValue n_present(JSContext *ctx, JSValueConst this, int argc, JSValueConst *v) {
    int w = num(ctx, v[1]), h = num(ctx, v[2]);
    u8 *src = buf(ctx, v[0], (size_t)w * h * 4);
    if (!src) return JS_ThrowTypeError(ctx, "present: bad buffer");
    platform_present(src, w, h, num(ctx, v[3]), (u32)num(ctx, v[4]));
    return JS_UNDEFINED;
}

// savePPM(buf, w, h, path): debug screenshot of a canvas (RGB over black)
static JSValue n_savePPM(JSContext *ctx, JSValueConst this, int argc, JSValueConst *v) {
    int w = num(ctx, v[1]), h = num(ctx, v[2]);
    u8 *src = buf(ctx, v[0], (size_t)w * h * 4);
    const char *path = JS_ToCString(ctx, v[3]);
    char real[512];
    FILE *f = src && path ? fopen(platform_path(path, real, sizeof real), "wb") : NULL;
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) { u8 *s = src + i * 4; u8 o[3] = { s[0] * s[3] / 255, s[1] * s[3] / 255, s[2] * s[3] / 255 }; fwrite(o, 1, 3, f); }
        fclose(f);
    }
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry fns[] = {
    JS_CFUNC_DEF("savePPM", 4, n_savePPM),
    JS_CFUNC_DEF("blit", 17, n_blit),
    JS_CFUNC_DEF("fill", 11, n_fill),
    JS_CFUNC_DEF("decodePNG", 1, n_decodePNG),
    JS_CFUNC_DEF("present", 5, n_present),
};

void gfx_register(JSContext *ctx, JSValue native) {
    JS_SetPropertyFunctionList(ctx, native, fns, sizeof(fns) / sizeof(fns[0]));
}
