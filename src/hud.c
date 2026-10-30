// Bottom-screen UI (hud.js + index.html + style.css): HUD, hotbar, menus, album grid, toasts, floaters, fade.
// The web lays these out with the DOM; here they are drawn every frame with the same art (9-slice frames from
// assets/ui, Cute Fantasy UI icons, Jersey 10 / Nunito) and driven by touch or D-pad + A / B.
#include <stdarg.h>
#include <stdio.h>
#include "game.h"

#define SW 320
#define SH 240
#define INK 0x3f2832ff
#define BROWN 0x743f39ff
#define TANHI 0xead4aaff
#define TANLO 0xb86f50ff
#define CREAM 0xfff3dcff
#define BG 0x193c3eff

// ---------- fonts ----------
typedef struct { int x, y, w, h, ox, oy, adv; } Glyph;
typedef struct { const char *name; Img *img; int asc, desc; Glyph ascii[256]; int nextra; struct { int cp; Glyph g; } extra[32]; } Font;
static Font fonts[3] = { { "px" }, { "body" }, { "small" } };

static Font *font(const char *name) {
    Font *f = &fonts[0];
    for (int i = 0; i < 3; i++) if (!strcmp(fonts[i].name, name)) f = &fonts[i];
    if (!f->img) {
        char p[64];
        snprintf(p, sizeof p, "romfs:/fonts/%s.png", f->name);
        f->img = img_load(p);
        snprintf(p, sizeof p, "romfs:/fonts/%s.json", f->name);
        char *t = readFile(p, NULL);
        J *j = cJSON_Parse(t);
        free(t);
        f->asc = num(j, "ascent"); f->desc = num(j, "descent");
        JFOR(g, jget(j, "glyphs")) {
            int cp = atoi(g->string);
            Glyph gl = { numv(jat(g, 0)), numv(jat(g, 1)), numv(jat(g, 2)), numv(jat(g, 3)), numv(jat(g, 4)), numv(jat(g, 5)), numv(jat(g, 6)) };
            if (cp < 256) f->ascii[cp] = gl;
            else if (f->nextra < 32) { f->extra[f->nextra].cp = cp; f->extra[f->nextra++].g = gl; }
        }
        cJSON_Delete(j);
    }
    return f;
}
static Glyph *glyph(Font *f, int cp) {
    if (cp < 256) return &f->ascii[cp];
    for (int i = 0; i < f->nextra; i++) if (f->extra[i].cp == cp && f->extra[i].g.w) return &f->extra[i].g;
    // ponytail: glyphs the fonts lack fall back to ASCII look-alikes
    return &f->ascii[cp == 0x2192 ? '>' : cp == 0x2190 ? '<' : cp == 0x2605 ? '*' : cp == 0x2665 ? '*' : cp == 0x2022 ? '-' : '?'];
}
static int utf8(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p++;
    if (c >= 0xf0) { c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63); p += 3; }
    else if (c >= 0xe0) { c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63); p += 2; }
    else if (c >= 0xc0) { c = (c & 31) << 6 | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}
int text_lineh(const char *name) { Font *f = font(name); return f->asc + f->desc + (f == &fonts[0] ? 0 : 1); }

// ---------- icons (hud.js ICONS) ----------
#define UI "assets/Cute_Fantasy_UI/UI/"
typedef struct { const char *name; const char *src; int sx, sy, w, h; const char *pixel; } Icon;
#define U(n, c, r) { n, UI "UI_Icons.png", (c) * 16, (r) * 16, 16, 16 }
#define B(n, c, r) { n, UI "UI_Button_Icons.png", (c) * 16, (r) * 16, 16, 16 }
static const Icon ICONS[] = {
    U("heart", 0, 0), U("heart_empty", 2, 0), U("star", 3, 0), U("coin", 6, 0), U("energy", 9, 0),
    U("talk", 0, 1), U("wrench", 3, 1), U("crown", 4, 1), U("trophy", 5, 1), U("gift", 8, 1), U("book", 10, 1), U("mail", 14, 1),
    U("quest", 12, 2), U("coins", 13, 3), U("check", 0, 6), U("sparkle", 9, 3), U("hand", 3, 14),
    U("bag", 9, 2), U("save", 9, 1), U("gear", 2, 1), U("music", 9, 4), U("sound", 10, 4), U("music_off", 11, 4), U("sound_off", 12, 4),
    U("arrow_r", 10, 9), U("arrow_d", 4, 9), U("trash", 14, 3), U("book_red", 12, 1), U("book_green", 11, 1), U("question", 12, 3), U("talk2", 11, 2),
    B("home", 6, 1), B("close", 12, 1), B("rotate", 30, 1),
    { "ticket", NULL, 0, 0, 0, 0, "ticket" }, { "menu", NULL, 0, 0, 0, 0, "menu" }, { "lock", NULL, 0, 0, 0, 0, "lock" },
    { "chair", "assets/Cute_Fantasy/Buildings/House_Decor/Chairs.png", 16, 8, 16, 24 },
};
static const Icon *iconDef(const char *name, int len) {
    for (size_t i = 0; i < sizeof ICONS / sizeof *ICONS; i++) if ((int)strlen(ICONS[i].name) == len && !strncmp(ICONS[i].name, name, len)) return &ICONS[i];
    return NULL;
}
static int iconW(const Icon *ic) { return ic->pixel ? sprite(ic->pixel)->w : ic->w; }
// UI icons draw at their pixel size, scaled to the text line when the font is small
static void drawUIIcon(const Icon *ic, int x, int y, int h) {
    if (ic->pixel) { Img *s = sprite(ic->pixel); cv_image(s, 0, 0, s->w, s->h, x, y + (h - s->h) / 2, s->w, s->h); return; }
    int dh = ic->h > h ? h : ic->h, dw = ic->w * dh / ic->h;
    cv_image(I(ic->src), ic->sx, ic->sy, ic->w, ic->h, x, y + (h - dh) / 2, dw, dh);
}

static J *critterById(int id) { JFOR(c, table("CRITTERS")) if (num(c, "id") == id) return c; return NULL; }

