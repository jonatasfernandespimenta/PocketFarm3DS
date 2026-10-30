// 3DS screens through citro2d. Images upload lazily as power-of-two chunks (an image dimension splits like
// its binary representation: 576 = 512 + 64), so big sheets need no padding; fully transparent chunks are
// skipped.
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cv.h"

typedef struct { C3D_Tex tex; int x, y, w, h; bool empty; } Chunk;
unsigned long texBytes;   // GPU texture memory in use (debug log)
void plog(const char *fmt, ...);
typedef struct { int n; Chunk c[]; } Chunks;

static int split(int n, int *out) {   // n → power-of-two pieces (≤ 1024, ≥ 8)
    int k = 0;
    while (n > 0) {
        int p = 1024;
        while (p > n && p > 8) p >>= 1;
        out[k++] = p;
        n -= p;
    }
    return k;
}

static void swizzle(Img *im, Chunk *c);
static void upload(Img *im) {
    Chunks *old = im->gpu;
    if (old) {   // same image, new pixels (light map, edited canvases): rewrite the textures in place
        bool same = true;
        for (int i = 0; i < old->n; i++) same &= !old->c[i].empty;
        if (same) { for (int i = 0; i < old->n; i++) swizzle(im, &old->c[i]); im->dirty = false; return; }
    }
    gpu_free(im);
    int xs[16], ys[16], nx = split(im->w, xs), ny = split(im->h, ys);
    Chunks *cs = calloc(1, sizeof *cs + nx * ny * sizeof(Chunk));
    int y0 = 0;
    for (int j = 0; j < ny; j++) {
        int x0 = 0;
        for (int i = 0; i < nx; i++) {
            Chunk *c = &cs->c[cs->n++];
            c->x = x0; c->y = y0; c->w = xs[i]; c->h = ys[j];
            int vw = im->w - x0 < c->w ? im->w - x0 : c->w, vh = im->h - y0 < c->h ? im->h - y0 : c->h;
            bool any = false;
            for (int y = 0; y < vh && !any; y++) for (int x = 0; x < vw; x++) if (im->px[((y0 + y) * im->w + x0 + x) * 4 + 3]) { any = true; break; }
            c->empty = !any;
            if (c->empty) { x0 += xs[i]; continue; }
            if (!C3D_TexInit(&c->tex, c->w, c->h, GPU_RGBA8)) {   // out of linear memory: draw nothing rather than garbage
                static int warned;
                if (warned++ < 8) plog("tex alloc failed %dx%d (image %dx%d), linear free %lu KB, textures %lu KB", c->w, c->h, im->w, im->h, (unsigned long)linearSpaceFree() / 1024, texBytes / 1024);
                c->empty = true; x0 += xs[i]; continue;
            }
            texBytes += c->w * c->h * 4;
            C3D_TexSetFilter(&c->tex, GPU_NEAREST, GPU_NEAREST);
            C3D_TexSetWrap(&c->tex, GPU_REPEAT, GPU_REPEAT);
            swizzle(im, c);
            x0 += xs[i];
        }
        y0 += ys[j];
    }
    im->gpu = cs;
    im->dirty = false;
}

static void swizzle(Img *im, Chunk *c) {
    u32 *dst = c->tex.data;
    int vw = im->w - c->x < c->w ? im->w - c->x : c->w, vh = im->h - c->y < c->h ? im->h - c->y : c->h;
    if (vw < c->w || vh < c->h) memset(dst, 0, c->w * c->h * 4);
    for (int y = 0; y < vh; y++) {
        const uint8_t *row = im->px + ((c->y + y) * im->w + c->x) * 4;
        u32 ty = y;   // citro2d subtextures: top = 1 - y/h already maps image row 0 to the first texture row
        for (int x = 0; x < vw; x++) {
            const uint8_t *s = row + x * 4;
            u32 off = (((ty >> 3) * (c->w >> 3) + (x >> 3)) << 6) +
                      ((x & 1) | ((ty & 1) << 1) | ((x & 2) << 1) | ((ty & 2) << 2) | ((x & 4) << 2) | ((ty & 4) << 3));
            dst[off] = (u32)s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3];
        }
    }
    C3D_TexFlush(&c->tex);
}

void gpu_free(Img *im) {
    Chunks *cs = im->gpu;
    if (!cs) return;
    for (int i = 0; i < cs->n; i++) if (!cs->c[i].empty) { C3D_TexDelete(&cs->c[i].tex); texBytes -= cs->c[i].w * cs->c[i].h * 4; }
    free(cs);
    im->gpu = NULL;
}

// ---------- blending ----------
static int cur_op = OP_OVER;
static void set_op(int op) {
    if (op == cur_op) return;
    C2D_Flush();
    cur_op = op;
    switch (op) {
    case OP_LIGHTER: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ONE, GPU_ONE); break;
    // exact for opaque sources, which is how the game multiplies (the day/night tint)
    case OP_MULTIPLY: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE); break;
    case -1: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ZERO, GPU_ZERO, GPU_ZERO, GPU_ZERO); break;   // clear
    // ponytail: atop / destination-in only happen on offscreen canvases (CPU); on a screen they draw as over
    default: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    }
}

static C3D_RenderTarget *rt[2];
static C3D_RenderTarget *screenTarget(int which) {
    if (!rt[which]) rt[which] = C2D_CreateScreenTarget(which ? GFX_BOTTOM : GFX_TOP, GFX_LEFT);
    return rt[which];
}
// Once per frame, right after C3D_FrameBegin: clears color AND depth of both screens. citro2d depth-tests every
// quad, and on hardware VRAM starts with the launcher's leftovers (Azahar zeroes it), so an uncleared depth buffer
// randomly rejects parts of the frame.
void gpu_beginFrame(void) {
    for (int i = 0; i < 2; i++) C2D_TargetClear(screenTarget(i), C2D_Color32(0, 0, 0, 255));
}
void gpu_target(Img *screen) {
    int which = screen->w == 400 ? 0 : 1;
    C2D_SceneBegin(screenTarget(which));
    cur_op = -2;
    set_op(OP_OVER);
}

