// 3DS platform: citro2d frame, buttons, touch, SD files, log. The game draws through cv (GPU on screens).
#include <3ds.h>
#include <citro2d.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "game.h"
#include "audio.h"   // audio (sfx.c / music.c / audio.c)

Input in;
u32 __stacksize__ = 512 * 1024;   // the game keeps some big scratch arrays on the stack (default is 32 KB)
static Img top = { .w = 400, .h = 240, .screen = true }, bot = { .w = 320, .h = 240, .screen = true };
static FILE *logfile;

Img *cv_screen(int which) { return which ? &bot : &top; }
const char *plat_path(const char *path) { return path; }

char *readFile(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
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
    char tmp[520]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    if (ok) { remove(path); ok = !rename(tmp, path); }   // write-then-rename: a crash mid-save keeps the old file
    return ok;
}
void plog(const char *fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (!logfile) logfile = fopen("sdmc:/3ds/PocketFarm/log.txt", "w");
    if (logfile) { fputs(b, logfile); fputc('\n', logfile); fflush(logfile); }
    svcOutputDebugString(b, strlen(b));
}
bool plat_keyboard(const char *hint, const char *initial, char *out, int n) {
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, n - 1);
    swkbdSetHintText(&kb, hint ? hint : "");
    if (initial) swkbdSetInitialText(&kb, initial);
    return swkbdInputText(&kb, out, n) == SWKBD_BUTTON_CONFIRM;
}
double profNow(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000.0); }
double nowMs(void) { return osGetTime() - 2208988800000.0; }   // 1900 epoch → 1970

static uint32_t mapKeys(u32 k) {
    uint32_t b = 0;
    if (k & KEY_A) b |= BTN_A;
    if (k & KEY_B) b |= BTN_B;
    if (k & KEY_X) b |= BTN_X;
    if (k & KEY_Y) b |= BTN_Y;
    if (k & KEY_L) b |= BTN_L;
    if (k & KEY_R) b |= BTN_R;
    if (k & KEY_START) b |= BTN_START;
    if (k & KEY_SELECT) b |= BTN_SELECT;
    if (k & KEY_UP) b |= BTN_UP;      // KEY_UP etc. = D-pad or circle pad
    if (k & KEY_DOWN) b |= BTN_DOWN;
    if (k & KEY_LEFT) b |= BTN_LEFT;
    if (k & KEY_RIGHT) b |= BTN_RIGHT;
    return b;
}

static bool debug;
void gpu_beginFrame(void);   // cv_3ds.c
extern unsigned long texBytes;
static void logMem(const char *when) {
    struct mallinfo mi = mallinfo();
    plog("mem %s: linear free %lu KB, textures %lu KB, app free %lu KB, heap used %lu KB", when, (unsigned long)linearSpaceFree() / 1024,
         texBytes / 1024, (unsigned long)osGetMemRegionFree(MEMREGION_APPLICATION) / 1024, (unsigned long)mi.uordblks / 1024);
}
bool plat_debug(void) { return debug; }

int main(void) {
    osSetSpeedupEnable(true);   // New 3DS: 804 MHz + L2 cache
    gfxInitDefault();
    romfsInit();
    mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/PocketFarm", 0777);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
    C2D_Init(8192);
    C2D_Prepare();
    u64 t0 = svcGetSystemTick();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);   // boot draws offscreen canvases; keep a frame open for safety
    C3D_FrameEnd(0);
    audio_init();   // audio: ndsp + mixer thread (runs silent without dspfirm.cdc)
    game_boot();
    plog("boot: %.0f ms", (svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0));
    logMem("boot");
    debug = !access("sdmc:/3ds/PocketFarm/debug", F_OK);
    bool autoGacha = !access("sdmc:/3ds/PocketFarm/gacha", F_OK);   // debug: pass the title, open the gacha, keep tapping
    u64 last = svcGetSystemTick();
    int frame = 0;
    double acc = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 held = hidKeysHeld(), down = hidKeysDown();
        if ((held & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT)) break;   // quit
        touchPosition tp; hidTouchRead(&tp);
        bool touch = held & KEY_TOUCH;
        // on the release frame the hardware reports (0,0): keep the last real position, menus act on release
        static int ltx, lty;
        if (touch) ltx = tp.px, lty = tp.py;
        in = (Input){ .down = mapKeys(down), .held = mapKeys(held), .touch = touch, .touchDown = down & KEY_TOUCH, .touchUp = hidKeysUp() & KEY_TOUCH, .tx = ltx, .ty = lty };
        if (autoGacha) {
            static int f; f++;
            if (f == 60) in.down |= BTN_A;
            if (f == 120) { setnum(state, "money", 99999); gacha_open(NULL); }
            if (f > 120 && f % 150 == 0) { in.touch = in.touchDown = true; in.tx = 60; in.ty = 100; }
        }
        u64 now = svcGetSystemTick();
        double dt = fmin(0.05, (now - last) / (double)SYSCLOCK_ARM11);
        last = now;
        u64 a = svcGetSystemTick();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        gpu_beginFrame();
        game_frame(dt);
        u64 b = svcGetSystemTick();
        C3D_FrameEnd(0);
        acc += (b - a) / (SYSCLOCK_ARM11 / 1000.0);
        extern double prof[4];
        static int frames;   // memory in the log even without `debug`: hardware gives less than Azahar
        if (++frames == 300 || frames % 3600 == 0) logMem("run");
        if (debug && ++frame % 120 == 0) {
            plog("cpu %.2f ms (update %.2f, draw %.2f, lights %.2f) gpu %.2f ms", acc / 120, prof[0] / 120, prof[1] / 120, prof[2] / 120, C3D_GetDrawingTime());
            acc = 0; memset(prof, 0, sizeof(double) * 4);
        }
    }
    game_quit();
    audio_exit();   // audio
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}
