// sfx.js: synthesized sound effects (no audio files). The tables/recipes below run on the audio thread
// (sfx_play); the game-facing calls just queue a command.
#include <string.h>
#include "audio.h"
#include "game.h"

static bool muted;
void setMuted(bool m) { muted = m; }

// Simple sounds: [waveform, startHz, endHz, seconds, volume] played in sequence.
typedef struct { uint8_t w; float f0, f1, dur, vol; } Note;
static const struct { const char *name; Note n[3]; } SIMPLE[] = {
    { "hoe",   { { W_TRI, 180, 90, 0.08f, 0.25f } } },
    { "water", { { W_SINE, 900, 500, 0.12f, 0.12f }, { W_SINE, 700, 400, 0.1f, 0.08f } } },
    { "chop",  { { W_SQUARE, 220, 120, 0.06f, 0.12f }, { W_TRI, 120, 80, 0.08f, 0.2f } } },
    { "pick",  { { W_SQUARE, 880, 600, 0.04f, 0.1f }, { W_TRI, 300, 200, 0.06f, 0.15f } } },
    { "break", { { W_SAW, 300, 60, 0.18f, 0.12f } } },
    { "pop",   { { W_SINE, 500, 1100, 0.08f, 0.18f } } },
    { "bite",  { { W_SQUARE, 660, 660, 0.05f, 0.12f }, { W_SQUARE, 990, 990, 0.08f, 0.12f } } },
    { "catch", { { W_TRI, 523, 523, 0.08f, 0.15f }, { W_TRI, 659, 659, 0.08f, 0.15f }, { W_TRI, 784, 784, 0.14f, 0.15f } } },
    { "miss",  { { W_TRI, 300, 150, 0.2f, 0.15f } } },
    { "click", { { W_SQUARE, 600, 600, 0.02f, 0.05f } } },
    { "blip",  { { W_SQUARE, 520, 560, 0.025f, 0.025f } } },   // dialogue typing
    { "door",  { { W_TRI, 220, 330, 0.12f, 0.15f } } },
};
#define NSIMPLE (int)(sizeof SIMPLE / sizeof *SIMPLE)
static const char *RICH[] = { "coin", "crank", "clunk", "bounce", "roll", "rattle", "pu", "tada", "step", "twinkle" };
enum { COIN = NSIMPLE, CRANK, CLUNK, BOUNCE, ROLL, RATTLE, PU, TADA, FOOTSTEP, TWINKLE, NSFX };

static void tone(int w, float f0, float f1, double at, float dur, float vol) { a_tone(w, f0, f1, at, dur, vol, 0.005f); }
#define R a_rand()

