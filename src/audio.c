// Software synth + mixer: the subset of WebAudio that sfx.js / music.js use (oscillators with exponential
// pitch glides, exponential gain envelopes, biquad-filtered noise), rendered to mono s16.
// 3DS: ndsp channel 0 fed by 4 small wave buffers, refilled from a mixer thread woken by the ndsp frame callback.
// PC: silent unless PF_WAV=1, then audio_pump() (called once per frame) writes build/audio.wav.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "game.h"

#define NV 128     // voice pool (tada legendary ≈ 36 voices, crank ≈ 33, music ≈ 6)
#define TBL 2048   // sine table
#define QN 128     // command queue (power of 2)

typedef struct {
    int64_t start;               // first sample on the render clock
    int n, pos, na;              // length, samples played, attack length
    uint8_t type, ftype, on;
    float ph, inc, rf;           // phase (cycles), phase step, per-sample step multiplier (pitch glide)
    float g, ra, rd, dg;         // gain, attack multiplier, release multiplier, linear step (hat)
    float fc, rfc, qd, b0, b1, b2, a1, a2, z1, z2;   // noise biquad (cutoff glide per 32 samples)
} Voice;

static Voice v[NV];
static float sine[TBL];
static int64_t clk;              // render clock (samples)
static uint32_t arng = 0x9e3779b9;
static volatile bool ok;         // a backend is running
typedef struct { uint8_t op, a, b; float x; } Cmd;
static Cmd q[QN];
static volatile unsigned qh, qt;

double a_now(void) { return clk / (double)SR; }
static inline uint32_t xs(void) { arng ^= arng << 13; arng ^= arng >> 17; arng ^= arng << 5; return arng; }
float a_rand(void) { return xs() * (1.0f / 4294967296.0f); }

static Voice *alloc(double at, float dur) {
    for (int i = 0; i < NV; i++) if (!v[i].on) {
        Voice *o = &v[i];
        memset(o, 0, sizeof *o);
        o->on = 1;
        o->start = llround(at * SR);
        o->n = (int)(dur * SR);
        if (o->n < 1) o->n = 1;
        return o;
    }
    return NULL;   // ponytail: pool full → the sound is dropped
}

void a_tone(int type, float f0, float f1, double at, float dur, float vol, float attack) {
    Voice *o = alloc(at, dur);
    if (!o) return;
    if (f1 < 1) f1 = 1;
    o->type = type;
    o->inc = f0 / SR;
    o->rf = powf(f1 / f0, 1.0f / o->n);
    o->na = (int)(attack * SR);
    if (o->na >= o->n) o->na = o->n - 1;
    o->g = 0.0001f;
    o->ra = o->na > 0 ? powf(vol / 0.0001f, 1.0f / o->na) : 1;
    if (o->na <= 0) o->g = vol;
    o->rd = powf(0.0001f / vol, 1.0f / (o->n - o->na));
}

static void coef(Voice *o) {
    float f = o->fc < SR * 0.49f ? o->fc : SR * 0.49f;
    float w = 6.2831853f * f / SR, c = cosf(w), al = sinf(w) / o->qd, a0 = 1 + al;
    if (o->ftype == F_BP) { o->b0 = al / a0; o->b1 = 0; o->b2 = -al / a0; }
    else {
        float k = o->ftype == F_LP ? (1 - c) / 2 : (1 + c) / 2;
        o->b0 = o->b2 = k / a0; o->b1 = (o->ftype == F_LP ? 2 : -2) * k / a0;
    }
    o->a1 = -2 * c / a0; o->a2 = (1 - al) / a0;
}

static Voice *noiseVoice(double at, float dur, int ftype, float freq, float q, float freqEnd) {
    Voice *o = alloc(at, dur);
    if (!o) return NULL;
    if (freqEnd < 20) freqEnd = 20;
    o->type = W_NOISE; o->ftype = ftype; o->fc = freq;
    o->rfc = powf(powf(freqEnd / freq, 1.0f / o->n), 32);
    // WebAudio: lowpass/highpass Q is in dB, bandpass Q is linear
    o->qd = 2 * (ftype == F_BP ? q : powf(10, q / 20));
    coef(o);
    return o;
}

void a_noise(double at, float dur, float vol, int ftype, float freq, float q, float freqEnd) {
    Voice *o = noiseVoice(at, dur, ftype, freq, q, freqEnd);
    if (!o) return;
    o->g = vol; o->ra = o->rd = powf(0.0001f / vol, 1.0f / o->n);
}

void a_hat(double at, float vol) {
    Voice *o = noiseVoice(at, 0.03f, F_HP, 6000, 1, 6000);
    if (!o) return;
    o->g = vol; o->ra = o->rd = 1; o->dg = vol / o->n;
}

