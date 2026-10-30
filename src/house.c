// Interiors (house.js): the player's house (level + placed furniture) and the town buildings (fixed furniture).
// Both build a Map shaped like loadMap()'s, so the rest of the engine just works.
#include <stdio.h>
#include "game.h"

#define BI "assets/Cute_Fantasy/Buildings/"
#define WALL BI "Houses_Interiors/Interior_Walls.png"
#define FLOOR BI "Houses_Interiors/Wood_Floor_Tiles.png"

PropDef *propDefNew(const char *key);

// Register a furniture item (or a displayed critter) as a prop type and return its key.
const char *furnitureType(const char *item) {
    static char key[48];
    snprintf(key, sizeof key, "f:%s", item);
    if (propDef(key)) return key;
    PropDef *d;
    J *props = table("PROPS"), *furn = table("FURNITURE");
    if (!strncmp(item, "px:", 3)) {                       // hand-made pixel sprite (sprites.js)
        Img *img = sprite(item + 3);
        d = propDefNew(key);
        d->img = img; d->rect[2] = img->w; d->rect[3] = img->h;
        strcpy(d->kind, "furniture"); strcpy(d->mask, "base");
    } else if (jget(props, item) && !jget(furn, item)) {  // any map prop (log piles…)
        PropDef *src = propDef(item);
        d = propDefNew(key);
        char k[48]; memcpy(k, d->key, sizeof k);
        *d = *src;
        memcpy(d->key, k, sizeof k);
        strcpy(d->kind, "furniture");
    } else if (!strncmp(item, "critter:", 8)) {
        J *c = NULL;
        JFOR(x, table("CRITTERS")) if (num(x, "id") == atoi(item + 8)) c = x;
        int size = num(c, "size") ? num(c, "size") : 32;
        d = propDefNew(key);
        d->img = I(str(c, "sprite")); d->rect[2] = d->rect[3] = size;
        strcpy(d->kind, "critter"); strcpy(d->mask, "none");
        d->frames = num(c, "frames") ? num(c, "frames") : 2;
    } else {
        J *f = jget(furn, item);
        Img *img = str(f, "pixel") ? sprite(str(f, "pixel")) : I(str(f, "src"));
        d = propDefNew(key);
        d->img = img;
        J *r = jget(f, "rect");
        if (r) for (int i = 0; i < 4; i++) d->rect[i] = numv(jat(r, i));
        else { d->rect[2] = img->w; d->rect[3] = img->h; }
        strcpy(d->kind, has(f, "bed") ? "bed" : "furniture");
        strcpy(d->mask, has(f, "flat") ? "none" : "base");
        d->flat = has(f, "flat");
        d->frames = num(f, "frames");
    }
    return key;
}

typedef struct { int W, H; Rect floor; Tile door; } RoomSize;
static RoomSize roomSize(int w, int d) { return (RoomSize){ w + 2, d + 4, { 1, 3, w, d }, { (w + 2) / 2, d + 3 } }; }

// Wall styles = column in Interior_Walls.png (16x48 strips); floors = tile in Wood_Floor_Tiles.png.
static int wallX(const char *w) { return !strcmp(w, "stone") ? 64 : !strcmp(w, "brick") ? 80 : 48; }
static void floorXY(const char *f, int *x, int *y) {
    static const struct { const char *id; int x, y; } F[] = { { "wood", 32, 32 }, { "dark", 0, 0 }, { "parquet", 96, 32 }, { "herring", 64, 0 }, { "stone", 0, 64 }, { "red", 32, 64 } };
    for (int i = 0; i < 6; i++) if (!strcmp(F[i].id, f)) { *x = F[i].x; *y = F[i].y; return; }
    *x = 32; *y = 32;
}

static void makeFurniture(Map *map, long uid, bool movable, const char *item, int x, int y) {
    Prop p = { .x = x, .y = y, .uid = uid, .movable = movable };
    snprintf(p.id, sizeof p.id, "u%ld", uid);
    snprintf(p.item, sizeof p.item, "%s", item);
    setPropType(&p, furnitureType(item));
    addProp(map, p);
}

static void beam(int x, int y, int w, int h) { cv_fill(x, y, w, h, 0x3f2832ff); cv_fill(x + 2, y + 2, w - 4, h - 4, 0x6d483bff); cv_fill(x + 2, y + 2, w - 4, 2, 0x8f5d47ff); }
static void win(int cx) {   // little window on plaster walls (wood frame, sky glass, cross bar, sill)
    int x = cx - 7, y = 8;
    cv_fill(x, y, 14, 15, 0x3f2832ff);
    cv_fill(x + 1, y + 1, 12, 13, 0x8f5d47ff);
    cv_fill(x + 2, y + 2, 10, 10, 0x9fe2ffff);
    cv_fill(x + 2, y + 2, 4, 1, 0xd4f4ffff); cv_fill(x + 2, y + 3, 1, 3, 0xd4f4ffff); cv_fill(x + 8, y + 7, 2, 1, 0xd4f4ffff);
    cv_fill(x + 6, y + 2, 2, 10, 0x8f5d47ff); cv_fill(x + 2, y + 6, 10, 2, 0x8f5d47ff);
    cv_fill(x - 1, y + 14, 16, 2, 0x3f2832ff);
    cv_fill(x, y + 14, 14, 1, 0xb86f50ff);
}

