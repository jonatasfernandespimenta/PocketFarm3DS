// Desktop harness: the same host + shim, headless. Runs N frames at a fixed 60 Hz clock with scripted
// buttons and writes the screens as PPM, so the port can be iterated without the emulator.
//   pf-pc <frames> [shot-every] [frame:button-mask:hold-frames ...]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "gfx.h"

static double clock_ms;
static int frame_no, shot_every;

void platform_log(const char *s) { fprintf(stderr, "%s\n", s); }
double platform_now(void) { return clock_ms; }

const char *platform_path(const char *path, char *out, size_t n) {
    if (!strncmp(path, "romfs:/", 7)) snprintf(out, n, "build/oracle-romfs/%s", path + 7);
    else if (!strncmp(path, "sdmc:/", 6)) snprintf(out, n, "build/oracle-sdmc/%s", path + 6);
    else snprintf(out, n, "%s", path);
    return out;
}

void platform_present(const uint8_t *src, int w, int h, int screen, uint32_t bg) {
    if (!shot_every || frame_no % shot_every) return;
    char path[64]; snprintf(path, sizeof path, "build/oracle-shots/%s-%05d.ppm", screen ? "bot" : "top", frame_no);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int SW = screen ? 320 : 400, SH = 240, ox = (w - SW) / 2, oy = (h - SH) / 2;
    fprintf(f, "P6\n%d %d\n255\n", SW, SH);
    for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) {
        int sx = x + ox, sy = y + oy;
        uint8_t o[3] = { bg >> 16, bg >> 8, bg };
        if (sx >= 0 && sy >= 0 && sx < w && sy < h) {
            const uint8_t *s = src + (sy * w + sx) * 4;
            for (int i = 0; i < 3; i++) o[i] = (s[i] * s[3] + o[i] * (255 - s[3])) / 255;
        }
        fwrite(o, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 300;
    shot_every = argc > 2 ? atoi(argv[2]) : 60;
    mkdir("build/oracle-sdmc", 0777); mkdir("build/oracle-sdmc/3ds", 0777); mkdir("build/oracle-sdmc/3ds/PocketFarm", 0777); mkdir("build/oracle-shots", 0777);
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    host_init();
    clock_gettime(CLOCK_MONOTONIC, &b);
    fprintf(stderr, "boot: %.1f ms\n", (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6);
    uint32_t prev = 0;
    for (frame_no = 1; frame_no <= frames; frame_no++) {
        clock_ms = frame_no * 1000.0 / 60;
        uint32_t held = 0;
        for (int i = 3; i < argc; i++) {   // "frame:mask:hold"
            int at, hold; unsigned mask;
            if (sscanf(argv[i], "%d:%x:%d", &at, &mask, &hold) == 3 && frame_no >= at && frame_no < at + hold) held |= mask;
        }
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        host_frame(clock_ms, held & ~prev, prev & ~held, held, 0, 0);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
        if (frame_no <= 3 || frame_no % 60 == 0) fprintf(stderr, "frame %d: %.2f ms\n", frame_no, ms);
        prev = held;
    }
    host_quit();
    return 0;
}
