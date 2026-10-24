// Sample playback engine: 16 tracks, a Drum track plays 8 banks x 16 pads; 32 stereo voices shared by all. Pure C, no I/O: runs in the audio thread
// and in host self-tests. Parameter ranges follow the MPC Sample User Guide (Sample Mode).
#pragma once
#include <stdint.h>

#define RATE 44100
#define TRACKS 16
#define BANKS 8
#define PADS 16
#define VOICES 32
#define VOL_MIN_DB (-75.0f)  // this value and below means -INF

// One decoded sample, interleaved stereo -1..1. Allocated once together with its length and never changed, so
// every thread holding the pointer sees matching frames and data; an edit (trim, normalize) makes a new one.
struct sample { int frames; char name[64]; float lr[]; };

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

struct pad { struct sample *smp; struct pad_params p; int muted; };  // smp NULL: empty pad; only the audio thread sets it

// Drum: pads play samples. Audio: one recording on the timeline. MIDI: pads and events go out as MIDI notes.
// Sample (by notes, like an MPC 3 Keygroup) comes later; it is last so the Type knob can stop before it.
enum { TRACK_EMPTY, TRACK_DRUM, TRACK_AUDIO, TRACK_MIDI, TRACK_SAMPLE };
enum { PORT_EXTERNAL, PORT_USB };  // the MPC Sample's two MIDI ports: TRS jacks and USB-C

struct track {
    int type;
    float vol_db;                  // track volume; on a Drum track the guide's "Kit Volume"
    float gain;                    // as heard: follows vol_db smoothly; < 0 before the first render
    int midi_port, midi_ch;        // MIDI track: PORT_*, channel 1..16
    int muted;                     // Track Mute: its sequence events do not play (pads still do)
    struct pad pads[BANKS][PADS];  // Drum program (kept when the type changes, so changing back loses nothing)
};

struct voice {
    const struct pad *pad;
    int track, active, released, stage;
    double pos, step;              // read position and step in sample frames
    int dir, lo, hi, loop_at;      // +1/-1; play range [lo, hi); loop restart point
    float gl, gr, env, att_inc, dec_inc, rel_inc, end_frames;
    int fade;                      // > 0: frames left of a click-free cut
    int wait;                      // frames of silence before it starts: sequence events land on their exact frame
    unsigned age;
};

struct engine {
    struct track tracks[TRACKS];
    struct voice voices[VOICES];
    float master;                  // MAIN VOLUME gain 0..1
    float gain;                    // master as heard: follows it smoothly; < 0 before the first render
    float peak;                    // loudest output sample of the last render, for the level meter
    float click_env, click_phase, click_step;  // metronome: a short sine blip of our own, no sampled clicks
    int click_wait;
    int full_level;                // 1: every hit at velocity 127
    unsigned clock;
};

void pad_defaults(struct pad_params *p);
void engine_init(struct engine *e);
float db_to_gain(float db);
float env_seconds(int v);          // 0..127 -> envelope time; 0 is instant
// t: track 0..15. Mute groups and Mono act inside the track's program.
void engine_trigger(struct engine *e, int t, int bank, int pad, int velocity);
void engine_trigger_at(struct engine *e, int t, int bank, int pad, int velocity, int offset);  // offset frames into the block
void engine_click(struct engine *e, int accent, float level, int offset);  // metronome: accent on the bar's first beat
void engine_release(struct engine *e, int t, int bank, int pad);  // pad lifted: ends Note On voices
void engine_stop(struct engine *e, int t, int bank, int pad);     // click-free fade of a pad's voices
void engine_kill(struct engine *e, int t, int bank, int pad);     // immediate stop (before its sample buffer changes)
void engine_stop_all(struct engine *e);
void engine_stop_track(struct engine *e, int t);
int engine_playing(const struct engine *e, int t, int bank, int pad);
void engine_render(struct engine *e, int32_t *out, int frames);  // interleaved stereo S32_LE
// Sample helpers for the file thread; they allocate. Each returns a new sample or NULL.
struct sample *sample_new(int frames, const char *name);  // audio left uninitialised
struct sample *sample_slice(const struct sample *s, float start, float end);
struct sample *sample_gain(const struct sample *s, float gain);
float sample_peak(const struct sample *s, float start, float end);
