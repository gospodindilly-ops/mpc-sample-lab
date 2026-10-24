// Parameter table, see params.h. Ranges follow the MPC Sample User Guide (Sample Mode).
#include "params.h"
#include "seq.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define PADF(f) .in = IN_PAD, .at = offsetof(struct pad_params, f)
#define PADI(f) PADF(f), .is_int = 1
#define TRKI(f) .in = IN_TRACK, .at = offsetof(struct track, f), .is_int = 1
#define SQI(f) .in = IN_SQ, .at = offsetof(struct sequencer, f), .is_int = 1

const struct param PARAMS[P_COUNT] = {
    [P_NONE] = {.name = ""},
    [P_START] = {"Start", 0, 100, 0.5f, 100, SHOW_PCT, PADF(start)},
    [P_END] = {"End", 0, 100, 0.5f, 100, SHOW_PCT, PADF(end)},
    [P_LOOP] = {"Loop", 0, 100, 0.5f, 100, SHOW_PCT, PADF(loop)},
    [P_VOL] = {"Volume", VOL_MIN_DB, 6, 0.5f, 1, SHOW_DB, PADF(vol_db)},
    [P_KITVOL] = {"Kit Vol", VOL_MIN_DB, 6, 0.5f, 1, SHOW_DB, .in = IN_TRACK, .at = offsetof(struct track, vol_db)},
    [P_PAN] = {"Pan", -50, 50, 1, 50, SHOW_PAN, PADF(pan), .whole = 1},
    [P_ATTACK] = {"Attack", 0, 127, 1, 1, SHOW_TIME, PADI(attack)},
    [P_DECAY] = {"Decay", 0, 127, 1, 1, SHOW_TIME, PADI(decay)},
    [P_RELEASE] = {"Release", 0, 127, 1, 1, SHOW_TIME, PADI(release)},
    [P_VELSENS] = {"Vel Sens", 0, 127, 1, 1, SHOW_INT, PADI(vel_sens)},
    [P_DECAYFROM] = {"Decay From", 0, 1, 1, 1, SHOW_LIST, PADI(decay_from), .list = {"Start", "End"}},
    [P_SEMI] = {"Semi Tune", -24, 24, 1, 1, SHOW_SEMI, PADI(semi)},
    [P_FINE] = {"Fine Tune", -90, 90, 1, 1, SHOW_CENTS, PADI(fine)},
    [P_MONO] = {"Polyphony", 0, 1, 1, 1, SHOW_LIST, PADI(mono), .list = {"Poly", "Mono"}},
    [P_MUTEGRP] = {"Mute Group", 0, 16, 1, 1, SHOW_OFF, PADI(mute_group)},
    [P_TTYPE] = {"Type", TRACK_EMPTY, TRACK_MIDI, 1, 1, SHOW_LIST, TRKI(type), .list = {"Empty", "Drum", "Audio", "MIDI"}},
    [P_TRKVOL] = {"Volume", VOL_MIN_DB, 6, 0.5f, 1, SHOW_DB, .in = IN_TRACK, .at = offsetof(struct track, vol_db)},
    [P_MIDIPORT] = {"MIDI Port", PORT_EXTERNAL, PORT_USB, 1, 1, SHOW_LIST, TRKI(midi_port), .list = {"External", "USB"}},
    [P_MIDICH] = {"Channel", 1, 16, 1, 1, SHOW_INT, TRKI(midi_ch)},
    [P_SEQBARS] = {"Bars", 0, 7, 1, 1, SHOW_BARS2, .in = IN_SEQ, .at = offsetof(struct sequence, bars), .is_int = 1},
    [P_SEQBARS1] = {"Bars", 1, 128, 1, 1, SHOW_BARS, .in = IN_SEQ, .at = offsetof(struct sequence, bars), .is_int = 1},
    [P_SEQQ] = {"Q", 0, 7, 1, 1, SHOW_LIST, SQI(q), .list = {"Q 1/4", "Q 1/4T", "Q 1/8", "Q 1/8T", "Q 1/16", "Q 1/16T", "Q 1/32", "Q 1/32T"}},
    [P_SIG] = {"Time Sig", 0, 11, 1, 1, SHOW_LIST, SQI(sig),
               .list = {"4/4", "3/4", "2/4", "5/4", "6/4", "7/4", "3/8", "5/8", "6/8", "7/8", "9/8", "12/8"}},  // as SIGS
    [P_SWING] = {"RT Swing", 50, 75, 1, 1, SHOW_PCT, SQI(swing)},
    [P_METROVOL] = {"Click Vol", 0, 100, 1, 1, SHOW_PCT, SQI(metro_vol)},
    [P_EVVEL] = {"Velocity", 1, 127, 1, 1, SHOW_INT, .in = IN_EVENT, .at = offsetof(struct event, vel), .is_int = 2},
    [P_EVLEN] = {"Length", 10, 2 * 4 * PPQ, 10, 1, SHOW_LEN, .in = IN_EVENT, .at = offsetof(struct event, len), .is_int = 3},
    [P_TLEN] = {"Length", 0, 64, 1, 1, SHOW_BEATS, .in = IN_SEQTRACK, .at = offsetof(struct seqtrack, len_beats), .is_int = 1},
};

