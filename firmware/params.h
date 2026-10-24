// Parameter table: one row per parameter gives its range, step, how it shows and where it is stored. Knobs,
// encoder and fader use it now; automation, MIDI Learn and the project file are meant to use the same rows.
#pragma once
#include "engine.h"

enum {
    P_NONE, P_START, P_END, P_LOOP, P_VOL, P_KITVOL, P_PAN, P_ATTACK, P_DECAY, P_RELEASE, P_VELSENS,
    P_DECAYFROM, P_SEMI, P_FINE, P_MONO, P_MUTEGRP, P_TTYPE, P_TRKVOL, P_MIDIPORT, P_MIDICH,
    P_SEQBARS, P_SEQBARS1, P_SEQQ, P_SIG, P_SWING, P_METROVOL, P_TLEN, P_EVVEL, P_EVLEN, P_COUNT
};
enum { SHOW_INT, SHOW_PCT, SHOW_DB, SHOW_PAN, SHOW_SEMI, SHOW_CENTS, SHOW_OFF, SHOW_LIST, SHOW_TIME,
       SHOW_BARS, SHOW_BARS2, SHOW_BEATS, SHOW_LEN };  // SHOW_BARS2: the shown value is log2 of the bars, so a knob steps 1, 2, 4 .. 128
enum { IN_PAD, IN_TRACK, IN_SEQ, IN_SQ, IN_SEQTRACK, IN_EVENT };  // pad_params, track, current sequence, sequencer,
                                                                    // its seqtrack, the piano roll's selected event

struct param {
    const char *name;
    float lo, hi, step;      // in shown units: a knob sweeps lo..hi, the encoder moves one step
    float scale;             // shown = stored * scale
    unsigned char show, in;
    unsigned char is_int;    // stored as 1 int, 2 uint8_t, 3 uint32_t; 0 float
    unsigned char whole;     // float that takes whole shown values only
    unsigned at;             // offset in its struct (the sequencer is bigger than 64 KB)
    const char *list[12];    // SHOW_LIST: labels for lo, lo + 1, ...
};

extern const struct param PARAMS[P_COUNT];
float param_get(const void *base, int id);    // base: the struct PARAMS[id].in names
void param_set(void *base, int id, float v);  // clamped to the range, rounded as the row says
void param_text(int id, float v, char *buf, int n);
