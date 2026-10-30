// Canvas2D-shaped drawing, so the web game's draw code ports line by line.
// Colors are 0xRRGGBBAA. Images are straight-alpha RGBA8.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct Img {
    int w, h;
    uint8_t *px;        // CPU pixels (NULL for a 3DS screen)
    void *gpu;          // 3DS: uploaded texture chunks, refreshed when dirty
    bool dirty;
    bool screen;        // a 3DS screen: drawn with the GPU
} Img;

enum { OP_OVER, OP_LIGHTER, OP_MULTIPLY, OP_ATOP, OP_DST_IN };

Img *img_load(const char *path);     // romfs path, decoded once and cached; NULL if missing
Img *img_load_ex(const char *path, bool *fresh);   // fresh = decoded by this call
Img *img_decode(const char *path);   // uncached, caller frees
Img *img_new(int w, int h);          // transparent canvas
void img_free(Img *im);
static inline uint32_t *img_at(Img *im, int x, int y) { return (uint32_t *)(im->px + (y * im->w + x) * 4); }
static inline void img_touch(Img *im) { im->dirty = true; }   // after writing px directly

Img *cv_screen(int which);           // 0 = top 400x240, 1 = bottom 320x240
void cv_target(Img *t);              // following draws go to t (resets transform, alpha, op)
void cv_push_target(Img *t);         // draw into t until cv_pop_target (state saved, no GPU switch)
void cv_pop_target(void);
void cv_save(void);
void cv_restore(void);
void cv_translate(float x, float y);
void cv_scale(float x, float y);
void cv_rotate(float a);
void cv_set_transform(float a, float b, float c, float d, float e, float f);
void cv_alpha(float a);              // globalAlpha
void cv_op(int op);                  // globalCompositeOperation
void cv_tint(uint32_t rgba);         // images draw as this color (keeping their alpha); 0 = off. For text.
void cv_image(Img *im, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh);
void cv_draw(Img *im, float dx, float dy);
void cv_fill(float x, float y, float w, float h, uint32_t rgba);
void cv_fill_pattern(Img *pat, float x, float y, float w, float h);   // repeat pattern anchored at user (0,0)
void cv_fill_radial(float x, float y, float w, float h, const float *grad, int nstops);   // grad: cx, cy, r, (offset, r, g, b, a255)*n; offscreen only
void cv_clear(void);                 // clearRect of the whole target

// backends (cv.c dispatches: screens on 3DS → gpu_*, everything else → soft_*)
void soft_image(Img *dst, Img *im, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, const float m[6], float alpha, int op, uint32_t tint);
void soft_fill(Img *dst, float x, float y, float w, float h, const float m[6], float alpha, int op, uint32_t rgba, Img *pattern);
void gpu_image(Img *im, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, const float m[6], float alpha, int op, uint32_t tint);
void gpu_fill(float x, float y, float w, float h, const float m[6], float alpha, int op, uint32_t rgba, Img *pattern);
void gpu_target(Img *screen);
void gpu_free(Img *im);

const char *plat_path(const char *path);   // romfs:/x → real path on PC

uint32_t css(const char *s);         // '#rgb', '#rrggbb', 'rgb(…)', 'rgba(…)', 'white'… → 0xRRGGBBAA
