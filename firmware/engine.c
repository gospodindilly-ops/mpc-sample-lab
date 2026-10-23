// Sample playback engine, see engine.h.
#include "engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FADE 256  // 5.8 ms click-free cut (retrigger, mute group, stop)
#define TAIL 64   // 1.5 ms minimum fade before a sample's end: files may stop off zero

void pad_defaults(struct pad_params *p) {
    *p = (struct pad_params){.end = 1, .decay_from = DECAY_FROM_END, .vel_sens = 127, .loop_lock = 1};
}

void engine_init(struct engine *e) {
    memset(e, 0, sizeof *e);
    for (int b = 0; b < BANKS; b++)
        for (int i = 0; i < PADS; i++) pad_defaults(&e->pads[b][i].p);
    e->master = 0.25f;  // until MAIN VOLUME is moved: the knob position is unknown at start
    e->gain = -1;
}

float db_to_gain(float db) { return db <= VOL_MIN_DB ? 0 : powf(10, db / 20); }

float env_seconds(int v) { return v <= 0 ? 0 : 0.001f * powf(10000, v / 127.0f); }  // 1 ms .. 10 s

static int voice_of(const struct voice *v, int bank, int pad) { return v->active && v->bank == bank && v->idx == pad; }

int engine_playing(const struct engine *e, int bank, int pad) {
    for (int i = 0; i < VOICES; i++)
        if (voice_of(&e->voices[i], bank, pad) && !e->voices[i].fade) return 1;
    return 0;
}

void engine_stop(struct engine *e, int bank, int pad) {
    for (int i = 0; i < VOICES; i++)
        if (voice_of(&e->voices[i], bank, pad) && !e->voices[i].fade) e->voices[i].fade = FADE;
}

void engine_kill(struct engine *e, int bank, int pad) {
    for (int i = 0; i < VOICES; i++)
        if (voice_of(&e->voices[i], bank, pad)) e->voices[i].active = 0;
}

void engine_stop_all(struct engine *e) {
    for (int i = 0; i < VOICES; i++)
        if (e->voices[i].active && !e->voices[i].fade) e->voices[i].fade = FADE;
}

void engine_release(struct engine *e, int bank, int pad) {
    for (int i = 0; i < VOICES; i++)
        if (voice_of(&e->voices[i], bank, pad)) e->voices[i].released = 1;
}

static struct voice *alloc_voice(struct engine *e) {
    struct voice *oldest = &e->voices[0];
    for (int i = 0; i < VOICES; i++) {
        if (!e->voices[i].active) return &e->voices[i];
        if (e->voices[i].age < oldest->age) oldest = &e->voices[i];
    }
    return oldest;  // shortcut: stealing cuts the oldest voice without a fade; add spare fade slots if it clicks
}

void engine_trigger(struct engine *e, int bank, int idx, int velocity) {
    struct pad *pd = &e->pads[bank][idx];
    const struct pad_params *p = &pd->p;
    if (!pd->smp.lr || pd->muted) return;
    if (p->loop_on && !p->note_on && engine_playing(e, bank, idx)) {  // a one-shot loop is toggled by the pad
        engine_stop(e, bank, idx);
        return;
    }
    for (int i = 0; i < VOICES; i++) {
        struct voice *v = &e->voices[i];
        if (!v->active || v->fade) continue;
        int same = v->bank == bank && v->idx == idx;
        if ((p->mono && same) || (p->mute_group && !same && v->pad->p.mute_group == p->mute_group)) v->fade = FADE;
    }
    int n = pd->smp.frames, lo = (int)(p->start * n), hi = (int)(p->end * n);
    if (lo < 0) lo = 0;
    if (hi > n) hi = n;
    if (hi - lo < 4) return;
    struct voice *v = alloc_voice(e);
    float vs = p->vel_sens / 127.0f, vel = e->full_level ? 127 : velocity;
    float g = ((1 - vs) + vs * vel / 127.0f) * db_to_gain(p->vol_db);
    float a = env_seconds(p->attack) * RATE, d = env_seconds(p->decay) * RATE, r = env_seconds(p->release) * RATE;
    int loop_at = p->loop_lock ? lo : (int)(p->loop * n);
    loop_at = loop_at < lo ? lo : loop_at > hi - 2 ? hi - 2 : loop_at;
    *v = (struct voice){
        .pad = pd, .bank = bank, .idx = idx, .active = 1, .dir = p->reverse ? -1 : 1, .lo = lo, .hi = hi, .loop_at = loop_at,
        .step = pow(2.0, (p->semi + p->fine / 100.0) / 12.0),
        .gl = g * (p->pan > 0 ? 1 - p->pan : 1), .gr = g * (p->pan < 0 ? 1 + p->pan : 1),  // balance: centre stays at unity
        .env = a < 1 ? 1 : 0, .stage = a < 1 ? 1 : 0, .att_inc = a < 1 ? 0 : 1 / a,
        .dec_inc = d < 1 ? 1 : 1 / d, .rel_inc = r < TAIL ? 1.0f / TAIL : 1 / r, .end_frames = d < TAIL ? TAIL : d,
        .age = ++e->clock,
    };
    v->pos = v->dir > 0 ? lo : hi - 1;
}