void sfx_play(int id, int b, float x, double t) {
    if (id < NSIMPLE) {
        double at = t;
        for (int i = 0; i < 3 && SIMPLE[id].n[i].dur > 0; i++) {
            const Note *n = &SIMPLE[id].n[i];
            tone(n->w, n->f0, n->f1, at, n->dur, n->vol);
            at += n->dur * 0.8;
        }
        return;
    }
    switch (id) {
    case COIN:     // coin sliding in: bright double "tlim"
        tone(W_SINE, 2637, 2600, t, 0.18f, 0.12f);
        tone(W_TRI, 3520, 3400, t + 0.06, 0.25f, 0.08f);
        a_noise(t, 0.04f, 0.05f, F_HP, 5000, 1, 5000);
        break;
    case CRANK: {  // crank ratchet "qrrrrrr": clicks speeding up then slowing down over `dur`
        float dur = x >= 0 ? x : 1;
        for (int i = 0; i < 16; i++) {
            float k = i / 15.0f, e = k < 0.5f ? 2 * k * k : 1 - (-2 * k + 2) * (-2 * k + 2) / 2;
            double at = t + dur * e;
            a_noise(at, 0.025f, 0.22f, F_BP, 2200 + R * 600, 6, 2200);
            tone(W_SQUARE, 180 + R * 40, 120, at, 0.02f, 0.03f);
        }
        a_noise(t, dur, 0.04f, F_LP, 400, 1, 400);   // mechanism hum
        break;
    }
    case CLUNK:    // capsule released inside the machine: low hollow "tunc"
        tone(W_SINE, 150, 70, t, 0.16f, 0.35f);
        a_noise(t, 0.08f, 0.15f, F_LP, 700, 1, 700);
        break;
    case BOUNCE: { // hollow plastic bounce "blub"; vol 0..1 scales each bounce
        float vol = x >= 0 ? x : 1;
        a_tone(W_SINE, 520, 230, t, 0.09f, 0.28f * vol, 0.002f);
        a_tone(W_TRI, 780, 380, t, 0.05f, 0.08f * vol, 0.002f);
        a_noise(t, 0.03f, 0.08f * vol, F_BP, 1500, 3, 1500);
        break;
    }
    case ROLL: {   // rolling to a stop: soft gravelly rumble fading out
        float dur = x >= 0 ? x : 0.6f;
        a_noise(t, dur, 0.09f, F_BP, 900, 2, 300);
        for (int i = 0; i < 6; i++) a_noise(t + i * dur / 7, 0.02f, 0.05f * (1 - i / 6.0f), F_BP, 1800, 5, 1800);
        break;
    }
    case RATTLE:   // capsule wobbling before it opens: little plastic rattles
        for (int i = 0; i < 5; i++) {
            a_noise(t + i * 0.09, 0.03f, 0.12f, F_BP, 2500 + i * 150, 5, 2500 + i * 150);
            tone(W_TRI, 900 + i * 40, 700, t + i * 0.09, 0.03f, 0.05f);
        }
        break;
    case PU:       // the opening "pu": a muffled bubble pop
        a_tone(W_SINE, 420, 90, t, 0.11f, 0.5f, 0.002f);
        a_tone(W_SINE, 210, 60, t, 0.14f, 0.25f, 0.002f);
        a_noise(t, 0.09f, 0.3f, F_LP, 1200, 0.7f, 250);
        a_noise(t + 0.03, 0.25f, 0.05f, F_BP, 3000, 1, 6000);   // little air/sparkle tail
        break;
    case TADA: {   // fanfare: bigger for rarer critters (b: 0 common, 1 rare, 2 legendary)
        static const float C[] = { 523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f, 1568 };
        static const float CH[3][5] = { { 783.99f, 1046.5f }, { 659.25f, 783.99f, 1046.5f }, { 523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f } };
        static const int nch[3] = { 2, 3, 5 }, nchime[3] = { 0, 7, 14 };
        static const float hold[3] = { 0.6f, 0.9f, 1.4f };
        int na = b == 2 ? 6 : 4;
        for (int i = 0; i < na; i++) {
            tone(W_TRI, C[i], C[i], t + i * 0.08, 0.22f, 0.16f);
            tone(W_SQUARE, C[i], C[i], t + i * 0.08, 0.12f, 0.03f);
        }
        double end = t + na * 0.08;
        for (int i = 0; i < nch[b]; i++) {
            float f = CH[b][i];
            a_tone(W_TRI, f, f, end, hold[b], 0.12f, 0.02f);
            a_tone(W_SINE, f * 2, f * 2, end, hold[b] * 0.8f, 0.03f, 0.02f);
        }
        for (int i = 0; i < nchime[b]; i++) {   // shimmering chimes
            float f = 2000 + R * 2500;
            tone(W_SINE, f, f * 1.01f, end + i * 0.07, 0.18f, 0.05f);
        }
        break;
    }
    case FOOTSTEP:     // footstep: soft swish on grass, crunch on dirt, hard tap in the cave (b: 0 grass, 1 dirt, 2 cave)
        if (b == 1) { float f = 1400 + R * 400; a_noise(t, 0.06f, 0.07f, F_BP, f, 1.5f, f); tone(W_SINE, 120, 70, t, 0.04f, 0.03f); }
        else if (b == 2) { float f = 900 + R * 300; a_noise(t, 0.04f, 0.09f, F_BP, f, 3, f); tone(W_TRI, 200, 110, t, 0.05f, 0.05f); }
        else a_noise(t, 0.07f, 0.05f, F_LP, 2200 + R * 800, 0.8f, 600);
        break;
    case TWINKLE: { // single twinkle (reveal sparkles)
        float f = 2400 + R * 1800;
        tone(W_SINE, f, f, t, 0.12f, 0.035f);
        break;
    }
    }
}

void sfxOpt(const char *name, float x, const char *rarity) {
    if (muted || !name) return;
    int id = -1;
    for (int i = 0; i < NSIMPLE && id < 0; i++) if (!strcmp(name, SIMPLE[i].name)) id = i;
    for (int i = 0; i < NSFX - NSIMPLE && id < 0; i++) if (!strcmp(name, RICH[i])) id = NSIMPLE + i;
    if (id < 0) return;   // unknown name: silent, like SIMPLE[name] || []
    int b = rarity && !strcmp(rarity, "legendary") ? 2 : rarity && !strcmp(rarity, "rare") ? 1 : 0;
    a_cmd(OP_SFX, id, b, x);
}
void sfx(const char *name) { sfxOpt(name, -1, NULL); }
void sfxStep(const char *surface) {
    if (muted) return;
    a_cmd(OP_SFX, FOOTSTEP, surface && !strcmp(surface, "dirt") ? 1 : surface && !strcmp(surface, "cave") ? 2 : 0, -1);
}
