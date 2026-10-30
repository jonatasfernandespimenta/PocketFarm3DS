#pragma once
#include <stddef.h>
#include <stdint.h>
#include "quickjs.h"

void gfx_register(JSContext *ctx, JSValue native);

// provided by the platform (main.c on 3DS, pc/pc.c on the desktop harness)
double platform_now(void);
const char *platform_path(const char *path, char *out, size_t n);   // romfs:/ and sdmc:/ -> real path
void platform_present(const uint8_t *rgba, int w, int h, int screen, uint32_t bg);
void platform_log(const char *s);

// host.c
void host_init(void);
void host_frame(double now, uint32_t down, uint32_t up, uint32_t held, int tx, int ty);
void host_quit(void);
