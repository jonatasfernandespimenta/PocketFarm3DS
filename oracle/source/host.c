// Platform-neutral host: QuickJS runtime, romfs ES module loader, file natives, the per-frame call.
// The web game's modules run untouched; romfs:/shim/*.js fakes the browser on top of these natives.
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "quickjs.h"
#include "gfx.h"

static JSRuntime *rt;
static JSContext *ctx;
static JSValue frame_fn;

static void dump_exception(void) {
    JSValue e = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, e);
    JSValue st = JS_GetPropertyStr(ctx, e, "stack");
    const char *ss = JS_IsUndefined(st) ? NULL : JS_ToCString(ctx, st);
    char buf[2048];
    snprintf(buf, sizeof buf, "JS error: %s\n%s", s ? s : "?", ss ? ss : "");
    platform_log(buf);
    JS_FreeCString(ctx, s); if (ss) JS_FreeCString(ctx, ss);
    JS_FreeValue(ctx, st); JS_FreeValue(ctx, e);
}

static char *read_file(const char *path, size_t *len) {
    char real[512];
    FILE *f = fopen(platform_path(path, real, sizeof real), "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[n] = 0; if (len) *len = n; }
    return b;
}

// ---------- modules: plain relative paths under romfs:/ ----------
static char *normalize(JSContext *c, const char *base, const char *name, void *op) {
    char buf[512];
    if (name[0] != '.') snprintf(buf, sizeof buf, "%s", name);
    else {
        const char *slash = strrchr(base, '/');
        int dir = slash ? (int)(slash - base) : 0;
        snprintf(buf, sizeof buf, "%.*s/%s", dir, base, name);
    }
    // collapse "/./" and "/x/../"
    char out[512]; int o = 0;
    for (char *p = buf; *p;) {
        if (p[0] == '/' && p[1] == '.' && p[2] == '/') { p += 2; continue; }
        if (p[0] == '/' && p[1] == '.' && p[2] == '.' && p[3] == '/') {
            while (o > 0 && out[o - 1] != '/') o--;
            if (o > 0) o--;
            p += 3; continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
    // a 3DS override (romfs:/src3ds/x.js) replaces a DOM-bound web module (romfs:/src/x.js)
    if (!strncmp(out, "romfs:/src/", 11)) {
        char alt[512], real[512]; struct stat st;
        snprintf(alt, sizeof alt, "romfs:/src3ds/%s", out + 11);
        if (!stat(platform_path(alt, real, sizeof real), &st)) return js_strdup(c, alt);
    }
    return js_strdup(c, out);
}

static JSModuleDef *loader(JSContext *c, const char *name, void *op) {
    size_t len; char *src = read_file(name, &len);
    if (!src) { JS_ThrowReferenceError(c, "module not found: %s", name); return NULL; }
    JSValue fn = JS_Eval(c, src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(src);
    if (JS_IsException(fn)) return NULL;
    JSModuleDef *m = JS_VALUE_GET_PTR(fn);
    JS_FreeValue(c, fn);
    return m;
}

// ---------- natives ----------
static JSValue n_log(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *s = JS_ToCString(c, argv[0]); if (s) { platform_log(s); JS_FreeCString(c, s); }
    return JS_UNDEFINED;
}
static JSValue n_readFile(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *p = JS_ToCString(c, argv[0]); size_t len;
    char *b = p ? read_file(p, &len) : NULL; JS_FreeCString(c, p);
    if (!b) return JS_NULL;
    JSValue r = JS_NewArrayBufferCopy(c, (uint8_t *)b, len); free(b); return r;
}
static JSValue n_writeFile(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *p = JS_ToCString(c, argv[0]); size_t n; const char *s = JS_ToCStringLen(c, &n, argv[1]);
    bool ok = false;
    if (p && s) {
        char real[512], tmp[520];
        platform_path(p, real, sizeof real);
        snprintf(tmp, sizeof tmp, "%s.tmp", real);   // write-then-rename: a crash mid-save keeps the old file
        FILE *f = fopen(tmp, "wb");
        if (f) { ok = fwrite(s, 1, n, f) == n; fclose(f); if (ok) { remove(real); ok = !rename(tmp, real); } }
    }
    JS_FreeCString(c, p); JS_FreeCString(c, s);
    return JS_NewBool(c, ok);
}
static JSValue n_now(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) { return JS_NewFloat64(c, platform_now()); }

static const JSCFunctionListEntry natives[] = {
    JS_CFUNC_DEF("log", 1, n_log),
    JS_CFUNC_DEF("readFile", 1, n_readFile),
    JS_CFUNC_DEF("writeFile", 2, n_writeFile),
    JS_CFUNC_DEF("now", 0, n_now),
};

static void run_jobs(void) {
    JSContext *c;
    for (int r; (r = JS_ExecutePendingJob(rt, &c));) if (r < 0) dump_exception();
}

void host_init(void) {
    platform_log("host: start");
    rt = JS_NewRuntime();
    JS_SetMaxStackSize(rt, 900 * 1024);
    JS_SetModuleLoaderFunc(rt, normalize, loader, NULL);
    ctx = JS_NewContext(rt);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue native = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, native, natives, sizeof natives / sizeof natives[0]);
    gfx_register(ctx, native);
    JS_SetPropertyStr(ctx, global, "native", native);

    size_t len; char *src = read_file("romfs:/shim/boot.js", &len);
    JSValue r = src ? JS_Eval(ctx, src, len, "romfs:/shim/boot.js", JS_EVAL_TYPE_MODULE) : JS_ThrowReferenceError(ctx, "no boot.js");
    free(src);
    if (JS_IsException(r)) dump_exception();
    run_jobs();
    if (JS_IsPromise(r) && JS_PromiseState(ctx, r) == JS_PROMISE_REJECTED) {   // a module's top-level error lands here
        JS_Throw(ctx, JS_PromiseResult(ctx, r));
        dump_exception();
    }
    JS_FreeValue(ctx, r);
    frame_fn = JS_GetPropertyStr(ctx, global, "__frame");
    JS_FreeValue(ctx, global);
    platform_log("host: booted");
}

void host_frame(double now, uint32_t down, uint32_t up, uint32_t held, int tx, int ty) {
    if (!JS_IsFunction(ctx, frame_fn)) {
        JS_FreeValue(ctx, frame_fn);
        JSValue global = JS_GetGlobalObject(ctx);
        frame_fn = JS_GetPropertyStr(ctx, global, "__frame");
        JS_FreeValue(ctx, global);
    }
    if (JS_IsFunction(ctx, frame_fn)) {
        JSValue a[6] = { JS_NewFloat64(ctx, now), JS_NewUint32(ctx, down), JS_NewUint32(ctx, up),
                         JS_NewUint32(ctx, held), JS_NewInt32(ctx, tx), JS_NewInt32(ctx, ty) };
        JSValue v = JS_Call(ctx, frame_fn, JS_UNDEFINED, 6, a);
        if (JS_IsException(v)) dump_exception();
        JS_FreeValue(ctx, v);
    }
    run_jobs();
}

void host_quit(void) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue quit = JS_GetPropertyStr(ctx, global, "__quit");   // lets the shim persist the save
    if (JS_IsFunction(ctx, quit)) { JSValue v = JS_Call(ctx, quit, global, 0, NULL); if (JS_IsException(v)) dump_exception(); JS_FreeValue(ctx, v); }
    JS_FreeValue(ctx, quit);
    JS_FreeValue(ctx, global);
    run_jobs();
}
