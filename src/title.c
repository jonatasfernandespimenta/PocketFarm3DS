// title.js: the title screen over the live farm. The logo goes on the top screen (over the world, like the web),
// the buttons (Jogar / Saves) and the save info on the bottom one.
#include <stdio.h>
#include "api.h"

void hud_nine(const char *src, int x, int y, int w, int h, int l, int t, int r, int b);   // hud.c
void hud_menuFrame(double dt);

static const uint8_t INK[3] = { 63, 40, 50 }, DROP[3] = { 116, 63, 57 }, HI[3] = { 255, 243, 220 };
static const uint8_t BANDS[3][3] = { { 254, 231, 97 }, { 254, 174, 52 }, { 247, 118, 34 } };

// Pixel logo: the font rendered tiny (our "px" font, Jersey 10, at 2×) to hard pixels, then extruded, outlined and
// banded in the UI palette, so it reads like hand-made pixel lettering.
// ponytail: Jersey 10 instead of Pixelify Sans 700 (the font we ship).
static Img *logoCanvas(const char *text) {
    const int S = 2, pad = 6;
    int tw = text_width("px", text) * S, W = tw + pad * 2 + 2, H = text_lineh("px") * S + 12;
    Img *src = img_new(W, H);
    cv_push_target(src);
    cv_translate(pad, 6); cv_scale(S, S);
    text_draw("px", text, 0, 0, 0xffffffff, 0);
    cv_pop_target();
    #define ON(x, y) ((x) >= 0 && (y) >= 0 && (x) < W && (y) < H && src->px[((y) * W + (x)) * 4 + 3] > 110)
    Img *out = img_new(W, H);
    #define PUT(x, y, c) do { int px_ = (x), py_ = (y); if (px_ >= 0 && py_ >= 0 && px_ < W && py_ < H) { uint8_t *d = out->px + (py_ * W + px_) * 4; d[0] = (c)[0]; d[1] = (c)[1]; d[2] = (c)[2]; d[3] = 255; } } while (0)
    int top0 = H, top1 = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (ON(x, y)) { if (y < top0) top0 = y; if (y > top1) top1 = y; }
    // extrusion (3px down), then the outline around letters + extrusion
    #define SOLID(x, y) (ON(x, y) || ON(x, (y) - 1) || ON(x, (y) - 2) || ON(x, (y) - 3))
    static const int N8[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        if (SOLID(x, y)) continue;
        for (int k = 0; k < 8; k++) if (SOLID(x + N8[k][0], y + N8[k][1])) { PUT(x, y, INK); break; }
    }
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (!ON(x, y) && SOLID(x, y)) PUT(x, y, DROP);
    int h = top1 - top0 + 1;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        if (!ON(x, y)) continue;
        double t = (double)(y - top0) / h;
        if (!ON(x, y - 1)) PUT(x, y, HI); else PUT(x, y, BANDS[t < 0.45 ? 0 : t < 0.85 ? 1 : 2]);
    }
    img_free(src);
    img_touch(out);
    return out;
}

// #title background: radial-gradient(ellipse 60% 55% at 50% 50%, rgba(25,20,30,.55), rgba(25,20,30,.15))
static Img *dimImg(void) {
    static Img *im;
    if (im) return im;
    im = img_new(200, 120);
    for (int y = 0; y < 120; y++) for (int x = 0; x < 200; x++) {
        double dx = (x + 0.5 - 100) / 120, dy = (y + 0.5 - 60) / 66, d = fmin(1, sqrt(dx * dx + dy * dy));
        uint8_t *p = im->px + (y * 200 + x) * 4;
        p[0] = 25; p[1] = 20; p[2] = 30; p[3] = (0.55 + (0.15 - 0.55) * d) * 255 + 0.5;
    }
    img_touch(im);
    return im;
}

static Img *logo;
static double T, outT = -1;   // time since shown; fade-out timer once "Play" is picked
static int focus;              // 0 = Jogar, 1 = Saves

static double easeOutBack(double k) { double c = 1.9; return 1 + (c + 1) * pow(k - 1, 3) + c * pow(k - 1, 2); }

static void button(const char *src, int x, int y, int w, int h, const char *label, uint32_t color, uint32_t shadow, bool foc) {
    hud_nine(src, x, y, w, h, 5, 5, 5, 8);
    int tw = text_width("px", label);
    if (shadow) text_draw("px", label, x + (w - tw) / 2, y + (h - 3 - text_lineh("px")) / 2 + 1, shadow, 0);
    text_draw("px", label, x + (w - tw) / 2, y + (h - 3 - text_lineh("px")) / 2, color, 0);
    if (foc) { cv_fill(x - 2, y - 2, w + 4, 1, 0xfee761ff); cv_fill(x - 2, y + h + 1, w + 4, 1, 0xfee761ff); cv_fill(x - 2, y - 2, 1, h + 4, 0xfee761ff); cv_fill(x + w + 1, y - 2, 1, h + 4, 0xfee761ff); }
}

