// Sample playback engine: 8 banks x 16 pads, 32 stereo voices. Pure C, no I/O: runs in the audio thread
// and in host self-tests. Parameter ranges follow the MPC Sample User Guide (Sample Mode).
#pragma once
#include <stdint.h>

#define RATE 44100
#define BANKS 8
#define PADS 16
#define VOICES 32
#define VOL_MIN_DB (-75.0f)  // this value and below means -INF

// Interleaved stereo -1..1; lr == NULL: empty pad. gen counts in-place edits (normalize) for the screen's cache.
struct sample { float *lr; int frames; unsigned gen; char name[64]; };

enum { DECAY_FROM_START, DECAY_FROM_END };

struct pad_params {
    float start, end, loop;        // 0..1 of the sample length
    float vol_db;                  // VOL_MIN_DB (-INF) .. +6
    float pan;                     // -1 (50L) .. +1 (50R)
    int attack, decay, release;    // 0..127
    int decay_from;                // DECAY_FROM_START or DECAY_FROM_END (one-shot only)
    int vel_sens;                  // 0..127; 0 = always full velocity
    int semi, fine;                // -24..24 semitones, -90..90 cents
    int mono;                      // 1: a new hit restarts the pad, 0: hits overlap
    int mute_group;                // 0 = off, 1..16
    int note_on, loop_on, reverse, loop_lock;
};

struct pad { struct sample smp; struct pad_params p; int muted; };

struct voice {
    const struct pad *pad;
    int bank, idx, active, released, stage;
    double pos, step;              // read position and step in sample frames
    int dir, lo, hi, loop_at;      // +1/-1; play range [lo, hi); loop restart point
    float gl, gr, env, att_inc, dec_inc, rel_inc, end_frames;
    int fade;                      // > 0: frames left of a click-free cut
    unsigned age;
};

struct engine {
    struct pad pads[BANKS][PADS];
    struct voice voices[VOICES];
    float kit_db, master;          // master: MAIN VOLUME gain 0..1
    float gain;                    // output gain as heard: follows kit_db and master smoothly; < 0 before the first render
    int full_level;                // 1: every hit at velocity 127
    unsigned clock;
};

void pad_defaults(struct pad_params *p);
void engine_init(struct engine *e);
float db_to_gain(float db);
float env_seconds(int v);          // 0..127 -> envelope time; 0 is instant
void engine_trigger(struct engine *e, int bank, int pad, int velocity);
void engine_release(struct engine *e, int bank, int pad);  // pad lifted: ends Note On voices
void engine_stop(struct engine *e, int bank, int pad);     // click-free fade of a pad's voices
void engine_kill(struct engine *e, int bank, int pad);     // immediate stop (before its sample buffer changes)
void engine_stop_all(struct engine *e);
int engine_playing(const struct engine *e, int bank, int pad);
void engine_render(struct engine *e, int32_t *out, int frames);  // interleaved stereo S32_LE
float sample_peak(const struct sample *s, float start, float end);
void sample_scale(struct sample *s, float gain);
float *sample_slice(const struct sample *s, float start, float end, int *frames);  // new buffer or NULL