void drawIcon(const char *spec, int x, int y, int scale) {
    if (!spec) return;
    if (!strncmp(spec, "item:", 5)) {
        J *ic = jget(jget(table("ITEMS"), spec + 5), "icon");
        if (!ic) return;
        if (str(ic, "pixel")) { Img *s = sprite(str(ic, "pixel")); cv_image(s, 0, 0, s->w, s->h, x, y, s->w * scale, s->h * scale); return; }
        cv_image(I(str(ic, "src")), num(ic, "sx"), num(ic, "sy"), 16, 16, x, y, 16 * scale, 16 * scale);
    } else if (!strncmp(spec, "tool:", 5)) {
        const char *id = spec + 5;
        J *t = jget(table("TOOLS"), id);
        char b[64];
        if (jget(t, "icon")) cv_image(I(str(DATA, "TOOL_ICONS")), numv(jat(jget(t, "icon"), 0)) * 16, 0, 16, 16, x, y, 16 * scale, 16 * scale);
        else if (!strcmp(id, "seeds")) { snprintf(b, sizeof b, "item:seed_%s", str(state, "seed")); drawIcon(b, x, y, scale); }
        else if (!strcmp(id, "build") && str(state, "build")) { snprintf(b, sizeof b, "item:machine_%s", str(state, "build")); drawIcon(b, x, y, scale); }
        else { const Icon *ic = iconDef(!strcmp(id, "build") ? "wrench" : "hand", !strcmp(id, "build") ? 6 : 4); drawUIIcon(ic, x, y, 16 * scale); }
    } else if (!strncmp(spec, "critter:", 8)) {
        J *c = critterById(atoi(spec + 8));
        int size = num(c, "size") ? num(c, "size") : 32, d = 16 * scale;
        cv_image(I(str(c, "sprite")), 0, 0, size, size, x, y, d, d);
    } else if (!strncmp(spec, "furniture:", 10)) {   // fitted into a 16×16 box (decorIcon)
        J *f = jget(table("FURNITURE"), spec + 10);
        Img *im = str(f, "pixel") ? sprite(str(f, "pixel")) : I(str(f, "src"));
        if (!im) return;
        int r[4] = { 0, 0, im->w, im->h };
        if (jget(f, "rect")) for (int k = 0; k < 4; k++) r[k] = numv(jat(jget(f, "rect"), k));
        double k = 16.0 * scale / fmax(r[2], r[3]);
        cv_image(im, r[0], r[1], r[2], r[3], x + (16 * scale - r[2] * k) / 2, y + (16 * scale - r[3] * k) / 2, r[2] * k, r[3] * k);
    } else if (!strncmp(spec, "sprite:", 7)) {
        Img *s = sprite(spec + 7);
        cv_image(s, 0, 0, s->w, s->h, x, y, s->w * scale, s->h * scale);
    } else {
        const char *n = spec[0] == ':' ? spec + 1 : spec;
        int len = strcspn(n, ":");
        const Icon *ic = iconDef(n, len);
        if (ic) drawUIIcon(ic, x, y, 16 * scale);
    }
}

