// music.js: procedural chiptune background music. A soft lead + bass + light percussion, with scale, tempo
// and mood picked from the context (season, night, mine, festival). The sequencer runs on the audio thread,
// sample-accurately inside the mixer (music_tick) instead of a 50 ms setInterval with 0.2 s lookahead.
#include <math.h>
#include <string.h>
#include "audio.h"
#include "game.h"

typedef struct { const char *id; int root, scale[5], prog[4], bpm, lead; float vol; } Mood;
static const Mood MOODS[] = {
    { "spring",   60, { 0, 2, 4, 7, 9 },  { 0, 7, 9, 5 },  96,  W_SQUARE, 1 },
    { "summer",   62, { 0, 2, 4, 7, 9 },  { 0, 5, 7, 5 },  112, W_SQUARE, 1 },
    { "fall",     57, { 0, 3, 5, 7, 10 }, { 0, 8, 3, 10 }, 90,  W_TRI,    1 },
    { "winter",   64, { 0, 3, 5, 7, 10 }, { 0, 8, 5, 3 },  72,  W_TRI,    0.8f },
    { "night",    57, { 0, 2, 3, 7, 8 },  { 0, 5, 3, 7 },  66,  W_SINE,   0.7f },
    { "mine",     50, { 0, 1, 5, 7, 8 },  { 0, 1, 0, 8 },  70,  W_TRI,    0.7f },
    { "festival", 65, { 0, 2, 4, 7, 9 },  { 0, 5, 9, 7 },  128, W_SQUARE, 1 },
};
#define MASTER 0.6f

// ---- audio thread ----
static const Mood *mood = &MOODS[0];
static bool playing, enabled = true;
static int beat;
static double nextTime;
static int8_t melody[32];   // scale degree, -1 = rest
static uint32_t seed = 1;

static double NOTE(int n) { return 440 * pow(2, (n - 69) / 12.0); }   // MIDI → Hz
static double rnd(void) { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0; }

// 4 bars × 8 eighth notes; stepwise motion over the scale, rests here and there.
static void compose(void) {
    seed += 7919;
    int deg = 4;
    for (int i = 0; i < 32; i++) {
        if (i % 8 == 7 && rnd() < 0.6) { melody[i] = -1; continue; }
        if (rnd() < 0.2) { melody[i] = -1; continue; }
        deg += (int)floor((rnd() - 0.5) * 3 + 0.5);   // Math.round
        deg = deg < 0 ? 0 : deg > 9 ? 9 : deg;
        melody[i] = deg;
    }
}

static void voice(int type, double freq, double at, double dur, float vol) { a_tone(type, freq, freq, at, dur, vol * MASTER, 0.015f); }

void music_tick(double t1) {
    if (!playing) return;
    bool hats = strcmp(mood->id, "night") && strcmp(mood->id, "mine");
    while (nextTime < t1) {
        double eighth = 60.0 / mood->bpm / 2;
        int bar = beat / 8 % 4, chord = mood->root + mood->prog[bar], deg = melody[beat % 32];
        if (deg >= 0) {
            int octave = deg / 5, n = mood->scale[deg % 5];
            voice(mood->lead, NOTE(chord + 12 + n + octave * 12 - mood->prog[bar]), nextTime, eighth * 1.6, 0.05f * mood->vol);
        }
        if (beat % 4 == 0) voice(W_TRI, NOTE(chord - 12), nextTime, eighth * 3.5, 0.09f * mood->vol);
        if (beat % 8 == 4) voice(W_TRI, NOTE(chord - 5), nextTime, eighth * 1.5, 0.05f * mood->vol);
        if (hats && beat % 2 == 1) a_hat(nextTime, 0.015f * mood->vol * MASTER);
        nextTime += eighth;
        if (++beat % 32 == 0) compose();
    }
}

void music_op(int op, int a) {
    if (op == OP_START) {
        if (playing || !enabled) return;
        compose();
        nextTime = a_now() + 0.1;
        playing = true;
    } else if (op == OP_MOOD) {
        if (&MOODS[a] == mood) return;
        mood = &MOODS[a]; compose();
    } else if (op == OP_ENABLE) {
        enabled = a;
        if (!a) playing = false;   // ponytail: no 0.2 s of already-scheduled notes ringing out
    }
}

// ---- main thread ----
static const char *moodId = "spring";
static int enabledSent = -1;
static bool gestured;

void startMusic(void) { a_cmd(OP_START, 0, 0, 0); }

// Called every frame by the game: a strcmp when nothing changed. Also where "music can only start after a
// user gesture" lives (web: once-only pointerdown/keydown listener), and the PC harness's wav pump.
void setMusicMood(const char *id) {
    audio_pump();
    if (!gestured && (in.down || in.touchDown)) { gestured = true; if (!has(state, "musicOff")) startMusic(); }
    if (!id || !strcmp(id, moodId)) return;
    for (int i = 0; i < (int)(sizeof MOODS / sizeof *MOODS); i++)
        if (!strcmp(id, MOODS[i].id)) { if (a_cmd(OP_MOOD, i, 0, 0)) moodId = MOODS[i].id; return; }
}

void setMusicEnabled(bool on) {
    if (on != enabledSent && a_cmd(OP_ENABLE, on, 0, 0)) enabledSent = on;
}