// A local rect (x,y,w,h) through m as citro2d params: position of the (x,y) corner, signed size, angle.
// The game never skews, so the matrix is a rotation times a (possibly mirrored) scale.
static C2D_DrawParams params(float x, float y, float w, float h, const float *m) {
    C2D_DrawParams p = { .depth = 0 };
    p.pos.x = m[0] * x + m[2] * y + m[4];
    p.pos.y = m[1] * x + m[3] * y + m[5];
    if (m[1] == 0 && m[2] == 0) { p.pos.w = m[0] * w; p.pos.h = m[3] * h; p.angle = 0; }
    else {
        float ang = atan2f(m[1], m[0]), c = cosf(ang), s = sinf(ang);
        p.angle = ang;
        p.pos.w = hypotf(m[0], m[1]) * w;
        p.pos.h = (-s * m[2] + c * m[3]) * h;
    }
    return p;
}

// citro2d doesn't mirror on a negative size (it shifts the quad), so a flipped draw becomes a positive-size
// quad starting at the far corner, with the texture coordinates swapped.
static void unflip(C2D_DrawParams *p, Tex3DS_SubTexture *sub) {
    float c = cosf(p->angle), s = sinf(p->angle), t;
    if (p->pos.w < 0) { p->pos.x += p->pos.w * c; p->pos.y += p->pos.w * s; p->pos.w = -p->pos.w; t = sub->left; sub->left = sub->right; sub->right = t; }
    if (p->pos.h < 0) { p->pos.x -= p->pos.h * s; p->pos.y += p->pos.h * c; p->pos.h = -p->pos.h; t = sub->top; sub->top = sub->bottom; sub->bottom = t; }
}

void gpu_image(Img *im, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, const float m[6], float alpha, int op, uint32_t tc) {
    if (im->dirty || !im->gpu) upload(im);
    float kx = dw / sw, ky = dh / sh;
    float x0 = fmaxf(sx, 0), y0 = fmaxf(sy, 0), x1 = fminf(sx + sw, im->w), y1 = fminf(sy + sh, im->h);
    if (x1 <= x0 || y1 <= y0) return;
    set_op(op);
    C2D_ImageTint tint;
    if (tc) C2D_PlainImageTint(&tint, C2D_Color32(tc >> 24, (tc >> 16) & 255, (tc >> 8) & 255, (u32)((tc & 255) * alpha + 0.5f)), 1);
    else C2D_AlphaImageTint(&tint, alpha);
    Chunks *cs = im->gpu;
    for (int i = 0; i < cs->n; i++) {
        Chunk *c = &cs->c[i];
        if (c->empty) continue;
        float cx0 = fmaxf(x0, c->x), cy0 = fmaxf(y0, c->y), cx1 = fminf(x1, c->x + c->w), cy1 = fminf(y1, c->y + c->h);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        Tex3DS_SubTexture sub = { (u16)(cx1 - cx0), (u16)(cy1 - cy0),
            (cx0 - c->x) / c->w, 1 - (cy0 - c->y) / c->h, (cx1 - c->x) / c->w, 1 - (cy1 - c->y) / c->h };
        C2D_Image img = { &c->tex, &sub };
        C2D_DrawParams p = params(dx + (cx0 - sx) * kx, dy + (cy0 - sy) * ky, (cx1 - cx0) * kx, (cy1 - cy0) * ky, m);
        unflip(&p, &sub);
        C2D_DrawImage(img, &p, &tint);
    }
}

void gpu_fill(float x, float y, float w, float h, const float m[6], float alpha, int op, uint32_t rgba, Img *pat) {
    set_op(op);
    if (pat) {   // repeat: one quad whose UVs run past 1 (pattern images are a single power-of-two chunk)
        if (pat->dirty || !pat->gpu) upload(pat);
        Chunk *c = &((Chunks *)pat->gpu)->c[0];
        if (c->empty) return;
        Tex3DS_SubTexture sub = { (u16)w, (u16)h, x / c->w, 1 - y / c->h, (x + w) / c->w, 1 - (y + h) / c->h };
        C2D_Image img = { &c->tex, &sub };
        C2D_DrawParams p = params(x, y, w, h, m);
        unflip(&p, &sub);
        C2D_ImageTint tint;
        C2D_AlphaImageTint(&tint, alpha);
        C2D_DrawImage(img, &p, &tint);
        return;
    }
    u32 a = (u32)((rgba & 255) * alpha + 0.5f);
    u32 col = C2D_Color32(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, a);
    if (m[1] == 0 && m[2] == 0) {
        float x0 = m[0] * x + m[4], y0 = m[3] * y + m[5], x1 = x0 + m[0] * w, y1 = y0 + m[3] * h;
        C2D_DrawRectSolid(fminf(x0, x1), fminf(y0, y1), 0, fabsf(x1 - x0), fabsf(y1 - y0), col);
        return;
    }
    float px[4], py[4], cx[4] = { x, x + w, x, x + w }, cy[4] = { y, y, y + h, y + h };
    for (int i = 0; i < 4; i++) { px[i] = m[0] * cx[i] + m[2] * cy[i] + m[4]; py[i] = m[1] * cx[i] + m[3] * cy[i] + m[5]; }
    C2D_DrawTriangle(px[0], py[0], col, px[1], py[1], col, px[2], py[2], col, 0);
    C2D_DrawTriangle(px[1], py[1], col, px[3], py[3], col, px[2], py[2], col, 0);
}
