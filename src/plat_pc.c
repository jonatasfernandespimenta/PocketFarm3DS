// Desktop harness: headless, fixed 60 Hz clock, scripted buttons, PPM shots of both screens.
//   pf <frames> [shot-every] [frame:button-mask(hex):hold-frames ...]
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include "game.h"

Input in;
static Img top = { 400, 240 }, bot = { 320, 240 };
static double clockMs;

Img *cv_screen(int which) { return which ? &bot : &top; }
void gpu_image(Img *im, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, const float m[6], float alpha, int op, uint32_t tint) {}
void gpu_fill(float x, float y, float w, float h, const float m[6], float alpha, int op, uint32_t rgba, Img *pattern) {}
void gpu_target(Img *screen) {}
void gpu_free(Img *im) {}

const char *plat_path(const char *path) {
    static char out[4][512]; static int k;
    char *o = out[k++ & 3];
    if (!strncmp(path, "romfs:/", 7)) snprintf(o, 512, "build/romfs/%s", path + 7);
    else if (!strncmp(path, "sdmc:/", 6)) snprintf(o, 512, "build/sdmc/%s", path + 6);
    else snprintf(o, 512, "%s", path);
    return o;
}
char *readFile(const char *path, size_t *len) {
    FILE *f = fopen(plat_path(path), "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    if (len) *len = n;
    return b;
}
bool writeFile(const char *path, const char *data, size_t len) {
    char tmp[520]; snprintf(tmp, sizeof tmp, "%s.tmp", plat_path(path));
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok && !rename(tmp, plat_path(path));
}
void plog(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); }
bool plat_keyboard(const char *hint, const char *initial, char *out, int n) { snprintf(out, n, "%s", initial ? initial : ""); return false; }
bool plat_debug(void) { return getenv("PF_DEBUG"); }
double profNow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
double nowMs(void) { return 1.7e12 + clockMs; }

static void shot(Img *s, const char *name, int frame) {
    char path[64]; snprintf(path, sizeof path, "build/shots/%s-%05d.ppm", name, frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int i = 0; i < s->w * s->h; i++) fwrite(s->px + i * 4, 1, 3, f);
    fclose(f);
}

static double ms(struct timespec a, struct timespec b) { return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6; }

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
// pf --bake <romfs>: paint every map's ground for every season into <romfs>/terrain/*.png
static int bake(const char *root) {
    const char *maps[] = { "farm", "town", "mine", "forest" }, *seasons[] = { "spring", "summer", "fall", "winter" };
    char p[512];
    snprintf(p, sizeof p, "%s/terrain", root); mkdir(p, 0777);
    for (int i = 0; i < 4; i++) {
        snprintf(p, sizeof p, "romfs:/assets/maps/%s.json", maps[i]);
        J *j = cJSON_Parse(readFile(p, NULL));
        J *ter = jget(j, "terrain");
        int n = jlen(ter);
        char **rows = calloc(n, sizeof *rows);
        for (int y = 0; y < n; y++) rows[y] = jat(ter, y)->valuestring;
        for (int s = 0; s < 4; s++) {
            Img *g, *m;
            paintTerrain(rows, n, num(j, "seed") ? num(j, "seed") : 7, seasons[s], &g, &m);
            snprintf(p, sizeof p, "%s/terrain/%s-%s.png", root, maps[i], seasons[s]);
            stbi_write_png(p, g->w, g->h, 4, g->px, g->w * 4);
            if (m) { snprintf(p, sizeof p, "%s/terrain/%s-%s-mask.png", root, maps[i], seasons[s]); stbi_write_png(p, m->w, m->h, 4, m->px, m->w * 4); }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 2 && !strcmp(argv[1], "--bake")) return bake(argv[2]);
    // pf --factory <state.json> <steps> [seed]: run the factory sim, print the state (tools/factest.mjs diffs it with the JS)
    if (argc > 3 && !strcmp(argv[1], "--factory")) {
        DATA = cJSON_Parse(readFile("romfs:/data.json", NULL));
        J *s = cJSON_Parse(readFile(argv[2], NULL));
        Rng r = rng(argc > 4 ? atof(argv[4]) : 42);
        int sold = 0, items = 0;
        for (int i = atoi(argv[3]); i > 0; i--) { int a, b; step(s, &r, &a, &b); sold += a; items += b; }
        setnum(s, "_sold", sold); setnum(s, "_items", items); setnum(s, "_tick", currentTick());
        puts(cJSON_PrintUnformatted(s));
        return 0;
    }
    int frames = argc > 1 ? atoi(argv[1]) : 300, every = argc > 2 ? atoi(argv[2]) : 60;
    mkdir("build/sdmc", 0777); mkdir("build/sdmc/3ds", 0777); mkdir("build/sdmc/3ds/PocketFarm", 0777); mkdir("build/shots", 0777);
    top.px = calloc(400 * 240, 4); bot.px = calloc(320 * 240, 4);
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    game_boot();
    clock_gettime(CLOCK_MONOTONIC, &b);
    fprintf(stderr, "boot: %.1f ms\n", ms(a, b));
    uint32_t prev = 0;
    double total = 0;
    for (int fr = 1; fr <= frames; fr++) {
        clockMs = fr * 1000.0 / 60;
        uint32_t held = 0;
        bool touch = false; int tx = 0, ty = 0;
        for (int i = 3; i < argc; i++) {   // "frame:mask:hold" or a touch "frame:t:x,y:hold"
            int at, hold, x, y; unsigned mask;
            if (sscanf(argv[i], "%d:t:%d,%d:%d", &at, &x, &y, &hold) == 4) { if (fr >= at && fr < at + hold) touch = true, tx = x, ty = y; }
            else if (sscanf(argv[i], "%d:%x:%d", &at, &mask, &hold) == 3 && fr >= at && fr < at + hold) held |= mask;
        }
        static bool wasTouch; static int ltx, lty;
        if (touch) ltx = tx, lty = ty;
        in = (Input){ .down = held & ~prev, .held = held, .touch = touch, .touchDown = touch && !wasTouch, .touchUp = !touch && wasTouch, .tx = ltx, .ty = lty };
        wasTouch = touch;
        prev = held;
        // PF_GACHA=1: open the capsule machine with money right away (profiling the gacha scene)
        if (fr == 60 && getenv("PF_GACHA")) { setnum(state, "money", 99999); gacha_open(NULL); }
        clock_gettime(CLOCK_MONOTONIC, &a);
        game_frame(1 / 60.0);
        clock_gettime(CLOCK_MONOTONIC, &b);
        total += ms(a, b);
        if (fr <= 2 || fr % 60 == 0 || getenv("PF_GACHA")) fprintf(stderr, "frame %d: %.2f ms\n", fr, ms(a, b));
        if (every && fr % every == 0) { shot(&top, "top", fr); shot(&bot, "bot", fr); }
    }
    fprintf(stderr, "avg frame: %.3f ms\n", total / frames);
    game_quit();
    return 0;
}