// Adds one output frame of v to *l, *r; returns 0 when the voice has finished.
static int voice_tick(struct voice *v, float *l, float *r) {
    const struct pad_params *p = &v->pad->p;
    const float *x = v->pad->smp.lr;
    if (p->note_on && v->released && v->stage < 2) v->stage = 2;
    if (v->stage == 0 && (v->env += v->att_inc) >= 1) v->env = 1, v->stage = 1;
    if (v->stage == 1 && !p->note_on && p->decay_from == DECAY_FROM_START) v->stage = 2;
    if (v->stage == 2 && (v->env -= p->note_on ? v->rel_inc : v->dec_inc) <= 0) return 0;

    int floor_ = p->loop_on ? v->loop_at : v->lo;  // reverse loops run between the loop point and the end
    float g = v->env;
    if (!p->loop_on) {
        float left = (float)((v->dir > 0 ? v->hi - v->pos : v->pos - floor_) / v->step);
        float span = !p->note_on && p->decay_from == DECAY_FROM_END ? v->end_frames : TAIL;
        if (left < span) g *= left / span;
    }
    if (v->fade) {
        g *= (float)v->fade / FADE;
        if (--v->fade == 0) return 0;
    }
    int i = (int)v->pos, j = i + 1 < v->hi ? i + 1 : i;
    float fr = (float)(v->pos - i);
    *l += (x[2 * i] + (x[2 * j] - x[2 * i]) * fr) * g * v->gl;
    *r += (x[2 * i + 1] + (x[2 * j + 1] - x[2 * i + 1]) * fr) * g * v->gr;

    v->pos += v->step * v->dir;
    if (v->dir > 0 && v->pos >= v->hi) {
        if (!p->loop_on) return 0;
        v->pos = v->loop_at + fmod(v->pos - v->hi, (double)(v->hi - v->loop_at));
    } else if (v->dir < 0 && v->pos < floor_) {
        if (!p->loop_on) return 0;
        v->pos = v->hi - 1 - fmod(floor_ - v->pos, (double)(v->hi - floor_));
    }
    return 1;
}

static float soft_clip(float x) {  // cubic: unity slope at 0, reaches +-1 smoothly at +-1.5
    if (x > 1.5f) return 1;
    if (x < -1.5f) return -1;
    return x - (4.0f / 27.0f) * x * x * x;
}

#define GAIN_K 0.00227f  // one-pole smoothing of MAIN VOLUME / Kit Vol over ~10 ms: knob steps no longer crackle

void engine_render(struct engine *e, int32_t *out, int frames) {
    float target = db_to_gain(e->kit_db) * e->master, g = e->gain < 0 ? target : e->gain;
    for (int f = 0; f < frames; f++) {
        float l = 0, r = 0;
        for (int i = 0; i < VOICES; i++)
            if (e->voices[i].active && !voice_tick(&e->voices[i], &l, &r)) e->voices[i].active = 0;
        g += (target - g) * GAIN_K;
        out[2 * f] = (int32_t)(soft_clip(l * g) * 2147483520.0f);  // largest float below 2^31
        out[2 * f + 1] = (int32_t)(soft_clip(r * g) * 2147483520.0f);
    }
    e->gain = fabsf(target - g) < 1e-6f ? target : g;  // snap: no endless denormal tail
}

float sample_peak(const struct sample *s, float start, float end) {
    int lo = (int)(start * s->frames), hi = (int)(end * s->frames);
    float peak = 0;
    for (int i = 2 * (lo < 0 ? 0 : lo); i < 2 * (hi > s->frames ? s->frames : hi); i++) peak = fmaxf(peak, fabsf(s->lr[i]));
    return peak;
}

void sample_scale(struct sample *s, float gain) {
    for (int i = 0; i < 2 * s->frames; i++) s->lr[i] *= gain;
    s->gen++;
}

float *sample_slice(const struct sample *s, float start, float end, int *frames) {
    int lo = (int)(start * s->frames), hi = (int)(end * s->frames);
    lo = lo < 0 ? 0 : lo, hi = hi > s->frames ? s->frames : hi;
    if (hi - lo < 4) return NULL;
    float *out = malloc((size_t)(hi - lo) * 2 * sizeof *out);
    if (out) memcpy(out, s->lr + 2 * lo, (size_t)(hi - lo) * 2 * sizeof *out), *frames = hi - lo;
    return out;
}