bool title_frame(double dt) {
    if (!logo) logo = logoCanvas("PocketFarm");
    T += dt;
    double a = fmin(1, T / 0.4);   // dim .4s
    if (outT >= 0) { outT += dt; a = fmax(0, 1 - outT / 0.25); }
    bool hasSave = cJSON_IsTrue(jget(state, "tutorialDone")) || num(state, "day") > 1;
    int menu = hud_isMenuOpen();

    // input (not while the Saves menu is up)
    int bx = 70, bw = 180, bh = 32, by0 = 96, by1 = by0 + bh + 6;
    if (!menu && outT < 0) {
        if (in.down & (BTN_UP | BTN_DOWN)) { focus ^= 1; sfx("click"); }
        bool play = (in.down & BTN_A && focus == 0) || (in.touchDown && in.tx >= bx && in.tx < bx + bw && in.ty >= by0 && in.ty < by0 + bh);
        bool saves = (in.down & BTN_A && focus == 1) || (in.touchDown && in.tx >= bx && in.tx < bx + bw && in.ty >= by1 && in.ty < by1 + bh);
        if (play) { sfx("click"); outT = 0; }
        else if (saves) { sfx("click"); shops_savesMenu(); }
    }

    // top screen: dim + logo over the world (it was drawn before hud_frame)
    cv_target(cv_screen(0));
    cv_alpha(a);
    cv_image(dimImg(), 0, 0, 200, 120, 0, 0, 400, 240);
    double k = fmin(1, T / 0.7), s = 2 * (0.8 + 0.2 * easeOutBack(k));
    double bob = T > 0.7 ? -2 * (1 - cos((T - 0.7) * 2 * M_PI / 2.4)) : 0;
    double ly = 120 - logo->h * s / 2 - 40 * (1 - easeOutBack(k)) + bob, lx = 200 - logo->w * s / 2;
    cv_alpha(a * fmin(1, k * 2));
    cv_save();
    cv_translate(jround(lx), jround(ly)); cv_scale(s, s);
    cv_tint(0x19141eff); cv_alpha(a * fmin(1, k * 2) * 0.45);   // drop-shadow(0 4px 0 rgba(25,20,30,.45))
    cv_draw(logo, 0, 2);
    cv_tint(0); cv_alpha(a * fmin(1, k * 2));
    cv_draw(logo, 0, 0);
    cv_restore();
    cv_alpha(1);
    cv_target(cv_screen(1));

    // bottom screen: the backdrop the HUD uses, dimmed, then logo (1×), buttons, info
    cv_fill(0, 0, 320, 240, 0x193c3eff);
    for (int y = 0; y < 240; y += 8) for (int x = (y / 8) % 2 * 8; x < 320; x += 16) cv_fill(x, y, 8, 8, 0x1d4446ff);
    cv_image(dimImg(), 0, 0, 200, 120, 0, 0, 320, 240);
    cv_alpha(a);
    cv_draw(logo, 160 - logo->w / 2, 30);
    button("assets/ui/btn-red.png", bx, by0, bw, bh, hasSave ? "Continue" : "Play", 0xffffffff, 0x9e2835ff, !menu && focus == 0);
    button("assets/ui/btn.png", bx, by1, bw, bh, "Saves", 0x3f2832ff, 0, !menu && focus == 1);
    char info[96];
    if (hasSave) {
        char m[24], t[24]; int v = num(state, "money"), o = 0;
        snprintf(t, sizeof t, "%d", v);
        for (int i = 0, len = strlen(t); i < len; i++) { m[o++] = t[i]; if ((len - i - 1) % 3 == 0 && i < len - 1 && t[i] != '-') m[o++] = ','; }
        m[o] = 0;
        int days = num(DATA, "SEASON_DAYS"), d = num(state, "day");
        snprintf(info, sizeof info, "%s %d · %s coins", str(jat(table("SEASONS"), ((d - 1) / days) % 4), "name"), (d - 1) % days + 1, m);
    } else snprintf(info, sizeof info, "A pocket-sized farm");
    int iw = text_width("px", info);
    cv_alpha(a * 0.8);
    text_draw("px", info, 160 - iw / 2, by1 + bh + 11, 0x3f2832ff, 0);
    text_draw("px", info, 160 - iw / 2, by1 + bh + 10, 0xfff3dcff, 0);
    cv_alpha(1);
    if (menu) hud_menuFrame(dt);

    if (outT >= 0.25) {   // gone: free it all, the game starts
        img_free(logo); logo = NULL;
        return false;
    }
    return true;
}
