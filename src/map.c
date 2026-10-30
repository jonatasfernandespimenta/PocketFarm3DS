// Maps (map.js): editor JSON + painted terrain + sprite props, collision from layers and per-prop pixel boxes.
// ponytail: the shipped maps have no art-layer tiles, ground extras or fences (all empty in the JSON), so the
// tileset/object/fence code of map.js isn't ported; add it if a map starts using them.
#include <stdio.h>
#include "game.h"

const char *DIR_NAME[4] = { "up", "down", "left", "right" };
const int DV[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
int dirFrom(const char *s) { for (int i = 0; i < 4; i++) if (s && !strcmp(s, DIR_NAME[i])) return i; return DOWN; }

// ---------- prop types (PROPS + furniture registered by house.c) ----------
static PropDef *defs[256]; static int ndefs;

PropDef *propDefNew(const char *key) {
    PropDef *d = calloc(1, sizeof *d);
    snprintf(d->key, sizeof d->key, "%s", key);
    defs[ndefs++] = d;
    return d;
}
PropDef *propDef(const char *type) {
    for (int i = 0; i < ndefs; i++) if (!strcmp(defs[i]->key, type)) return defs[i];
    J *j = jget(table("PROPS"), type);
    if (!j) return NULL;
    PropDef *d = propDefNew(type);
    d->img = I(str(j, "src"));
    for (int i = 0; i < 4; i++) d->rect[i] = numv(jat(jget(j, "rect"), i));
    snprintf(d->kind, sizeof d->kind, "%s", str(j, "kind") ? str(j, "kind") : "");
    snprintf(d->mask, sizeof d->mask, "%s", str(j, "mask") ? str(j, "mask") : "");
    snprintf(d->stump, sizeof d->stump, "%s", str(j, "stump") ? str(j, "stump") : "");
    snprintf(d->item, sizeof d->item, "%s", str(j, "item") ? str(j, "item") : "");
    d->frames = num(j, "frames");
    return d;
}

// ---------- loading ----------
static J *layerData(J *map, bool collision) {
    JFOR(l, jget(map, "layers")) if (has(l, "isCollision") == collision) return jget(l, "data");
    return NULL;
}
static bool inList(J *list, const char *id) { JFOR(e, list) if (cJSON_IsString(e) && !strcmp(e->valuestring, id)) return true; return false; }

Map *loadMap(const char *url, J *removed, J *stumps, const char *season) {
    char path[128]; snprintf(path, sizeof path, "romfs:/%s", url);
    char *txt = readFile(path, NULL);
    J *j = cJSON_Parse(txt);
    free(txt);
    Map *m = calloc(1, sizeof *m);
    m->w = num(j, "width"); m->h = num(j, "height");
    m->coll = calloc(m->w * m->h, 1);
    J *cl = layerData(j, true);
    for (int y = 0; y < m->h; y++) for (int x = 0; x < m->w; x++) m->coll[y * m->w + x] = numv(jat(jat(cl, y), x)) != 0;
    J *ter = jget(j, "terrain");
    m->nterrain = jlen(ter);
    m->terrain = calloc(m->nterrain, sizeof(char *));
    for (int y = 0; y < m->nterrain; y++) m->terrain[y] = strdup(jat(ter, y)->valuestring);
    // the ground is painted at build time (`pf --bake`, same code) so the 3DS doesn't spend seconds on it
    char gp[128], mp[128];
    snprintf(gp, sizeof gp, "romfs:/terrain/%s-%s.png", str(j, "name"), season);
    snprintf(mp, sizeof mp, "romfs:/terrain/%s-%s-mask.png", str(j, "name"), season);
    if ((m->ground = img_decode(gp))) m->mask = img_decode(mp);
    else paintTerrain(m->terrain, m->nterrain, num(j, "seed") ? num(j, "seed") : 7, season, &m->ground, &m->mask);
    JFOR(p, jget(j, "props")) {
        if (inList(removed, str(p, "id"))) continue;
        Prop pr = { .x = num(p, "x"), .y = num(p, "y") };
        snprintf(pr.id, sizeof pr.id, "%s", str(p, "id"));
        const char *type = str(p, "type");
        setPropType(&pr, inList(stumps, pr.id) ? propDef(type)->stump : type);
        addProp(m, pr);
    }
    cJSON_Delete(j);
    rebuildSolid(m);
    return m;
}

void freeMap(Map *m) {
    if (!m) return;
    for (int i = 0; i < m->nterrain; i++) free(m->terrain[i]);
    free(m->terrain); free(m->coll); free(m->props); free(m->solid); free(m->blocked);
    img_free(m->ground); img_free(m->mask);
    free(m);
}

// ---------- props ----------
// Opaque bounding box of a sprite region (alpha ≥ 200, so soft shadows don't count).
Rect opaqueBox(Img *img, const int r[4]) {
    int sx = r[0], sy = r[1], sw = r[2], sh = r[3], x0 = sw, y0 = sh, x1 = -1, y1 = -1;
    for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {
        int ix = sx + x, iy = sy + y;
        if (ix < 0 || iy < 0 || ix >= img->w || iy >= img->h || img->px[(iy * img->w + ix) * 4 + 3] < 200) continue;
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x > x1) x1 = x;
        if (y > y1) y1 = y;
    }
    return x1 < 0 ? (Rect){ 0, 0, sw, sh } : (Rect){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
}

// Collision box for a sprite drawn at (dx, dy), from its opaque box and a mask rule. false = walk through.
bool maskCollider(const char *mask, Rect box, double dx, double dy, Rect *out) {
    double bx = dx + box.x, by = dy + box.y, bottom = by + box.h;
    if (!strcmp(mask, "tree")) { *out = (Rect){ bx + box.w / 2 - 5, bottom - 6, 10, 6 }; return true; }
    if (!strcmp(mask, "base")) { double h = fmin(box.h, fmax(6, jround(box.h * 0.55))); *out = (Rect){ bx + 1, bottom - h, box.w - 2, h }; return true; }
    if (!strcmp(mask, "full")) { *out = (Rect){ bx, by + 4, box.w, box.h - 4 }; return true; }
    if (!strcmp(mask, "building")) { double h = jround(box.h * 0.48); *out = (Rect){ bx + 2, bottom - h, box.w - 4, h }; return true; }
    return false;
}

// (Re)compute a prop's sprite placement and collider. Its opaque box's bottom-center sits on the tile's bottom-center.
void setPropType(Prop *p, const char *type) {
    PropDef *def = propDef(type);
    Rect box = opaqueBox(def->img, def->rect);
    snprintf(p->type, sizeof p->type, "%s", type);
    p->def = def;
    p->kind = def->kind;
    p->hp = num(jget(table("OBJECT_KINDS"), def->kind), "hp");
    // sprites an even number of tiles wide sit on the tile edge so they cover whole tiles (x and x+1)
    double cx = (int)jround(box.w / 16) % 2 == 0 ? (p->x + 1) * 16 : (p->x + 0.5) * 16;
    p->dx = jround(cx - box.x - box.w / 2);
    p->dy = jround((p->y + 1) * 16 - 2 - box.y - box.h);
    p->box = (Rect){ p->dx + box.x, p->dy + box.y, box.w, box.h };
    p->hasCol = maskCollider(def->mask, box, p->dx, p->dy, &p->col);
    p->flat = !p->hasCol;
    p->foot = p->box.y + p->box.h;
}

Prop *addProp(Map *m, Prop p) {
    if (m->nprops == m->capprops) { m->capprops = m->capprops ? m->capprops * 2 : 64; m->props = realloc(m->props, m->capprops * sizeof(Prop)); }
    m->props[m->nprops] = p;
    return &m->props[m->nprops++];
}

void removeProp(Map *m, Prop *p) {
    int i = p - m->props;
    memmove(p, p + 1, (m->nprops - i - 1) * sizeof(Prop));
    m->nprops--;
    rebuildSolid(m);
}

void drawProp(Prop *p, double t) {
    const int *r = p->def->rect;
    int shake = p->shake > 0 ? jround(sin(t * 60) * 1.5) : 0;
    int frame = p->def->frames ? ((int)floor(t * 4) % p->def->frames) * r[2] : 0;   // animated (displayed critters)
    cv_image(p->def->img, r[0] + frame, r[1], r[2], r[3], p->dx + shake, p->dy, r[2], r[3]);
}

// ---------- collision ----------
int colliders(Map *m, Rect *out, int max) {
    int n = 0;
    for (int i = 0; i < m->nprops && n < max; i++) if (m->props[i].hasCol) out[n++] = m->props[i].col;
    for (int i = 0; i < m->nextra && n < max; i++) out[n++] = m->extra[i];
    return n;
}

// Tile grid: collision layer, water and rock. `blocked` adds pixel colliders for pathfinding.
void rebuildSolid(Map *m) {
    free(m->solid); free(m->blocked);
    m->solid = malloc(m->w * m->h); m->blocked = malloc(m->w * m->h);
    memcpy(m->solid, m->coll, m->w * m->h);
    for (int y = 0; y < m->nterrain; y++) for (int x = 0; m->terrain[y][x]; x++) if (m->terrain[y][x] == 'w' || m->terrain[y][x] == 'r') m->solid[y * m->w + x] = 1;
    memcpy(m->blocked, m->solid, m->w * m->h);
    static Rect cs[1024];
    int n = colliders(m, cs, 1024);
    for (int i = 0; i < n; i++) {
        Rect c = cs[i];
        for (int ty = floor(c.y / 16); ty <= floor((c.y + c.h - 1) / 16); ty++) {
            for (int tx = floor(c.x / 16); tx <= floor((c.x + c.w - 1) / 16); tx++) {
                if (!inBounds(m, tx, ty)) continue;
                double ow = fmin(c.x + c.w, tx * 16 + 16) - fmax(c.x, tx * 16);
                double oh = fmin(c.y + c.h, ty * 16 + 16) - fmax(c.y, ty * 16);
                if (ow * oh >= 40) m->blocked[ty * m->w + tx] = 1;
            }
        }
    }
}

bool inBounds(Map *m, int x, int y) { return x >= 0 && y >= 0 && x < m->w && y < m->h; }
bool isSolid(Map *m, int x, int y) { return !inBounds(m, x, y) || m->solid[y * m->w + x]; }
bool isBlocked(Map *m, int x, int y) { return !inBounds(m, x, y) || m->blocked[y * m->w + x]; }

// Pixel box test against tiles + prop colliders.
bool boxBlocked(Map *m, Rect b) {
    int x0 = floor(b.x / 16), x1 = floor((b.x + b.w - 0.01) / 16), y0 = floor(b.y / 16), y1 = floor((b.y + b.h - 0.01) / 16);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (isSolid(m, x, y)) return true;
    for (int i = 0; i < m->nprops; i++) if (m->props[i].hasCol && rectsOverlap(m->props[i].col, b)) return true;
    for (int i = 0; i < m->nextra; i++) if (rectsOverlap(m->extra[i], b)) return true;
    return false;
}

bool isWater(Map *m, int x, int y) { return y >= 0 && y < m->nterrain && x >= 0 && x < (int)strlen(m->terrain[y]) && m->terrain[y][x] == 'w'; }

// Prop on a tile: its collider touches the tile, or (for trees / flat props) its sprite covers the tile.
Prop *propAt(Map *m, int x, int y) {
    Rect tile = { x * 16, y * 16, 16, 16 };
    for (int i = 0; i < m->nprops; i++) if (m->props[i].hasCol && rectsOverlap(m->props[i].col, tile)) return &m->props[i];
    for (int i = 0; i < m->nprops; i++) { Prop *p = &m->props[i]; if (!p->hasCol && floor((p->box.x + p->box.w / 2) / 16) == x && floor((p->foot - 1) / 16) == y) return p; }
    for (int i = 0; i < m->nprops; i++) {
        Prop *p = &m->props[i];
        if (!strcmp(p->kind, "tree") && rectsOverlap((Rect){ p->box.x + 4, p->box.y, p->box.w - 8, p->box.h }, (Rect){ x * 16 + 6, y * 16 + 6, 4, 4 })) return p;
    }
    return NULL;
}
