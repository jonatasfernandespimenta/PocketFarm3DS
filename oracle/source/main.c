// 3DS platform: framebuffers, buttons, touch, SD log. Everything else lives in host.c.
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "gfx.h"

u32 __stacksize__ = 1024 * 1024;   // QuickJS recursion blows the default 32 KB stack

static FILE *logfile;

void platform_log(const char *s) {
    if (!logfile) { mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/PocketFarm", 0777); logfile = fopen("sdmc:/3ds/PocketFarm/log.txt", "w"); }
    if (logfile) { fputs(s, logfile); fputc('\n', logfile); fflush(logfile); }
    svcOutputDebugString(s, strlen(s));
}

double platform_now(void) { return (double)svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000.0); }

const char *platform_path(const char *path, char *out, size_t n) { return path; }

// Canvas is centered and cropped onto the screen, composited over the page background.
// The framebuffer is column-major and rotated: fw = 240 (rows), fh = screen width.
void platform_present(const uint8_t *src, int w, int h, int screen, uint32_t bg) {
    u16 fw, fh;
    u8 *fb = gfxGetFramebuffer(screen ? GFX_BOTTOM : GFX_TOP, GFX_LEFT, &fw, &fh);
    int SW = fh, SH = fw, ox = (w - SW) / 2, oy = (h - SH) / 2;
    u8 bgr = bg >> 16, bgg = bg >> 8, bgb = bg;
    for (int x = 0; x < SW; x++) {
        u8 *col = fb + x * SH * 3;
        int sx = x + ox;
        for (int y = 0; y < SH; y++) {
            int sy = y + oy;
            u8 *o = col + (SH - 1 - y) * 3;
            if (sx < 0 || sy < 0 || sx >= w || sy >= h) { o[0] = bgb; o[1] = bgg; o[2] = bgr; continue; }
            const u8 *s = src + (sy * w + sx) * 4;
            int a = s[3], ia = 255 - a;
            o[0] = (s[2] * a + bgb * ia) / 255; o[1] = (s[1] * a + bgg * ia) / 255; o[2] = (s[0] * a + bgr * ia) / 255;
        }
    }
}

int main(void) {
    osSetSpeedupEnable(true);   // New 3DS: 804 MHz + L2 cache
    gfxInitDefault();
    romfsInit();
    host_init();
    touchPosition tp;
    while (aptMainLoop()) {
        hidScanInput();
        hidTouchRead(&tp);
        u32 held = hidKeysHeld();
        if ((held & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT)) break;   // emergency exit
        host_frame(platform_now(), hidKeysDown(), hidKeysUp(), held, tp.px, tp.py);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    host_quit();
    romfsExit();
    gfxExit();
    return 0;
}
