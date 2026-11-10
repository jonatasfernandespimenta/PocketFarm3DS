// Player (player.js): free pixel movement with tile collision, layered 64x64 Cute Fantasy sprites, tool animations.
#include <stdio.h>
#include "game.h"

#define P "assets/Cute_Fantasy/Player/"
// Sheet rows: 0-2 idle, 3-5 walk (down, side, up; side faces right). Tool rows start at TOOLS[x].anim.
static const int DIR_INDEX[4] = { [DOWN] = 0, [RIGHT] = 1, [LEFT] = 1, [UP] = 2 };
#define FRAME 64
#define FEET_Y 41
#define SPEED 64   // px/s
#define BOX_W 10
#define BOX_H 6

Player player;
static Img *body, *tools, *rod;

// Skin tones 1..5: {light, shadow} targets for the base sprite's skin colors (the mid shade maps to their average).
static const uint8_t SKIN_SRC[3][3] = { { 246, 202, 159 }, { 233, 188, 149 }, { 210, 159, 112 } };
static const uint8_t SKIN_TO[5][2][3] = {
    { { 246, 202, 159 }, { 210, 159, 112 } }, { { 226, 168, 118 }, { 184, 126, 82 } }, { { 196, 134, 88 }, { 156, 100, 62 } },
    { { 156, 102, 66 }, { 120, 76, 48 } },    { { 120, 78, 52 }, { 90, 56, 38 } },
};

// Exact-RGB recolor of the three skin shades, alpha untouched. Tone 1 (and anything out of range) is the identity.
void recolorSkin(Img *im, int tone) {
    if (tone <= 1 || tone > 5) return;
    uint8_t to[3][3];
    for (int c = 0; c < 3; c++) {
        to[0][c] = SKIN_TO[tone - 1][0][c];
        to[2][c] = SKIN_TO[tone - 1][1][c];
        to[1][c] = (to[0][c] + to[2][c]) / 2;
    }
    for (int i = 0, n = im->w * im->h; i < n; i++) {
        uint8_t *p = im->px + i * 4;
        for (int k = 0; k < 3; k++)
            if (p[0] == SKIN_SRC[k][0] && p[1] == SKIN_SRC[k][1] && p[2] == SKIN_SRC[k][2]) { memcpy(p, to[k], 3); break; }
    }
    im->dirty = true;
}

// ponytail: the layers are composited into one sheet (same pixels as drawing them in order every frame: source-over
// is associative), which saves a draw per layer per frame and a texture per layer on the 3DS. Rebuilt in place.
// The layers are decoded uncached and freed right after: kept in I()'s cache they were 47 MB of dead heap.
static void drawLayer(const char *rel) {
    if (!rel) return;
    char path[256];
    snprintf(path, sizeof path, "romfs:/" P "%s", rel);
    Img *l = img_decode(path);
    if (!l) return;
    cv_draw(l, 0, 0);
    img_free(l);
}

void buildPlayerSheet(Img *dst, J *look) {
    if (!dst->px && !(dst->px = malloc((size_t)dst->w * dst->h * 4))) return;   // freed after upload on the 3DS
    memset(dst->px, 0, (size_t)dst->w * dst->h * 4);
    cv_push_target(dst);
    drawLayer("Player_Base/Player_Base_animations.png");
    drawLayer(str(look, "feet"));
    drawLayer(str(look, "legs"));
    drawLayer(str(look, "chest"));
    drawLayer("Hands/Hands_1_Bare.png");
    drawLayer(str(look, "hair"));
    drawLayer(str(look, "hat"));
    cv_pop_target();
    recolorSkin(dst, (int)num(look, "skin"));
    dst->dirty = true;
}

void rebuildPlayerSprites(void) { buildPlayerSheet(body, jget(state, "look")); }
Img *playerSheet(void) { return body; }

void loadPlayerSprites(void) {
    if (!body) { body = img_new(576, 3584); body->gpuOnly = true; }   // 8 MB of heap back once it's on the GPU
    rebuildPlayerSprites();
    tools = I(P "Tools/Iron/Iron_Tools.png");
    rod = I(P "Tools/Fishing_Rod/Wooden_Fishing_Rod.png");
}

ActOpts actDefaults(void) { return (ActOpts){ .frames = 6, .dur = 0.42, .overlay = -1, .hitAt = 0.55 }; }

