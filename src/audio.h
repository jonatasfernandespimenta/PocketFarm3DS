// Tiny software synth shared by sfx.c and music.c (WebAudio's oscillator/gain/biquad graph, rendered in C).
// Threading: the game calls sfx()/music API on the main thread; those only push commands into a lock-free
// queue. Everything else (voices, music sequencer) runs on the audio thread (3DS) or in the PC pump.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SR 32728   // ndsp's native output rate (no resampling)
enum { W_SINE, W_SQUARE, W_TRI, W_SAW, W_NOISE, W_HAT };   // W_HAT: highpassed noise with a linear fade
enum { F_LP, F_HP, F_BP };                                // BiquadFilter types used by the web
enum { OP_SFX, OP_START, OP_MOOD, OP_ENABLE };

// --- audio thread side (times are absolute seconds on the render clock) ---
double a_now(void);                                       // start of the block being rendered
float a_rand(void);                                       // Math.random()
// tone(): oscillator with an exponential pitch glide f0→f1 and 0.0001→vol→0.0001 exponential envelope
void a_tone(int type, float f0, float f1, double at, float dur, float vol, float attack);
// noise(): filtered white noise, vol→0.0001 exponential, filter cutoff glide freq→freqEnd
void a_noise(double at, float dur, float vol, int ftype, float freq, float q, float freqEnd);
void a_hat(double at, float vol);                         // music.js hat(): 30 ms, highpass 6 kHz, linear fade

// hooks run on the audio thread
void sfx_play(int id, int b, float x, double t);           // sfx.c
void music_op(int op, int a);                              // music.c
void music_tick(double t1);                                // music.c: schedule notes that start before t1

// --- main thread side ---
bool a_cmd(int op, int a, int b, float x);                 // queue a command (false if audio is off / queue full)
void audio_init(void);                                     // 3DS: ndspInit + mixer thread (silent if it fails)
void audio_exit(void);
const char *audio_problem(void);                           // why there's no sound (shown once at boot), or NULL
void audio_pump(void);                                     // PC only (PF_WAV=1): render up to nowMs() into build/audio.wav

// sfx with the web's opts: crank {dur}, roll {dur}, bounce {vol} → x; tada {rarity} → "common"/"rare"/"legendary"
void sfxOpt(const char *name, float x, const char *rarity);
