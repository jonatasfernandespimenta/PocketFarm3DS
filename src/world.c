// Map graph and mine generation (world.js). MAPS / ENTITIES / INTERIORS themselves are tables in data.json.
#include <stdio.h>
#include "game.h"

// Map graph for guidance arrows: which maps connect to which.
static int nlinks;
static struct { char a[16], b[16]; } links[128];
static void link2(const char *a, const char *b) {
    if (nlinks + 2 > 128) return;
    snprintf(links[nlinks].a, 16, "%s", a); snprintf(links[nlinks++].b, 16, "%s", b);
    snprintf(links[nlinks].a, 16, "%s", b); snprintf(links[nlinks++].b, 16, "%s", a);
}
static void mapLinks(void) {
    if (nlinks) return;
    link2("house", "farm");
    JFOR(list, table("ENTITIES")) JFOR(e, list) {
        const char *to = !strcmp(str(e, "type"), "warp") ? str(e, "to") : str(e, "interior");
        if (to) link2(list->string, to);
    }
}

// BFS over the map graph: the first map to walk to on the way from `from` to `to` (NULL if unreachable).
const char *nextHop(const char *from, const char *to) {
    mapLinks();
    static char names[64][16]; int prev[64], n = 0, head = 0;
    snprintf(names[n], 16, "%s", from); prev[n++] = -1;
    int found = !strcmp(from, to) ? 0 : -1;
    while (head < n && found < 0) {
        int cur = head++;
        for (int i = 0; i < nlinks; i++) {
            if (strcmp(links[i].a, names[cur])) continue;
            bool seen = false;
            for (int k = 0; k < n; k++) seen |= !strcmp(names[k], links[i].b);
            if (seen || n == 64) continue;
            snprintf(names[n], 16, "%s", links[i].b); prev[n] = cur;
            if (!strcmp(names[n], to)) { found = n; n++; break; }
            n++;
        }
    }
    if (found < 0) return NULL;
    int hop = found;
    while (prev[hop] > 0) hop = prev[hop];   // stop at the node whose prev is `from` (index 0)
    return hop == 0 ? NULL : names[hop];
}

// Ore nodes for a mine level, stable for a given day+level. One hides the ladder down.
int genMine(Map *map, int level, int day, Node *out, int max) {
    Rng r = rng(day * 131 + level * 977);
    struct { const char *id; double w; } pool[16]; int np = 0;
    double total = 0;
    JFOR(n, table("NODES")) {
        if ((num(n, "minLevel") ? num(n, "minLevel") : 1) > level || np == 16) continue;
        pool[np].id = n->string;
        pool[np].w = num(n, "weight") * (!strcmp(n->string, "stone") ? fmax(0.4, 1 - level * 0.06) : 1 + level * 0.08);
        total += pool[np++].w;
    }
    int nodes = 0, want = 14 + (int)fmin(10, level * 2);
    bool taken[100 * 64] = { 0 };
    for (int tries = 0; nodes < want && nodes < max && tries < 500; tries++) {
        int x = 1 + floor(rngNext(&r) * (map->w - 2)), y = 2 + floor(rngNext(&r) * (map->h - 3));
        if (map->solid[y * map->w + x] || taken[y * 100 + x] || (abs(x - 3) < 3 && abs(y - 4) < 3)) continue;
        double rr = rngNext(&r) * total;
        const char *type = "stone";
        for (int i = 0; i < np; i++) if ((rr -= pool[i].w) < 0) { type = pool[i].id; break; }
        taken[y * 100 + x] = true;
        out[nodes] = (Node){ .x = x, .y = y, .hp = num(jget(table("NODES"), type), "hp") };
        snprintf(out[nodes++].node, sizeof out->node, "%s", type);
    }
    if (nodes) out[(int)floor(rngNext(&r) * nodes)].ladder = true;
    return nodes;
}