// Play a one-shot animation. row = first of the 3 direction rows. onHit runs at hitAt (0..1); hold keeps the last frame.
void startAction(Player *p, ActOpts o, Thunk *onHit) {
    p->a = (Action){ .row = o.row, .frames = o.frames, .dur = o.dur, .overlay = o.overlay, .ovRow = o.ovRow, .hitAt = o.hitAt, .hold = o.hold };
    if (onHit) { p->a.onHit = *onHit; p->a.hasHit = true; }
    p->acting = true;
    p->moving = false;
}

static void updateAction(Player *p, double dt) {
    Action *a = &p->a;
    a->t += dt;
    if (!a->hit && a->t >= a->dur * a->hitAt) { a->hit = true; if (a->hasHit) { Thunk th = a->onHit; th.fn(th.arg); } }
    if (p->acting && a->t >= a->dur && !a->hold) p->acting = false;
}

static int dirOf(double x, double y) { return fabs(x) > fabs(y) ? (x > 0 ? RIGHT : LEFT) : (y > 0 ? DOWN : UP); }

// blocked(box) → true if a pixel box hits something. Returns the distance actually moved.
double updatePlayer(Player *p, double dt, double dx, double dy, bool (*blocked)(Rect)) {
    p->t += dt;
    if (p->acting) { updateAction(p, dt); return 0; }
    p->moving = dx || dy;
    if (!p->moving) return 0;
    p->dir = dirOf(dx, dy);
    double len = hypot(dx, dy), step = SPEED * dt / len, x0 = p->x, y0 = p->y;
    // Axis-separated so the player slides along walls.
    if (!blocked(feetBox(p->x + dx * step, p->y))) p->x += dx * step;
    if (!blocked(feetBox(p->x, p->y + dy * step))) p->y += dy * step;
    return hypot(p->x - x0, p->y - y0);
}

Tile playerTile(Player *p) { return (Tile){ floor(p->x / 16), floor((p->y - 3) / 16) }; }

void faceTile(Player *p, Tile t) {
    Tile me = playerTile(p);
    if (t.x != me.x || t.y != me.y) p->dir = dirOf(t.x - me.x, t.y - me.y);
}

// The player only collides with its feet, so it can stand in front of / behind tall sprites.
Rect feetBox(double x, double y) { return (Rect){ x - BOX_W / 2.0, y - BOX_H, BOX_W, BOX_H }; }

Tile facingTile(Player *p) {
    const int *v = DV[p->dir];
    return (Tile){ floor((p->x + v[0] * 11) / 16), floor((p->y - 3 + v[1] * 11) / 16) };
}

void drawPlayer(Player *p) {
    int di = DIR_INDEX[p->dir], row, frame;
    Img *ov = NULL; int ovRow = 0;
    if (p->acting) {
        Action *a = &p->a;
        row = a->row + di;
        frame = (int)fmin(a->frames - 1, floor(a->t / a->dur * a->frames));
        if (a->overlay >= 0) { ov = a->overlay ? rod : tools; ovRow = a->ovRow + di; }
    } else {
        row = di + (p->moving ? 3 : 0);
        frame = (int)floor(p->t * (p->moving ? 10 : 5)) % 6;
    }
    double dx = jround(p->x) - FRAME / 2, dy = jround(p->y) - FEET_Y;
    cv_save();
    if (p->dir == LEFT) { cv_translate(jround(p->x) * 2, 0); cv_scale(-1, 1); }
    cv_image(body, frame * FRAME, row * FRAME, FRAME, FRAME, dx, dy, FRAME, FRAME);
    if (ov) cv_image(ov, frame * FRAME, ovRow * FRAME, FRAME, FRAME, dx, dy, FRAME, FRAME);
    cv_restore();
}

// Same 64x64 layout as the player, used for town NPCs (row 0-2 idle, 3-5 walk).
void drawNpc(Img *img, double x, double y, int dir, bool moving, double t) {
    int row = DIR_INDEX[dir] + (moving ? 3 : 0), frame = (int)floor(t * (moving ? 8 : 4)) % 6;
    cv_save();
    if (dir == LEFT) { cv_translate(jround(x) * 2, 0); cv_scale(-1, 1); }
    cv_image(img, frame * FRAME, row * FRAME, FRAME, FRAME, jround(x) - FRAME / 2, jround(y) - FEET_Y, FRAME, FRAME);
    cv_restore();
}