static inline float blep(float t, float dt) {   // polyBLEP: band-limits the square/saw edges
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}

// one envelope segment of `cnt` samples with gain multiplier m
static void seg(Voice *o, float *out, int cnt, float m) {
    float g = o->g, ph = o->ph, inc = o->inc, rf = o->rf, dg = o->dg;
    switch (o->type) {
    case W_SINE:   for (int k = 0; k < cnt; k++) { out[k] += sine[(int)(ph * TBL)] * g; g = g * m - dg; ph += inc; if (ph >= 1) ph -= 1; inc *= rf; } break;
    case W_TRI:    for (int k = 0; k < cnt; k++) { out[k] += (ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph) * g; g *= m; ph += inc; if (ph >= 1) ph -= 1; inc *= rf; } break;
    case W_SAW:    for (int k = 0; k < cnt; k++) { out[k] += (2 * ph - 1 - blep(ph, inc)) * g; g *= m; ph += inc; if (ph >= 1) ph -= 1; inc *= rf; } break;
    case W_SQUARE: for (int k = 0; k < cnt; k++) {
            float p2 = ph + 0.5f; if (p2 >= 1) p2 -= 1;
            out[k] += ((ph < 0.5f ? 1 : -1) + blep(ph, inc) - blep(p2, inc)) * g; g *= m; ph += inc; if (ph >= 1) ph -= 1; inc *= rf;
        } break;
    default: {     // W_NOISE / W_HAT
        float b0 = o->b0, b1 = o->b1, b2 = o->b2, a1 = o->a1, a2 = o->a2, z1 = o->z1, z2 = o->z2;
        for (int k = 0; k < cnt; k++) {
            if (o->rfc != 1 && ((o->pos + k) & 31) == 0) { o->fc *= o->rfc; coef(o); b0 = o->b0; b1 = o->b1; b2 = o->b2; a1 = o->a1; a2 = o->a2; }
            float x = (int32_t)xs() * (1.0f / 2147483648.0f), y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y;
            out[k] += y * g; g = g * m - dg;
        }
        o->z1 = z1; o->z2 = z2;
    }
    }
    o->g = g; o->ph = ph; o->inc = inc; o->pos += cnt;
}

static void render(int16_t *dst, int N) {
    // commands from the main thread
    while (qt != qh) {
        __sync_synchronize();
        Cmd c = q[qt & (QN - 1)];
        __sync_synchronize();
        qt++;
        if (c.op == OP_SFX) sfx_play(c.a, c.b, c.x, a_now());
        else music_op(c.op, c.a);
    }
    music_tick((clk + N) / (double)SR);
    float mix[512];
    memset(mix, 0, N * sizeof(float));
    for (int i = 0; i < NV; i++) {
        Voice *o = &v[i];
        if (!o->on) continue;
        int64_t s0 = o->start - clk;
        if (s0 >= N) continue;
        int k = s0 > 0 ? (int)s0 : 0, cnt = N - k;
        if (cnt > o->n - o->pos) cnt = o->n - o->pos;
        if (o->pos < o->na) {
            int a = o->na - o->pos < cnt ? o->na - o->pos : cnt;
            seg(o, mix + k, a, o->ra); k += a; cnt -= a;
        }
        if (cnt > 0) seg(o, mix + k, cnt, o->rd);
        if (o->pos >= o->n) o->on = 0;
    }
    for (int k = 0; k < N; k++) {   // hard clip like the WebAudio destination
        float s = mix[k] * 32767;
        dst[k] = s > 32767 ? 32767 : s < -32768 ? -32768 : (int16_t)s;
    }
    clk += N;
}

static void tables(void) { for (int i = 0; i < TBL; i++) sine[i] = sinf(6.2831853f * i / TBL); }

#ifdef __3DS__
#include <3ds.h>
#include <unistd.h>
#define NBUF 4
#define LEN 320   // 2 DSP frames ≈ 9.8 ms per buffer → ≤ ~40 ms latency
static ndspWaveBuf wb[NBUF];
static int16_t *pcm;
static LightEvent ev;
static Thread th;
static volatile bool run;
static int core;