float param_get(const void *base, int id) {
    if (id <= P_NONE || id >= P_COUNT) return 0;
    const struct param *p = &PARAMS[id];
    const char *at = (const char *)base + p->at;
    float v = (p->is_int == 1 ? *(const int *)at : p->is_int == 2 ? *(const uint8_t *)at : p->is_int == 3 ? *(const uint32_t *)at
               : *(const float *)at) * p->scale;
    return p->show == SHOW_BARS2 ? log2f(v) : v;
}

void param_set(void *base, int id, float v) {
    if (id <= P_NONE || id >= P_COUNT) return;
    const struct param *p = &PARAMS[id];
    v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
    if (p->is_int || p->whole) v = lroundf(v);
    char *at = (char *)base + p->at;
    if (p->show == SHOW_BARS2) v = exp2f(v);
    if (p->is_int == 2) { *(uint8_t *)at = (uint8_t)v; return; }
    if (p->is_int == 3) { *(uint32_t *)at = (uint32_t)v; return; }
    if (p->is_int) { *(int *)at = (int)v; return; }
    float s = v / p->scale;
    const struct pad_params *pp = base;
    if (id == P_START) s = fminf(s, pp->end - 0.001f);  // start stays before end
    if (id == P_END) s = fmaxf(s, pp->start + 0.001f);
    *(float *)at = s;
}

void param_text(int id, float v, char *buf, int n) {
    if (id <= P_NONE || id >= P_COUNT) { snprintf(buf, n, "-"); return; }
    const struct param *p = &PARAMS[id];
    int iv = (int)lroundf(v);
    switch (p->show) {
    case SHOW_PCT: snprintf(buf, n, "%.1f%%", v); break;
    case SHOW_DB: v <= VOL_MIN_DB ? snprintf(buf, n, "-INF") : snprintf(buf, n, "%+.1fdB", v); break;
    case SHOW_PAN: iv ? snprintf(buf, n, "%d%c", abs(iv), iv < 0 ? 'L' : 'R') : snprintf(buf, n, "C"); break;
    case SHOW_SEMI: snprintf(buf, n, "%+d st", iv); break;
    case SHOW_CENTS: snprintf(buf, n, "%+d ct", iv); break;
    case SHOW_OFF: iv ? snprintf(buf, n, "%d", iv) : snprintf(buf, n, "Off"); break;
    case SHOW_LIST: snprintf(buf, n, "%s", p->list[iv - (int)p->lo]); break;
    case SHOW_TIME: {  // envelope stages: the time the 0..127 value stands for
        float s = env_seconds(iv);
        s < 1 ? snprintf(buf, n, "%.0f ms", s * 1000) : snprintf(buf, n, "%.2f s", s);
        break;
    }
    case SHOW_BARS2: iv = (int)lroundf(exp2f(v));  // fall through: as bars
    case SHOW_BARS: snprintf(buf, n, iv == 1 ? "%d Bar" : "%d Bars", iv); break;
    case SHOW_LEN: snprintf(buf, n, "%.2f Beats", v / PPQ); break;  // note length: quarter-note beats
    case SHOW_BEATS: iv ? snprintf(buf, n, iv == 1 ? "%d Beat" : "%d Beats", iv) : snprintf(buf, n, "Seq"); break;
    default: snprintf(buf, n, "%d", iv); break;
    }
}
