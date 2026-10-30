// Small hand-made pixel sprites (sprites.js). The pixel maps and palette come from data.json.
#include <stdio.h>
#include <string.h>
#include "game.h"

Img *sprite(const char *name) {
    static struct { const char *name; Img *im; } cache[64];
    static int n;
    for (int i = 0; i < n; i++) if (!strcmp(cache[i].name, name)) return cache[i].im;
    J *rows = jget(table("SPRITES"), name), *pal = table("SPRITE_PAL");
    if (!rows) return NULL;
    int w = 0, h = jlen(rows);
    JFOR(r, rows) if ((int)strlen(r->valuestring) > w) w = strlen(r->valuestring);
    Img *im = img_new(w, h);   // pixels written directly: a fill onto a transparent canvas is just the color
    int y = 0;
    JFOR(r, rows) {
        for (int x = 0; r->valuestring[x]; x++) {
            char ch[2] = { r->valuestring[x], 0 };
            if (ch[0] == '.') continue;
            uint32_t c = css(str(pal, ch));
            uint8_t *p = im->px + (y * w + x) * 4;
            p[0] = c >> 24; p[1] = c >> 16; p[2] = c >> 8; p[3] = c;
        }
        y++;
    }
    cache[n].name = strdup(name); cache[n++].im = im;
    return im;
}

// assets.js: images load on first use. Tiny Factory sprites use pure black outlines and punchy colors;
// Cute Fantasy uses a soft dark purple outline and warmer, calmer tones: repaint the factory art once.
Img *I(const char *src) {
    char path[256];
    snprintf(path, sizeof path, "romfs:/%s", src);
    bool fresh = false;
    Img *im = img_load_ex(path, &fresh);
    if (im && fresh && strstr(src, "Tiny_Factory")) {
        uint8_t *p = im->px;
        for (int i = 0; i < im->w * im->h * 4; i += 4) {
            if (!p[i + 3]) continue;
            double lum = 0.3 * p[i] + 0.59 * p[i + 1] + 0.11 * p[i + 2];
            if (lum < 42) { p[i] = 42; p[i + 1] = 36; p[i + 2] = 64; continue; }   // #2a2440, Cute Fantasy outline
            p[i] = lrint(fmin(255, p[i] * 0.9 + 22));   // Uint8ClampedArray rounds half to even, like lrint
            p[i + 1] = lrint(fmin(255, p[i + 1] * 0.88 + 14));
            p[i + 2] = lrint(fmin(255, p[i + 2] * 0.84 + 10));
        }
    }
    return im;
}