// room: { w, d, wall, floor, placed: [{ uid, item, x, y }] }
Map *buildRoom(int w, int d, const char *wallStyle, const char *floorStyle, J *placed, bool isHouse) {
    RoomSize rs = roomSize(w, d);
    int W = rs.W, H = rs.H;
    Rect floor = rs.floor; Tile door = rs.door;
    Img *wall = I(WALL), *tiles = I(FLOOR);
    const char *ws = wallStyle ? wallStyle : "wood";
    int wx = wallX(ws), fx, fy;
    floorXY(floorStyle ? floorStyle : "wood", &fx, &fy);
    Img *c = img_new(W * 16, H * 16);
    cv_push_target(c);
    cv_fill(0, 0, c->w, c->h, 0x1b1218ff);
    // back wall (3 tiles tall) and floor
    for (int x = floor.x; x < floor.x + floor.w; x++) {
        if (!strcmp(ws, "wood")) {
            // plaster on top, plank wainscot below, a wooden rail between them
            cv_image(wall, 16, 48, 16, 48, x * 16, 0, 16, 48);
            for (int i = 0; i < 16; i += 4) cv_fill(x * 16 + i, 3, 2, 27, css("rgba(184,111,80,0.16)"));   // wallpaper stripes + dots
            for (int i = 0; i < 16; i += 8) for (int j = 8; j < 30; j += 8) cv_fill(x * 16 + i + 3 + (j % 16 ? 0 : 4), j, 1, 1, css("rgba(255,243,220,0.35)"));
            cv_fill(x * 16, 0, 16, 3, css("rgba(63,40,50,0.35)"));
            cv_image(wall, wx, 64, 16, 16, x * 16, 32, 16, 16);
            cv_fill(x * 16, 30, 16, 1, 0x3f2832ff);
            cv_fill(x * 16, 31, 16, 2, 0xb86f50ff);
            cv_fill(x * 16, 31, 16, 1, 0xe4a672ff);
        } else cv_image(wall, wx, 48, 16, 48, x * 16, 0, 16, 48);
        for (int y = floor.y; y < floor.y + floor.h; y++) cv_image(tiles, fx, fy, 16, 16, x * 16, y * 16, 16, 16);
    }
    if (!strcmp(ws, "wood")) {
        int n = floor.w >= 7 ? 2 : 1;
        for (int i = 0; i < n; i++) win(jround((floor.x + floor.w * (i + 1) / (n + 1)) * 16));
    }
    // shadow under the wall, beams around the room, door gap with a mat
    cv_fill(16, 48, floor.w * 16, 3, css("rgba(42,28,34,0.45)"));
    cv_fill(16, 51, floor.w * 16, 2, css("rgba(42,28,34,0.2)"));
    beam(0, 0, 16, H * 16);
    beam((W - 1) * 16, 0, 16, H * 16);
    beam(0, (H - 1) * 16, door.x * 16, 16);
    beam((door.x + 1) * 16, (H - 1) * 16, (W - door.x - 1) * 16, 16);
    cv_image(tiles, fx, fy, 16, 16, door.x * 16, door.y * 16, 16, 16);
    cv_fill(door.x * 16 + 2, door.y * 16 + 4, 12, 9, 0x9c6754ff);
    cv_fill(door.x * 16 + 3, door.y * 16 + 5, 10, 7, 0xc99a5bff);
    for (int i = 0; i < 3; i++) cv_fill(door.x * 16 + 4 + i * 3, door.y * 16 + 6, 1, 5, 0x9c6754ff);
    cv_pop_target();

    Map *map = calloc(1, sizeof *map);
    map->w = W; map->h = H; map->ground = c;
    map->coll = malloc(W * H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        map->coll[y * W + x] = !((x >= floor.x && x < floor.x + floor.w && y >= floor.y && y < floor.y + floor.h) || (x == door.x && y == door.y));
    map->floor = floor; map->door = door; map->isHouse = isHouse; map->indoor = true;
    int i = 0;
    JFOR(p, placed) {   // town furniture isn't movable
        makeFurniture(map, cJSON_IsNumber(jget(p, "uid")) ? (long)num(p, "uid") : -1 - i, isHouse, str(p, "item"), num(p, "x"), num(p, "y"));
        i++;
    }
    rebuildSolid(map);
    return map;
}

Map *buildHouseMap(J *s) {
    J *lvl = jat(table("HOUSE_LEVELS"), num(jget(s, "house"), "level"));
    return buildRoom(num(lvl, "w"), num(lvl, "d"), NULL, NULL, jget(jget(s, "house"), "placed"), true);
}

// Would `item` fit standing on tile (x, y)? Fills *out (not yet added) and returns true if so.
bool tryPlace(Map *map, const char *item, int x, int y, long uid, Rect *blockers, int nb, Prop *out) {
    Prop p = { .x = x, .y = y, .uid = uid, .movable = true };
    snprintf(p.id, sizeof p.id, "u%ld", uid);
    snprintf(p.item, sizeof p.item, "%s", item);
    setPropType(&p, furnitureType(item));
    Rect room = { map->floor.x * 16, map->floor.y * 16 - 16, map->floor.w * 16, map->floor.h * 16 + 16 };
    Rect box = p.hasCol ? p.col : (Rect){ p.box.x, p.foot - 8, p.box.w, 8 };
    bool inside = box.x >= room.x && box.y >= room.y && box.x + box.w <= room.x + room.w && box.y + box.h <= map->floor.y * 16 + map->floor.h * 16;
    if (!inside) return false;
    if (p.hasCol) {
        Rect doorBox = { map->door.x * 16, (map->door.y - 1) * 16, 16, 32 };
        if (rectsOverlap(p.col, doorBox)) return false;
        Rect cs[512]; int n = colliders(map, cs, 512);
        for (int i = 0; i < n; i++) if (rectsOverlap(cs[i], p.col)) return false;
        for (int i = 0; i < nb; i++) if (rectsOverlap(blockers[i], p.col)) return false;
    }
    *out = p;
    return true;
}