static void onFrame(void *d) { LightEvent_Signal(&ev); }
static void mixer(void *arg) {
    int nxt = 0, n = 0;
    u64 busy = 0;
    bool debug = !access("sdmc:/3ds/PocketFarm/debug", F_OK);
    while (run) {
        while (wb[nxt].status == NDSP_WBUF_FREE || wb[nxt].status == NDSP_WBUF_DONE) {
            u64 t = svcGetSystemTick();
            render(wb[nxt].data_pcm16, LEN);
            DSP_FlushDataCache(wb[nxt].data_pcm16, LEN * 2);
            ndspChnWaveBufAdd(0, &wb[nxt]);
            busy += svcGetSystemTick() - t;
            nxt = (nxt + 1) % NBUF;
            if (debug && ++n % (SR * 8 / LEN) == 0) {   // every ~8 s of audio
                int nv = 0; for (int i = 0; i < NV; i++) nv += v[i].on;
                plog("audio: core %d, mixer %.2f%% of a core (%.3f ms per 16.7 ms frame), %d voices", core,
                     100.0 * busy / (SYSCLOCK_ARM11 * 8.0), busy / (SYSCLOCK_ARM11 / 1000.0) / (8 * 60.0), nv);
                busy = 0;
            }
        }
        LightEvent_Wait(&ev);
    }
}

void audio_init(void) {
    tables();
    if (R_FAILED(ndspInit())) { plog("audio: ndspInit failed (no sdmc:/3ds/dspfirm.cdc?), running silent"); return; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, SR);
    ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
    float m[12] = { 1, 1 };
    ndspChnSetMix(0, m);
    pcm = linearAlloc(NBUF * LEN * 2);
    if (!pcm) { ndspExit(); return; }
    memset(pcm, 0, NBUF * LEN * 2);
    memset(wb, 0, sizeof wb);
    for (int i = 0; i < NBUF; i++) { wb[i].data_pcm16 = pcm + i * LEN; wb[i].nsamples = LEN; }
    LightEvent_Init(&ev, RESET_ONESHOT);
    ndspSetCallback(onFrame, NULL);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio = prio > 0x19 ? prio - 1 : 0x18;
    run = true;
    // New 3DS core 2 (free), else core 1 (system core, 30% for apps), else share core 0 at a higher priority
    th = threadCreate(mixer, NULL, 32 * 1024, prio, core = 2, false);
    if (!th) { APT_SetAppCpuTimeLimit(30); th = threadCreate(mixer, NULL, 32 * 1024, prio, core = 1, false); }
    if (!th) th = threadCreate(mixer, NULL, 32 * 1024, prio, core = 0, false);
    if (!th) { plog("audio: no mixer thread, running silent"); run = false; ndspExit(); linearFree(pcm); return; }
    ok = true;
    plog("audio: ndsp ok, mixer on core %d", core);
}

void audio_exit(void) {
    if (!ok) return;
    ok = false; run = false;
    LightEvent_Signal(&ev);
    threadJoin(th, U64_MAX); threadFree(th);
    ndspSetCallback(NULL, NULL);
    ndspChnWaveBufClear(0);
    ndspExit();
    linearFree(pcm);
}
void audio_pump(void) {}
static void pcOpen(void) {}
#else
static FILE *wav;
static double t0, busyMs;
double profNow(void);   // plat_pc.c
static void wavClose(void) {
    uint32_t n = (uint32_t)(clk * 2), r = n + 36;
    fseek(wav, 4, SEEK_SET); fwrite(&r, 4, 1, wav);
    fseek(wav, 40, SEEK_SET); fwrite(&n, 4, 1, wav);
    fclose(wav);
    fprintf(stderr, "audio: %.1f s → build/audio.wav, mixer %.3f ms per second of audio\n", clk / (double)SR, busyMs / (clk / (double)SR));
}
static void pcOpen(void) {
    static bool tried;
    if (tried) return;
    tried = true;
    if (!getenv("PF_WAV") || !(wav = fopen("build/audio.wav", "wb"))) return;
    tables();
    uint32_t h[11] = { 0x46464952, 0, 0x45564157, 0x20746d66, 16, 1 | 1 << 16, SR, SR * 2, 2 | 16 << 16, 0x61746164, 0 };
    fwrite(h, 4, 11, wav);   // RIFF/WAVE mono s16 (little-endian host)
    atexit(wavClose);
    ok = true;
}
void audio_init(void) { pcOpen(); }
void audio_exit(void) {}
void audio_pump(void) {
    pcOpen();
    if (!wav) return;
    double ms = nowMs();
    if (!t0) t0 = ms;
    int64_t target = (int64_t)((ms - t0) * SR / 1000);
    while (clk < target) {
        int16_t b[512]; int n = target - clk > 512 ? 512 : (int)(target - clk);
        double a = profNow();
        render(b, n);
        busyMs += profNow() - a;
        fwrite(b, 2, n, wav);
    }
}
#endif

bool a_cmd(int op, int a, int b, float x) {
    pcOpen();
    if (!ok) return false;
    unsigned h = qh;
    if (h - qt >= QN) return false;
    q[h & (QN - 1)] = (Cmd){ op, a, b, x };
    __sync_synchronize();
    qh = h + 1;
    return true;
}