// ---------- rich text ----------
// Splits into words; ':name:' known icons are words of their own. Draw = lay out with wrapping at maxw.
static int layout(const char *fname, const char *s, int x0, int y0, uint32_t color, int maxw, bool draw) {
    Font *f = font(fname);
    int lh = text_lineh(fname), x = x0, y = y0, widest = 0;
    if (draw) cv_tint(color);
    const char *p = s;
    while (p && *p) {
        if (*p == '\n') { x = x0; y += lh; p++; continue; }
        // measure the next word (up to a space or newline), icons included
        const char *w = p;
        int ww = 0;
        while (*w && *w != ' ' && *w != '\n') {
            if (*w == ':') {
                int len = strspn(w + 1, "abcdefghijklmnopqrstuvwxyz_");
                const Icon *ic = len && w[1 + len] == ':' ? iconDef(w + 1, len) : NULL;
                if (ic) { ww += iconW(ic) * (lh >= 14 ? 1 : 1) + 1; w += len + 2; continue; }
            }
            const char *q = w;
            ww += glyph(f, utf8(&q))->adv;
            w = q;
        }
        if (maxw && x > x0 && x - x0 + ww > maxw) { x = x0; y += lh; }
        // draw it
        while (p < w) {
            if (*p == ':') {
                int len = strspn(p + 1, "abcdefghijklmnopqrstuvwxyz_");
                const Icon *ic = len && p[1 + len] == ':' ? iconDef(p + 1, len) : NULL;
                if (ic) {
                    if (draw) { cv_tint(0); drawUIIcon(ic, x, y - 1, lh + 2 > 16 ? 16 : lh + 2); cv_tint(color); }
                    x += iconW(ic) * (lh + 2 >= 16 ? 1 : 1) + 1; p += len + 2; continue;
                }
            }
            const char *q = p;
            Glyph *g = glyph(f, utf8(&q));
            if (draw && g->w) cv_image(f->img, g->x, g->y, g->w, g->h, x + g->ox, y + g->oy, g->w, g->h);
            x += g->adv;
            p = q;
        }
        if (x - x0 > widest) widest = x - x0;
        if (*p == ' ') { x += glyph(f, ' ')->adv; p++; }
    }
    if (draw) cv_tint(0);
    return maxw ? y - y0 + lh : widest;
}
int text_width(const char *f, const char *s) { return layout(f, s, 0, 0, 0, 0, false); }
int text_draw(const char *f, const char *s, int x, int y, uint32_t color, int maxw) { layout(f, s, x, y, color, maxw, true); return layout(f, s, 0, 0, 0, maxw ? maxw : 100000, false); }
static int text_height(const char *f, const char *s, int maxw) { return layout(f, s, 0, 0, 0, maxw, false); }
// px text with a 1px ink outline and a 2px drop (the floaters / slot counts)
static void text_outlined(const char *f, const char *s, int x, int y, uint32_t color) {
    static const int O[][2] = { { 0, 2 }, { 1, 0 }, { -1, 0 }, { 0, -1 }, { 1, 2 }, { -1, 2 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 8; i++) layout(f, s, x + O[i][0], y + O[i][1], INK, 0, true);
    layout(f, s, x, y, color, 0, true);
}
static void center(const char *f, const char *s, int cx, int y, uint32_t c) { text_draw(f, s, cx - text_width(f, s) / 2, y, c, 0); }

// ---------- 9-slice frames ----------
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
#define FRAME(x, y, w, h) nine("assets/ui/btn.png", x, y, w, h, 5, 5, 5, 8)
#define PAPER(x, y, w, h) nine("assets/ui/paper-small.png", x, y, w, h, 4, 4, 4, 4)
#define SLOT(x, y, w, h) nine("assets/ui/slot.png", x, y, w, h, 4, 4, 4, 4)
#define GOLD(x, y, w, h) nine("assets/ui/btn-gold-down.png", x, y, w, h, 5, 5, 5, 5)

static void fmtMoney(char *b, int n, int v) {   // toLocaleString('en-US'): 12,345
    char t[32]; snprintf(t, sizeof t, "%d", v < 0 ? -v : v);
    int len = strlen(t), o = 0;
    if (v < 0) b[o++] = '-';
    for (int i = 0; i < len && o < n - 2; i++) { b[o++] = t[i]; if ((len - i - 1) % 3 == 0 && i < len - 1) b[o++] = ','; }
    b[o] = 0;
}

// ---------- touch ----------
static bool tapped(int x, int y, int w, int h) { return in.touchDown && in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h; }

// ---------- toasts / floaters / fade (top screen) ----------
static char toastMsg[256]; static double toastT;
void toast(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(toastMsg, sizeof toastMsg, fmt, ap); va_end(ap);
    toastT = 2.2;
}
typedef struct { char text[96]; double wx, wy, t; } Floater;
static Floater floaters[16]; static int nfloat;
void floatText(double wx, double wy, const char *fmt, ...) {
    if (nfloat == 16) return;
    Floater *f = &floaters[nfloat++];
    va_list ap; va_start(ap, fmt); vsnprintf(f->text, sizeof f->text, fmt, ap); va_end(ap);
    f->wx = wx; f->wy = wy; f->t = 1.2;
}
void hud_drawFloaters(double camx, double camy, int k) {
    for (int i = 0; i < nfloat; i++) {
        Floater *f = &floaters[i];
        double y = f->wy - (1.2 - f->t) * 14;
        int w = text_width("px", f->text), h = text_lineh("px");
        cv_alpha(fmin(1, f->t * 2));
        text_outlined("px", f->text, jround((f->wx - camx) * k) - w / 2, jround((y - camy) * k) - h, CREAM);
        cv_alpha(1);
    }
}
static void (*fadeFn)(void); static double fadeT; static int fadePhase;   // 1 = darkening, 2 = clearing
void hud_fade(void (*fn)(void)) { fadeFn = fn; fadeT = 0; fadePhase = 1; }

// ---------- HUD state ----------
static const char *hot[16]; static int nhot;
static char objLabel[160], objIcon[48]; static bool objOn, objTap;
static char qtTitle[3][96], qtProg[3][48]; static int nqt, qtMore;
static char hintTool[16]; static bool hintMenu;
static bool decorOn;
void hud_render(void) {}
void hud_buildHotbar(const char **tools, int n) { nhot = n; for (int i = 0; i < n; i++) hot[i] = tools[i]; }
void hud_decor(bool on) { decorOn = on; }
void hud_objective(const char *icon, const char *label, bool tappable) {
    objOn = label != NULL;
    if (label) snprintf(objLabel, sizeof objLabel, "%s", label);
    snprintf(objIcon, sizeof objIcon, "%s", icon ? icon : "");
    objTap = tappable;
}
void hud_setQuests(const char **titles, const char **progress, int n) {
    nqt = n > 3 ? 3 : n; qtMore = n > 3 ? n - 3 : 0;
    for (int i = 0; i < nqt; i++) { snprintf(qtTitle[i], 96, "%s", titles[i]); snprintf(qtProg[i], 48, "%s", progress[i] ? progress[i] : ""); }
}
void hud_hint(const char *tool, bool menuButton) { snprintf(hintTool, sizeof hintTool, "%s", tool ? tool : ""); hintMenu = menuButton; }

// ---------- menus ----------
enum { R_TEXT, R_ROW, R_SAY, R_TITLE, R_SLOT, R_DIAGRAM, R_INPUT, R_CANVAS };
struct Row {
    int kind;
    char *label, *sub, *icon, *extra;      // extra: slot short name / input button / say portrait
    int price, count; bool hasPrice, disabled, stay, selected, dim;
    RowFn fn; char arg[160];
    RowFn pick; char parg[96];
    char *actLabel; RowFn act; char aarg[96];
    void (*input)(const char *, void *); void (*draw)(int, int, int, void *);
    const char *dicons[12], *dlabels[12]; int dn;
    int h;
};
static Row rows[160]; static int nrows;
static char menuTitle[128];
static bool menuOpen;
static void (*refreshFn)(void *); static char refreshArg[160];
static double scroll; static int focus = -1, selSlot = -1;
static double sayT;
static bool dragging; static int dragY0, touchY0; static double scroll0;
static bool (*overlay)(double);

static char *dup(const char *s) { return s ? strdup(s) : NULL; }
static void freeRows(void) {
    for (int i = 0; i < nrows; i++) { Row *r = &rows[i]; free(r->label); free(r->sub); free(r->icon); free(r->extra); free(r->actLabel); for (int k = 0; k < r->dn; k++) { free((char *)r->dicons[k]); free((char *)r->dlabels[k]); } }
    nrows = 0;
}
static Row *add(int kind) { if (nrows == 160) nrows--; Row *r = &rows[nrows++]; memset(r, 0, sizeof *r); r->kind = kind; return r; }

void menu_begin(const char *title) {
    freeRows();
    snprintf(menuTitle, sizeof menuTitle, "%s", title ? title : "");
    refreshFn = NULL;
}
void menu_refresh(void (*fn)(void *), const void *arg, size_t n) { refreshFn = fn; if (arg) memcpy(refreshArg, arg, n); }
void menu_text(const char *fmt, ...) {
    char b[600]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    add(R_TEXT)->label = strdup(b);
}
Row *menu_row(const char *label, const char *sub, RowFn fn, const void *arg, size_t n) {
    if (label && !strcmp(label, "Close") && !fn) { static Row dummy; memset(&dummy, 0, sizeof dummy); return &dummy; }   // the header's X closes
    Row *r = add(R_ROW);
    r->label = dup(label); r->sub = dup(sub); r->fn = fn;
    if (arg) memcpy(r->arg, arg, n < sizeof r->arg ? n : sizeof r->arg);
    return r;
}
void row_icon(Row *r, const char *icon) { free(r->icon); r->icon = dup(icon); }
void row_price(Row *r, int price) { r->price = price; r->hasPrice = true; }
void row_disabled(Row *r) { r->disabled = true; }
void row_stay(Row *r) { r->stay = true; }
void menu_say(const char *name, const char *portrait, const char *text) { Row *r = add(R_SAY); r->label = dup(name); r->extra = dup(portrait); r->sub = dup(text); sayT = 0; }
void menu_title(const char *title) { add(R_TITLE)->label = dup(title); }
void menu_slot(const char *icon, int count, const char *name, const char *shortName, const char *info, bool selected) {
    Row *r = add(R_SLOT);
    r->icon = dup(icon); r->count = count; r->label = dup(name); r->extra = dup(shortName); r->sub = dup(info); r->selected = selected;
}
static Row *lastSlot(void) { return nrows ? &rows[nrows - 1] : NULL; }
void slot_pick(RowFn fn, const void *arg, size_t n) { Row *r = lastSlot(); r->pick = fn; if (arg) memcpy(r->parg, arg, n < sizeof r->parg ? n : sizeof r->parg); }
void slot_action(const char *label, RowFn fn, const void *arg, size_t n) { Row *r = lastSlot(); r->actLabel = dup(label); r->act = fn; if (arg) memcpy(r->aarg, arg, n < sizeof r->aarg ? n : sizeof r->aarg); }
void slot_dim(void) { lastSlot()->dim = true; }
void menu_diagram(const char **icons, const char **labels, int n) {
    Row *r = add(R_DIAGRAM);
    for (int i = 0; i < n && i < 12; i++) { r->dicons[i] = dup(icons[i]); r->dlabels[i] = dup(labels ? labels[i] : NULL); }
    r->dn = n < 12 ? n : 12;
}
void menu_input(const char *placeholder, const char *value, const char *button, void (*fn)(const char *, void *), const void *arg, size_t n) {
    Row *r = add(R_INPUT);
    r->sub = dup(placeholder); r->label = dup(value ? value : ""); r->extra = dup(button ? button : "OK"); r->input = fn;
    if (arg) memcpy(r->arg, arg, n < sizeof r->arg ? n : sizeof r->arg);
}
void menu_canvas(int h, void (*draw)(int, int, int, void *), const void *arg, size_t n) {
    Row *r = add(R_CANVAS); r->count = h; r->draw = draw;   // count = the canvas height
    if (arg) memcpy(r->arg, arg, n < sizeof r->arg ? n : sizeof r->arg);
}
static bool swallowTouch;   // the touch that opened a menu must not also tap inside it (its X sits under the menu button)
void menu_end(void) {
    if (!menuOpen && in.touch) swallowTouch = true;
    menuOpen = true; scroll = 0; focus = -1; selSlot = -1;
    for (int i = 0; i < nrows; i++) if (rows[i].kind == R_SLOT && (rows[i].selected || selSlot < 0)) { if (rows[i].selected || selSlot < 0) selSlot = i; if (rows[i].selected) break; }
}
void hud_closeMenu(void) { menuOpen = false; freeRows(); }
bool hud_isMenuOpen(void) { return menuOpen; }
void hud_setOverlay(bool (*frame)(double)) { overlay = frame; }

// layout constants for the sheet
#define SX 4
#define SY 4
#define SWD (SW - 8)
#define SHT (SH - 8)
#define BODY_X (SX + 9)
#define BODY_W (SWD - 18)
#define BODY_Y (SY + 30)
#define BODY_H (SHT - 30 - 10)
#define SLOT_W 46
#define SLOT_H 52

static bool focusable(Row *r) { return (r->kind == R_ROW && !(r->disabled && !r->fn)) || r->kind == R_SLOT || r->kind == R_INPUT || r->kind == R_SAY; }

// Run a row's click like the web: keep → rebuild with the refresh builder; else close unless `stay`.
static void click(int i) {
    Row r = rows[i];   // copy: the callback may rebuild the menu
    sfx("click");
    if (r.kind == R_SAY) { sayT = 1e9; return; }
    if (r.kind == R_SLOT) {
        if (r.pick) { char a[96]; memcpy(a, r.parg, 96); r.pick(a); }
        else selSlot = i;
        return;
    }
    if (r.kind == R_INPUT) {
        char out[256];
        if (plat_keyboard(r.sub, r.label, out, sizeof out)) { char a[160]; memcpy(a, r.arg, 160); r.input(out, a); }
        return;
    }
    if (r.disabled) return;
    bool keep = false;
    void (*rf)(void *) = refreshFn; char ra[160]; memcpy(ra, refreshArg, 160);
    if (r.fn) { char a[160]; memcpy(a, r.arg, 160); keep = r.fn(a); }
    if (keep && rf) rf(ra);
    else if (!keep && !r.stay) hud_closeMenu();   // like the web: a click that opens another menu marks itself `stay`
}

// Heights of each row (wrapping text), laid out top to bottom; slots flow in a grid.
static int rowHeight(Row *r, int w) {
    switch (r->kind) {
    case R_TEXT: return text_height("body", r->label, w) + 4;
    case R_TITLE: return text_lineh("px") + 4;
    case R_SAY: return fmax(52, text_height("body", r->sub, w - 60) + text_lineh("px") + 6) + 4;
    case R_DIAGRAM: return 52;
    case R_INPUT: return 30;
    case R_CANVAS: return r->count + 4;
    case R_ROW: {
        int tw = w - 12 - (r->icon ? 36 : 0) - (r->hasPrice ? 56 : 0);
        int h = text_height("px", r->label ? r->label : "", tw) + (r->sub ? text_height("small", r->sub, tw) : 0) + 10;
        return fmax(r->icon ? 40 : 26, h) + 3;
    }
    }
    return 0;
}

static void drawRow(Row *r, int x, int y, int w, bool foc) {
    int h = r->h - 3;
    if (r->disabled && r->fn) cv_alpha(0.55);
    if (r->label && !strcmp(r->label, "Back")) nine("assets/ui/btn-down.png", x, y, w, h, 5, 5, 5, 5); else PAPER(x, y, w, h);
    if (foc) { cv_fill(x, y, w, 1, 0xfee761ff); cv_fill(x, y + h - 1, w, 1, 0xfee761ff); cv_fill(x, y, 1, h, 0xfee761ff); cv_fill(x + w - 1, y, 1, h, 0xfee761ff); }
    int tx = x + 6;
    if (r->icon) { drawIcon(r->icon, x + 4, y + (h - 32) / 2, 2); tx += 36; }
    int tw = w - (tx - x) - 6 - (r->hasPrice ? 56 : 0);
    int th = text_height("px", r->label ? r->label : "", tw) + (r->sub ? text_height("small", r->sub, tw) : 0);
    int ty = y + (h - th) / 2;
    ty += text_draw("px", r->label ? r->label : "", tx, ty, INK, tw);
    if (r->sub) text_draw("small", r->sub, tx, ty, BROWN, tw);
    if (r->hasPrice) {
        char b[32], m[24]; fmtMoney(m, sizeof m, r->price); snprintf(b, sizeof b, ":coin: %s", m);
        int pw = text_width("px", b) + 8;
        cv_fill(x + w - pw - 4, y + h / 2 - 9, pw, 18, r->disabled ? TANHI : 0xf6ca9fff);
        text_draw("px", b, x + w - pw, y + h / 2 - 8, r->disabled ? 0xe43b44ff : INK, 0);
    }
    cv_alpha(1);
}

static void portraitDraw(const char *src, int x, int y, int scale) {
    // head and shoulders from the first idle frame of a 64×64 sheet: first opaque row, 24×22 crop at x=20
    Img *img = I(src);
    int top = 0;
    while (top < 63) { bool any = false; for (int xx = 0; xx < 64 && !any; xx++) any = img->px[(top * img->w + xx) * 4 + 3]; if (any) break; top++; }
    int sy = top - 1 < 0 ? 0 : top - 1;
    cv_image(img, 20, sy, 24, 22, x, y, 24 * scale, 22 * scale);
}

static void drawMenu(double dt) {
    int inner = BODY_W;
    // layout
    int y = 0, slotCol = 0, cols = inner / SLOT_W, focusables[160], nf = 0;
    int ry[160], rx[160];
    for (int i = 0; i < nrows; i++) {
        Row *r = &rows[i];
        if (r->kind == R_SLOT) {
            if (slotCol == 0 && i > 0 && rows[i - 1].kind != R_SLOT) y += 2;
            rx[i] = slotCol * SLOT_W; ry[i] = y;
            r->h = SLOT_H;
            if (++slotCol == cols || i + 1 == nrows || rows[i + 1].kind != R_SLOT) {
                slotCol = 0; y += SLOT_H;
                bool groupEnd = i + 1 == nrows || rows[i + 1].kind != R_SLOT;
                bool mine = false;
                for (int k = i; k >= 0 && rows[k].kind == R_SLOT; k--) mine |= k == selSlot;
                if (groupEnd && selSlot >= 0 && mine) {   // the selected slot's info box under its grid
                    Row *s = &rows[selSlot];
                    int ih = text_lineh("px") + (s->sub ? text_height("small", s->sub, inner - 8) : 0) + (s->act ? 24 : 0) + 8;
                    rows[i].h = SLOT_H;
                    ry[i] = ry[i];
                    y += ih;
                }
            }
        } else { rx[i] = 0; ry[i] = y; r->h = rowHeight(r, inner); y += r->h; }
        if (focusable(r)) focusables[nf++] = i;
    }
    int content = y;
    // input: D-pad moves the focus, A clicks; touch taps / drags to scroll
    if (in.down & (BTN_DOWN | BTN_UP | BTN_LEFT | BTN_RIGHT) && nf) {
        int k = 0;
        while (k < nf && focusables[k] != focus) k++;
        if (k == nf) k = (in.down & (BTN_UP | BTN_LEFT)) ? nf : -1;
        bool fwd = in.down & (BTN_DOWN | BTN_RIGHT);
        if (rows[focusables[k < nf && k >= 0 ? k : 0]].kind == R_SLOT && (in.down & (BTN_DOWN | BTN_UP))) {   // grid: jump a row
            int step = cols;
            k += fwd ? step : -step;
        } else k += fwd ? 1 : -1;
        k = k < 0 ? 0 : k >= nf ? nf - 1 : k;
        focus = focusables[k];
        if (ry[focus] - scroll < 0) scroll = ry[focus];
        if (ry[focus] + rows[focus].h - scroll > BODY_H) scroll = ry[focus] + rows[focus].h - BODY_H;
        sfx("click");
    }
    if (in.down & BTN_A && focus >= 0 && focus < nrows) { click(focus); return; }
    if (in.touchDown) { dragging = false; touchY0 = in.ty; scroll0 = scroll; dragY0 = in.ty; }
    if (in.touch && !in.touchDown && abs(in.ty - touchY0) > 6) { dragging = true; scroll = scroll0 - (in.ty - touchY0); }
    if (scroll > content - BODY_H) scroll = content - BODY_H;
    if (scroll < 0) scroll = 0;
    bool tap = in.touchUp && !dragging;

    // draw: dim the HUD, the sheet, body rows, then the header on top (it also hides rows scrolled under it)
    cv_fill(0, 0, SW, SH, css("rgba(25,20,30,0.6)"));
    FRAME(SX, SY, SWD, SHT);
    for (int i = 0; i < nrows; i++) {
        Row *r = &rows[i];
        int x = BODY_X + rx[i], top = BODY_Y + ry[i] - (int)scroll;
        if (top + r->h < BODY_Y - 40 || top > BODY_Y + BODY_H + 40) continue;
        bool hit = tap && in.tx >= x && in.tx < x + (r->kind == R_SLOT ? SLOT_W : inner) && in.ty >= top && in.ty < top + r->h && in.ty >= BODY_Y && in.ty < BODY_Y + BODY_H;
        switch (r->kind) {
        case R_TEXT: text_draw("body", r->label, x + 2, top + 2, INK, inner - 4); break;
        case R_TITLE: text_draw("px", r->label, x + 2, top + 2, BROWN, 0); break;
        case R_ROW: drawRow(r, x, top, inner, focus == i); break;
        case R_CANVAS: r->draw(x, top, inner, r->arg); break;
        case R_INPUT: {
            int bw = text_width("px", r->extra) + 16;
            SLOT(x, top, inner - bw - 4, 26);
            text_draw("body", r->label[0] ? r->label : r->sub, x + 6, top + 7, r->label[0] ? CREAM : 0xb9a58aff, inner - bw - 16);
            nine("assets/ui/btn.png", x + inner - bw, top, bw, 26, 5, 5, 5, 8);
            text_draw("px", r->extra, x + inner - bw + 8, top + 4, INK, 0);
            if (focus == i) cv_fill(x, top + 26, inner, 1, 0xfee761ff);
            break;
        }
        case R_SAY: {
            sayT += dt;
            int px0 = x;
            if (r->extra) { SLOT(px0, top, 56, 52); portraitDraw(r->extra, px0 + 4, top + 4, 2); }
            int tx = x + (r->extra ? 60 : 0), tw = inner - (tx - x);
            PAPER(tx, top, tw, r->h - 4);
            int ty = top + 4;
            if (r->label) ty += text_draw("px", r->label, tx + 6, ty, INK, 0);
            // typewriter: 2 chars per 22 ms
            char shown[600];
            int len = strlen(r->sub), n = (int)(sayT / 0.022) * 2;
            static int lastBlip;
            if (n < len) { if (n / 6 != lastBlip) { lastBlip = n / 6; sfx("blip"); } snprintf(shown, sizeof shown, "%.*s", n, r->sub); }
            else snprintf(shown, sizeof shown, "%s", r->sub);
            text_draw("body", shown, tx + 6, ty, INK, tw - 12);
            break;
        }
        case R_DIAGRAM: {
            int cx = x;
            for (int k = 0; k < r->dn; k++) {
                if (!strcmp(r->dicons[k], "→") || !strcmp(r->dicons[k], "↓")) { drawIcon(!strcmp(r->dicons[k], "↓") ? "arrow_d" : "arrow_r", cx, top + 12, 1); cx += 18; continue; }
                SLOT(cx, top, 36, 36); drawIcon(r->dicons[k], cx + 2, top + 2, 2);
                if (r->dlabels[k]) center("small", r->dlabels[k], cx + 18, top + 38, INK);
                cx += 40;
            }
            break;
        }
        case R_SLOT: {
            int sx = x + (SLOT_W - 38) / 2;
            SLOT(sx, top, 38, 38);
            if (r->dim) cv_tint(0x2a2440ff);
            drawIcon(r->icon, sx + 3, top + 3, 2);
            cv_tint(0);
            if (r->count > 1) { char b[12]; snprintf(b, sizeof b, "%d", r->count); text_outlined("px", b, sx + 36 - text_width("px", b), top + 22, CREAM); }
            const char *nm = r->extra ? r->extra : r->label;
            char shortn[40]; snprintf(shortn, sizeof shortn, "%s", nm ? nm : "");
            char *xm = strstr(shortn, " ×"); if (xm) *xm = 0;   // name.replace(/ ×\d+$/, '')
            while (text_width("small", shortn) > SLOT_W - 2 && strlen(shortn) > 1) shortn[strlen(shortn) - 1] = 0;
            center("small", shortn, x + SLOT_W / 2, top + 40, INK);
            if (i == selSlot || focus == i) {
                int f = (int)floor(gtime / 0.2) % 4;   // the animated selector (selector.png, 4 frames of 48)
                cv_image(I("assets/ui/selector.png"), f * 48, 0, 48, 48, sx - 5, top - 5, 48, 48);
            }
            // info box after the last slot of this group
            bool mine = false;
            for (int k = i; k >= 0 && rows[k].kind == R_SLOT; k--) mine |= k == selSlot;
            if ((i + 1 == nrows || rows[i + 1].kind != R_SLOT) && mine) {
                int iy = BODY_Y + ry[i] + SLOT_H - (int)scroll;
                if (selSlot >= 0) {
                    Row *s = &rows[selSlot];
                    cv_fill(BODY_X, iy, inner, 1, TANLO);
                    int yy = iy + 3;
                    yy += text_draw("px", s->label ? s->label : "", BODY_X + 4, yy, INK, inner - 8);
                    if (s->sub) yy += text_draw("small", s->sub, BODY_X + 4, yy, BROWN, inner - 8);
                    if (s->act) {
                        int bw = text_width("px", s->actLabel) + 16;
                        nine("assets/ui/btn-red.png", BODY_X + 4, yy + 2, bw, 22, 5, 5, 5, 8);
                        text_draw("px", s->actLabel, BODY_X + 12, yy + 4, CREAM, 0);
                        if (tap && in.tx >= BODY_X + 4 && in.tx < BODY_X + 4 + bw && in.ty >= yy + 2 && in.ty < yy + 24) {
                            sfx("click"); char a[96]; memcpy(a, s->aarg, 96); RowFn fn = s->act; fn(a); return;
                        }
                    }
                }
            }
            break;
        }
        }
        if (hit) { focus = i; click(i); return; }
    }
    // header: title + close button, over the body
    FRAME(SX, SY, SWD, 34);
    cv_fill(SX + 6, SY + 28, SWD - 12, 1, TANLO);
    text_draw("px", menuTitle, SX + 9, SY + 7, INK, SWD - 50);
    nine("assets/ui/btn.png", SX + SWD - 30, SY + 4, 24, 24, 5, 5, 5, 8);
    drawIcon("close", SX + SWD - 26, SY + 7, 1);
    {   // the sheet's bottom edge again, over rows scrolled past the body
        Img *im = I("assets/ui/btn.png");
        int top = BODY_Y + BODY_H, h = SY + SHT - top;
        cv_image(im, 0, 5, 5, 1, SX, top, 5, h - 8);
        cv_image(im, 5, 5, im->w - 10, 1, SX + 5, top, SWD - 10, h - 8);
        cv_image(im, im->w - 5, 5, 5, 1, SX + SWD - 5, top, 5, h - 8);
        cv_image(im, 0, im->h - 8, 5, 8, SX, SY + SHT - 8, 5, 8);
        cv_image(im, 5, im->h - 8, im->w - 10, 8, SX + 5, SY + SHT - 8, SWD - 10, 8);
        cv_image(im, im->w - 5, im->h - 8, 5, 8, SX + SWD - 5, SY + SHT - 8, 5, 8);
    }
    if (content > BODY_H) {   // scrollbar
        int bh = BODY_H * BODY_H / content, by = BODY_Y + (BODY_H - bh) * scroll / (content - BODY_H);
        cv_fill(SX + SWD - 7, BODY_Y, 3, BODY_H, TANLO);
        cv_fill(SX + SWD - 7, by, 3, bh, BROWN);
    }
    if (tapped(SX + SWD - 30, SY + 4, 24, 24)) { sfx("click"); hud_closeMenu(); }
}

// ---------- HUD (no menu open) ----------
void shops_questLog(void);
void tutorial_tapObjective(void);
static void drawHud(void) {
    cv_fill(0, 0, SW, SH, BG);
    // tiled grass-ish backdrop so the screen isn't flat
    for (int y = 0; y < SH; y += 8) for (int x = (y / 8) % 2 * 8; x < SW; x += 16) cv_fill(x, y, 8, 8, 0x1d4446ff);
    // hud-box: day · time, money, tickets, energy
    char b[96], m[32];
    int si = seasonIndex(state);
    snprintf(b, sizeof b, "%s %d · %s", str(jat(table("SEASONS"), si), "name"), dayOfSeason(state), game_clock()[0] ? game_clock() : "06:00");
    FRAME(4, 4, 196, 66);
    text_draw("px", b, 12, 8, INK, 0);
    text_draw("small", game_mapName(), 12 + text_width("px", b) + 6, 12, BROWN, 0);
    fmtMoney(m, sizeof m, num(state, "money"));
    snprintf(b, sizeof b, ":coin: %s", m);
    text_draw("px", b, 12, 25, INK, 0);
    if (num(state, "tickets")) { snprintf(b, sizeof b, ":ticket: %d", (int)num(state, "tickets")); text_draw("px", b, 110, 25, INK, 0); }
    int max = maxEnergy(state), e = num(state, "energy");
    drawIcon("energy", 12, 43, 1);
    cv_fill(30, 46, 86, 12, INK);
    cv_fill(31, 47, 84, 10, 0x743f39ff);
    bool low = e <= fmax(2, max * 0.15);
    int bw = fmax(0, (double)e / max * 84);
    if (!(low && (int)floor(gtime / 0.4) % 2)) {
        cv_fill(31, 47, bw, 10, low ? 0xe43b44ff : 0x63c74dff);
        cv_fill(31, 56, bw, 1, low ? 0x9e2835ff : 0x3e8948ff);
        if (!low) cv_fill(31, 47, bw, 1, 0xa6e66aff);
    }
    snprintf(b, sizeof b, "%d/%d", e, max);
    text_draw("px", b, 122, 43, INK, 0);
    // menu + decor buttons (right)
    bool pulse = hintMenu && (int)floor(gtime * 2.5) % 2;
    nine(pulse ? "assets/ui/btn-gold.png" : "assets/ui/btn.png", SW - 40, 4, 36, 36, 5, 5, 5, 8);
    drawIcon("sprite:menu", SW - 31, 13, 1);
    if (tapped(SW - 40, 4, 36, 36)) { sfx("click"); shops_mainMenu(); return; }
    bool house = !strcmp(game_worldId(), "house");
    if (house) {
        nine(decorOn ? "assets/ui/btn-gold.png" : "assets/ui/btn.png", SW - 40, 44, 36, 36, 5, 5, 5, 8);
        cv_image(I("assets/Cute_Fantasy/Buildings/House_Decor/Chairs.png"), 16, 8, 16, 24, SW - 30, 50, 16, 24);
        if (tapped(SW - 40, 44, 36, 36)) { sfx("click"); game_setDecor(!decorOn); return; }
        snprintf(b, sizeof b, ":chair: Comfort %d · +%d :energy:", comfort(state), comfortBonus(state));
        FRAME(204, 84, 112, 22);
        text_draw("small", b, 210, 90, INK, 104);
    }
    // objective chip + quest tracker
    int y = 74;
    if (objOn) {
        int w = fmin(SW - 8 - (house ? 120 : 0), text_width("px", objLabel) + 40);
        if (objTap) nine("assets/ui/btn-gold.png", 4, y, w, 28, 5, 5, 5, 8); else FRAME(4, y, w, 28);
        if (objIcon[0]) drawIcon(objIcon, 9, y + 5, 1);
        text_draw("px", objLabel, 30, y + 5, INK, 0);
        if (objTap && tapped(4, y, w, 28)) { sfx("click"); tutorial_tapObjective(); return; }
        y += 32;
    }
    for (int i = 0; i < nqt + (qtMore ? 1 : 0); i++) {
        if (i == nqt) snprintf(b, sizeof b, "+%d more", qtMore);
        else if (qtProg[i][0]) snprintf(b, sizeof b, ":quest: %s  %s", qtTitle[i], qtProg[i]);
        else snprintf(b, sizeof b, ":quest: %s", qtTitle[i]);
        int w = fmin(SW - 8, text_width("small", b) + 12);
        PAPER(SW - 4 - w, y, w, 18);
        text_draw("small", b, SW - w + 2, y + 3, INK, 0);
        if (tapped(SW - 4 - w, y, w, 18)) { sfx("click"); shops_questLog(); return; }
        y += 20;
    }
    // hotbar (bottom) or the decorating bar
    if (decorOn) {
        FRAME(4, SH - 64, SW - 8, 60);
        const char *item = game_decorItem();
        text_draw("small", item[0] ? "Tap the floor to place (A: tile in front)" : "Pick an item, or A on furniture to pick up/move", 12, SH - 58, INK, SW - 90);
        nine("assets/ui/btn.png", SW - 64, SH - 60, 52, 20, 5, 5, 5, 8);
        text_draw("px", "Done", SW - 58, SH - 58, INK, 0);
        if (tapped(SW - 64, SH - 60, 52, 20)) { sfx("click"); game_setDecor(false); return; }
        int x = 12, n = 0;
        JFOR(d, jget(state, "decor")) {
            if (numv(d) <= 0 || x > SW - 40) continue;
            bool act = !strcmp(item, d->string);
            if (act) GOLD(x, SH - 38, 30, 30); else PAPER(x, SH - 38, 30, 30);
            J *f = jget(table("FURNITURE"), d->string);
            Img *im = str(f, "pixel") ? sprite(str(f, "pixel")) : I(str(f, "src"));
            int r[4] = { 0, 0, im->w, im->h };
            if (jget(f, "rect")) for (int k = 0; k < 4; k++) r[k] = numv(jat(jget(f, "rect"), k));
            double k = 22.0 / fmax(r[2], r[3]);
            cv_image(im, r[0], r[1], r[2], r[3], x + 15 - r[2] * k / 2, SH - 23 - r[3] * k / 2, r[2] * k, r[3] * k);
            if (numv(d) > 1) { snprintf(b, sizeof b, "%d", (int)numv(d)); text_outlined("px", b, x + 22, SH - 22, CREAM); }
            if (tapped(x, SH - 38, 30, 30)) { game_pickDecor(d->string); return; }
            x += 34; n++;
        }
        JFOR(c, table("CRITTERS")) {   // critters you own that aren't on display
            char k[24], key[16]; snprintf(k, sizeof k, "critter:%d", (int)num(c, "id")); snprintf(key, sizeof key, "%d", (int)num(c, "id"));
            if (!has(jget(state, "critters"), key) || x > SW - 40) continue;
            bool placed = false;
            JFOR(p, jget(jget(state, "house"), "placed")) placed |= !strcmp(str(p, "item"), k);
            if (placed) continue;
            if (!strcmp(item, k)) GOLD(x, SH - 38, 30, 30); else PAPER(x, SH - 38, 30, 30);
            drawIcon(k, x + 3, SH - 35, 1.5 > 1 ? 1 : 1);
            if (tapped(x, SH - 38, 30, 30)) { game_pickDecor(k); return; }
            x += 34; n++;
        }
        if (!n) text_draw("small", "Nothing stored. Buy furniture at the Carpenter or pick one up.", 12, SH - 32, BROWN, SW - 24);
        return;
    }
    static const char *KEYS[4] = { "Y", "X", "L", "R" };
    int bs = 36, gap = 3, total = nhot * (bs + gap) - gap, x0 = (SW - total) / 2;
    for (int i = 0; i < nhot; i++) {
        const char *id = hot[i];
        int x = x0 + i * (bs + gap), yy = SH - bs - 6;
        bool act = !strcmp(id, str(state, "tool"));
        bool hint = hintTool[0] && !strcmp(hintTool, id) && !act && (int)floor(gtime * 2.5) % 2;
        if (act || hint) GOLD(x, yy, bs, bs); else PAPER(x, yy, bs, bs);
        char spec[32]; snprintf(spec, sizeof spec, "tool:%s", id);
        drawIcon(spec, x + 2, yy + 2, 2);
        int lvl = num(jget(state, "tools"), id);
        if (!strcmp(id, "seeds") || !strcmp(id, "build")) {
            char k[48]; snprintf(k, sizeof k, !strcmp(id, "seeds") ? "seed_%s" : "machine_%s", str(state, !strcmp(id, "seeds") ? "seed" : "build") ? str(state, !strcmp(id, "seeds") ? "seed" : "build") : "");
            snprintf(b, sizeof b, "%d", count(state, k));
            text_outlined("px", b, x + bs - text_width("px", b) - 1, yy + bs - 14, CREAM);
        } else if (lvl > 1) { snprintf(b, sizeof b, "%s", lvl == 2 ? "II" : "III"); text_outlined("px", b, x + bs - text_width("px", b) - 1, yy + bs - 14, CREAM); }
        if (i < 4) text_outlined("small", KEYS[i], x + 2, yy - 6, TANHI);
        if (tapped(x, yy, bs, bs)) { sfx("click"); selectTool(id); return; }
    }
}

// ---------- frame ----------
bool title_frame(double dt);   // title screen (title.c): true while it's up
static bool titleUp = true;
bool hud_busy(void) { return menuOpen || overlay || titleUp || fadePhase == 1; }
void hud_back(void) { if (menuOpen) { sfx("click"); hud_closeMenu(); } }
void hud_boot(void) {}

void hud_menuFrame(double dt);
void hud_frame(double dt) {
    // top-screen overlays: toast, fade
    if ((toastT -= dt) > 0 && toastMsg[0]) {
        int w = fmin(280, text_width("px", toastMsg)) + 20, h = text_height("px", toastMsg, 280) + 10;
        double a = fmin(1, toastT / 0.18), k = fmin(1, (2.2 - toastT) / 0.18);
        cv_alpha(fmin(a, k));
        int y = 240 - 12 - h + (int)((1 - k) * 24);
        SLOT(200 - w / 2, y, w, h);
        text_draw("px", toastMsg, 200 - w / 2 + 10, y + 5, CREAM, 280);
        cv_alpha(1);
    }
    int n = 0;
    for (int i = 0; i < nfloat; i++) if ((floaters[i].t -= dt) > 0) floaters[n++] = floaters[i];
    nfloat = n;
    if (fadePhase) {
        fadeT += dt;
        double a = fadePhase == 1 ? fmin(1, fadeT / 0.26) : fmax(0, 1 - fadeT / 0.26);
        cv_fill(0, 0, 400, 240, 0x120d1000 | (uint32_t)(a * 255));
        if (fadePhase == 1 && fadeT >= 0.26) { fadePhase = 2; fadeT = 0; void (*fn)(void) = fadeFn; fadeFn = NULL; if (fn) fn(); }
        else if (fadePhase == 2 && fadeT >= 0.26) fadePhase = 0;
    }
    // bottom screen
    cv_target(cv_screen(1));
    if (titleUp) { titleUp = title_frame(dt); return; }
    if (overlay) { if (!overlay(dt)) overlay = NULL; return; }
    drawHud();
    hud_menuFrame(dt);
}

// ---- for gacha/title ----
void hud_nine(const char *src, int x, int y, int w, int h, int l, int t, int r, int b) { nine(src, x, y, w, h, l, t, r, b); }
void hud_outlined(const char *f, const char *s, int x, int y, uint32_t color) { text_outlined(f, s, x, y, color); }
void hud_menuFrame(double dt) {   // also a menu opened over the title (Saves)
    if (!menuOpen) return;
    Input real = in;
    if (swallowTouch) { if (!in.touch) swallowTouch = false; in.touch = in.touchDown = in.touchUp = false; }
    drawMenu(dt);
    in = real;
}
