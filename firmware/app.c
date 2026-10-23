// Sample Mode controller, see app.h. Behaviour follows the MPC Sample User Guide; functions of later phases
// answer with a "later" popup and keep their LEDs off ("not available", as the guide's LED convention says).
#include "app.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "surface.h"

enum { N_SAMPLE = 6, N_SEQ = 7, N_SHIFT = 49, N_PADBANK = 35, N_ERASE = 9, N_CHOP = 75, N_MUTE = 43, N_LOOP = 74,
       N_B1 = 12, N_B2 = 13, N_B3 = 14, N_ENCPUSH = 111, N_MINUS = 55, N_PLUS = 54, N_STOP = 81 };
enum { CC_K1 = 16, CC_K2 = 17, CC_K3 = 18, CC_MAIN_VOLUME = 19, CC_FADER = 20, CC_ENCODER = 100, CC_BANK_LED = 57 };

static const struct { const char *name; int k[3], shift_k[3]; } PAGES[3][3] = {
    {{"TRIM", {P_START, P_END, P_LOOP}, {0}},
     {"MIX", {P_VOL, P_NONE, P_PAN}, {P_NONE, P_KITVOL, P_NONE}},
     {"AMP ENV", {P_ATTACK, P_DECAY, P_VELSENS}, {P_NONE, P_DECAYFROM, P_NONE}}},
    {{"TUNE", {P_SEMI, P_FINE, P_NONE}, {0}}, {"PLAY", {P_MONO, P_MUTEGRP, P_NONE}, {0}}},
    {{"FILTER", {0}, {0}}, {"FILT ENV", {0}, {0}}},  // phase 2: empty pages say "later"
};
static const int NPAGES[3] = {3, 2, 2};

static const char *SHIFT_PAD[16] = {"Full Level", "Half Seq", "Double Seq", "Count-In", "Compressor", "Half Speed",
                                    "Double Speed", "MIDI Config", "Fader", "Rec Quantize", "Resample", "Song",
                                    "Trim Sample", "Time Correct", "Warp", "Project"};

static void popup(struct app *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->popup, sizeof a->popup, fmt, ap);
    va_end(ap);
    a->popup_until = a->now + RATE * 6 / 5;
}

static struct pad *cur(const struct app *a) { return &a->e->pads[a->bank][a->pad]; }

void app_init(struct app *a, struct engine *e) {
    memset(a, 0, sizeof *a);
    a->e = e;
    a->knob[0] = a->knob[1] = a->knob[2] = a->fader = -1;
    a->redraw = 1;
}

// ---- parameters ---------------------------------------------------------------------------------------

static void prange(int id, float *lo, float *hi, float *step) {
    switch (id) {
    case P_START: case P_END: case P_LOOP: *lo = 0, *hi = 100, *step = 0.5f; return;
    case P_VOL: case P_KITVOL: *lo = VOL_MIN_DB, *hi = 6, *step = 0.5f; return;
    case P_PAN: *lo = -50, *hi = 50, *step = 1; return;
    case P_DECAYFROM: case P_MONO: *lo = 0, *hi = 1, *step = 1; return;
    case P_SEMI: *lo = -24, *hi = 24, *step = 1; return;
    case P_FINE: *lo = -90, *hi = 90, *step = 1; return;
    case P_MUTEGRP: *lo = 0, *hi = 16, *step = 1; return;
    default: *lo = 0, *hi = 127, *step = 1; return;  // attack, decay/release, vel sens
    }
}

static float pget(const struct app *a, int id) {
    const struct pad_params *p = &cur(a)->p;
    switch (id) {
    case P_START: return p->start * 100;
    case P_END: return p->end * 100;
    case P_LOOP: return p->loop * 100;
    case P_VOL: return p->vol_db;
    case P_KITVOL: return a->e->kit_db;
    case P_PAN: return p->pan * 50;
    case P_ATTACK: return p->attack;
    case P_DECAY: return p->note_on ? p->release : p->decay;
    case P_VELSENS: return p->vel_sens;
    case P_DECAYFROM: return p->decay_from;
    case P_SEMI: return p->semi;
    case P_FINE: return p->fine;
    case P_MONO: return p->mono;
    case P_MUTEGRP: return p->mute_group;
    }
    return 0;
}

static void pset(struct app *a, int id, float v) {
    struct pad_params *p = &cur(a)->p;
    float lo, hi, step;
    prange(id, &lo, &hi, &step);
    v = v < lo ? lo : v > hi ? hi : v;
    int iv = (int)lroundf(v);
    switch (id) {
    case P_START: p->start = fminf(v / 100, p->end - 0.001f); break;  // start stays before end
    case P_END: p->end = fmaxf(v / 100, p->start + 0.001f); break;
    case P_LOOP: p->loop = v / 100; break;
    case P_VOL: p->vol_db = v; break;
    case P_KITVOL: a->e->kit_db = v; break;
    case P_PAN: p->pan = iv / 50.0f; break;
    case P_ATTACK: p->attack = iv; break;
    case P_DECAY: *(p->note_on ? &p->release : &p->decay) = iv; break;
    case P_VELSENS: p->vel_sens = iv; break;
    case P_DECAYFROM: p->decay_from = iv; break;
    case P_SEMI: p->semi = iv; break;
    case P_FINE: p->fine = iv; break;
    case P_MONO: p->mono = iv; break;
    case P_MUTEGRP: p->mute_group = iv; break;
    }
}

static float norm(const struct app *a, int id) {
    float lo, hi, step;
    prange(id, &lo, &hi, &step);
    return (pget(a, id) - lo) / (hi - lo);
}

static void set_norm(struct app *a, int id, float n) {
    float lo, hi, step;
    prange(id, &lo, &hi, &step);
    pset(a, id, lo + n * (hi - lo));
}

static void step_param(struct app *a, int id, int steps) {
    float lo, hi, step;
    if (id == P_NONE || !cur(a)->smp.lr) return;
    prange(id, &lo, &hi, &step);
    pset(a, id, pget(a, id) + steps * step);
}

// Scaled takeover (the guide's default for K1-K3): the value moves the same way as the knob, at a rate that
// makes both reach the end of travel together, so it never jumps.
static float scaled(float v, int k_old, int k_new) {
    float ko = k_old / 127.0f, kn = k_new / 127.0f;
    if (kn > ko && ko < 1) return v + (kn - ko) * (1 - v) / (1 - ko);
    if (kn < ko && ko > 0) return v - (ko - kn) * v / ko;
    return v;
}

int page_param(const struct app *a, int k) {
    int g = a->group, s = a->page[g];
    int sk = PAGES[g][s].shift_k[k];
    return a->shift && sk ? sk : PAGES[g][s].k[k];
}

const char *page_name(const struct app *a, int g) { return PAGES[g][a->page[g]].name; }

const char *param_name(const struct app *a, int id) {
    static const char *names[] = {"", "Start", "End", "Loop", "Volume", "Kit Vol", "Pan", "Attack", "Decay",
                                  "Vel Sens", "Decay From", "Semi Tune", "Fine Tune", "Polyphony", "Mute Group"};
    if (id == P_DECAY && cur(a)->p.note_on) return "Release";
    return id >= 0 && id < (int)(sizeof names / sizeof names[0]) ? names[id] : "";
}

void param_format(const struct app *a, int id, char *buf, int n) {
    float v = pget(a, id);
    int iv = (int)lroundf(v);
    switch (id) {
    case P_NONE: snprintf(buf, n, "-"); break;
    case P_START: case P_END: case P_LOOP: snprintf(buf, n, "%.1f%%", v); break;
    case P_VOL: case P_KITVOL: v <= VOL_MIN_DB ? snprintf(buf, n, "-INF") : snprintf(buf, n, "%+.1fdB", v); break;
    case P_PAN: iv ? snprintf(buf, n, "%d%c", abs(iv), iv < 0 ? 'L' : 'R') : snprintf(buf, n, "C"); break;
    case P_DECAYFROM: snprintf(buf, n, iv ? "End" : "Start"); break;
    case P_SEMI: snprintf(buf, n, "%+d st", iv); break;
    case P_FINE: snprintf(buf, n, "%+d ct", iv); break;
    case P_MONO: snprintf(buf, n, iv ? "Mono" : "Poly"); break;
    case P_MUTEGRP: iv ? snprintf(buf, n, "%d", iv) : snprintf(buf, n, "Off"); break;
    default: snprintf(buf, n, "%d", iv); break;
    }
}

// ---- actions ------------------------------------------------------------------------------------------

static void retire(struct app *a, float *buf) {  // the UI thread may still be drawing it: it frees after its frame
    unsigned n = a->grave_n;
    if (n - __atomic_load_n(&a->grave_freed, __ATOMIC_ACQUIRE) >= GRAVE) return;  // shortcut: leaks if the UI is 64 edits behind
    a->grave[n % GRAVE] = buf;
    __atomic_store_n(&a->grave_n, n + 1, __ATOMIC_RELEASE);
}

static void erase_pad(struct app *a, int i) {
    struct pad *pd = &a->e->pads[a->bank][i];
    if (!pd->smp.lr) return;
    engine_kill(a->e, a->bank, i);  // the buffer goes away: no fade may read it
    retire(a, pd->smp.lr);
    memset(&pd->smp, 0, sizeof pd->smp);
    pad_defaults(&pd->p);
    pd->muted = 0;
    popup(a, "Erased %c%02d", 'A' + a->bank, i + 1);
}

static void trim_sample(struct app *a) {
    struct pad *pd = cur(a);
    struct pad_params *p = &pd->p;
    int frames;
    float *buf = pd->smp.lr ? sample_slice(&pd->smp, p->start, p->end, &frames) : NULL;
    if (!buf) { popup(a, "Nothing to trim"); return; }
    engine_kill(a->e, a->bank, a->pad);
    retire(a, pd->smp.lr);
    float len = p->end - p->start;
    p->loop = fminf(fmaxf((p->loop - p->start) / len, 0), 1);
    p->start = 0, p->end = 1;
    pd->smp.frames = frames;  // shorter first: the UI may pair the old buffer with the new length, never the reverse
    __atomic_store_n(&pd->smp.lr, buf, __ATOMIC_RELEASE);
    popup(a, "Trimmed: %.2f s", frames / (double)RATE);
}

static void normalize(struct app *a) {
    struct pad *pd = cur(a);
    float peak = pd->smp.lr ? sample_peak(&pd->smp, pd->p.start, pd->p.end) : 0;
    if (peak <= 0) { popup(a, "Nothing to normalize"); return; }
    sample_scale(&pd->smp, 1 / peak);
    popup(a, "Normalized %+.1f dB", -20 * log10f(peak));
}

static void pad_event(struct app *a, int i, int vel) {
    a->pressed[i] = vel;
    if (!vel) { engine_release(a->e, a->bank, i); return; }
    if (a->bank_held && i < BANKS) { a->bank = i, a->bank_used = 1, a->fader_engaged = 0; popup(a, "Bank %c", 'A' + i); return; }
    if (a->erase_held) { erase_pad(a, i); return; }
    if (a->shift) {
        if (i == 0) a->e->full_level = !a->e->full_level, popup(a, "Full Level %s", a->e->full_level ? "On" : "Off");
        else if (i == 12) trim_sample(a);
        else popup(a, "%s: later", SHIFT_PAD[i]);
        return;
    }
    if (a->mute_mode) {
        struct pad *pd = &a->e->pads[a->bank][i];
        a->mute_used = 1;
        if ((pd->muted = !pd->muted)) engine_stop(a->e, a->bank, i);
        return;
    }
    if (i != a->pad) a->fader_engaged = 0;
    a->pad = i;
    if (!a->sample_held) engine_trigger(a->e, a->bank, i, vel);
}

static void button(struct app *a, int note, int down) {
    struct pad_params *p = &cur(a)->p;
    switch (note) {
    case N_SHIFT: a->shift = down; return;
    case N_SAMPLE:
        a->sample_held = down;
        if (down && a->shift) popup(a, "Input Config: later");
        return;
    case N_PADBANK:
        if (down) { a->bank_held = 1, a->bank_used = 0; return; }
        a->bank_held = 0;
        if (!a->bank_used) a->bank = (a->bank + (a->shift ? BANKS - 1 : 1)) % BANKS, a->fader_engaged = 0, popup(a, "Bank %c", 'A' + a->bank);
        return;
    case N_ERASE:
        a->erase_held = down;
        if (down && a->shift) popup(a, "Copy: later");
        return;
    case N_MUTE:
        if (!down) {
            if (a->mute_held && a->mute_used) a->mute_mode = 0;  // held and used: it was a momentary mute
            a->mute_held = 0;
            return;
        }
        if (a->shift) {
            for (int i = 0; i < PADS; i++) a->e->pads[a->bank][i].muted = 0;
            popup(a, "Unmute All");
            return;
        }
        a->mute_mode = !a->mute_mode, a->mute_held = a->mute_mode, a->mute_used = 0;
        return;
    }
    if (!down) return;
    switch (note) {
    case N_CHOP:
        if (a->shift) p->note_on = !p->note_on, popup(a, "Note On %s", p->note_on ? "On" : "Off");
        else popup(a, "Chop: later");
        return;
    case N_LOOP:
        if (a->shift) p->reverse = !p->reverse, popup(a, "Reverse %s", p->reverse ? "On" : "Off");
        else p->loop_on = !p->loop_on, popup(a, "Loop %s", p->loop_on ? "On" : "Off");
        return;
    case N_B1: case N_B2: case N_B3: {
        int g = note - N_B1;
        if (a->shift) {
            if (g == 0) p->loop_lock = !p->loop_lock, popup(a, "Loop Lock %s", p->loop_lock ? "On" : "Off");
            if (g == 1) normalize(a);
            return;
        }
        if (a->group == g) a->page[g] = (a->page[g] + 1) % NPAGES[g];
        a->group = g, a->sel = 0;
        if (!PAGES[g][a->page[g]].k[0]) popup(a, "%s: later", PAGES[g][a->page[g]].name);
        return;
    }
    case N_ENCPUSH: a->sel = (a->sel + (a->shift ? 2 : 1)) % 3; return;
    case N_MINUS: case N_PLUS:
        if (a->shift) popup(a, note == N_MINUS ? "Undo: later" : "Redo: later");
        else step_param(a, page_param(a, a->sel), note == N_PLUS ? 1 : -1);
        return;
    case N_STOP: engine_stop_all(a->e); return;
    }
    const char *name = button_name(note);
    if (name) popup(a, "%s: later", name);
}

static void control(struct app *a, int cc, int v) {
    if (cc == CC_MAIN_VOLUME) {
        a->e->master = (v / 127.0f) * (v / 127.0f);  // squared: closer to perceived loudness
        popup(a, "Main Volume %d%%", v * 100 / 127);
    } else if (cc >= CC_K1 && cc <= CC_K3) {
        int k = cc - CC_K1, id = page_param(a, k);
        if (a->knob[k] >= 0 && id != P_NONE && cur(a)->smp.lr) {
            float lo, hi, step;
            prange(id, &lo, &hi, &step);
            if (fabsf(a->shadow[k] * (hi - lo) + lo - pget(a, id)) > step) a->shadow[k] = norm(a, id);  // resync
            a->shadow[k] = scaled(a->shadow[k], a->knob[k], v);
            set_norm(a, id, a->shadow[k]);
            a->sel = k;
        }
        a->knob[k] = v;
    } else if (cc == CC_FADER) {  // Pad Volume, Pickup takeover: follows only after crossing the current value
        float t = v / 127.0f, c = norm(a, P_VOL);
        if (!a->fader_engaged && a->fader >= 0 && (a->fader / 127.0f - c) * (t - c) <= 0) a->fader_engaged = 1;
        if (a->fader_engaged && cur(a)->smp.lr) set_norm(a, P_VOL, t);
        a->fader = v;
    } else if (cc == CC_ENCODER) {  // relative, as the stock parser reads it: 1..64 = +n, 65..127 = -63..-1
        step_param(a, page_param(a, a->sel), v > 64 ? v - 128 : v);
    }
}

void app_midi(struct app *a, const unsigned char *m) {
    int st = m[0] & 0xF0, ch = (m[0] & 0x0F) + 1, i;
    if (ch == 10 && (st == 0x90 || st == 0x80) && (i = pad_index(m[1])) >= 0) pad_event(a, i, st == 0x90 ? m[2] : 0);
    else if (ch == 1 && (st == 0x90 || st == 0x80)) button(a, m[1], st == 0x90 && m[2] > 0);
    else if (ch == 1 && st == 0xB0) control(a, m[1], m[2]);
    else return;  // aftertouch and the rest change nothing on screen
    __atomic_store_n(&a->redraw, 1, __ATOMIC_RELEASE);
}

// ---- LEDs ---------------------------------------------------------------------------------------------

static const unsigned char BICOLOUR[] = {6, 7, 1, 0, 9, 75, 43, 74, 40, 36, 53};

static int led_value(int note, int level) {  // level 0 off, 1 dim (available), 2 bright (active)
    for (size_t i = 0; i < sizeof BICOLOUR; i++)
        if (BICOLOUR[i] == note) return level == 2 ? 8 : level == 1 ? 4 : 0;  // 3*A+B: both colours equal
    return level;  // single LEDs: 0 off, 1 dim, 2 full
}

static int led_level(const struct app *a, int note) {
    const struct pad_params *p = &cur(a)->p;
    switch (note) {
    case N_SAMPLE: return 2;
    case N_SHIFT: return a->shift ? 2 : 1;
    case N_PADBANK: return a->bank_held ? 2 : 1;
    case N_ERASE: return a->erase_held ? 2 : 1;
    case N_MUTE: return a->mute_mode ? 2 : 1;
    case N_CHOP: return a->shift ? (p->note_on ? 2 : 1) : 0;  // Chop itself comes later
    case N_LOOP: return a->shift ? (p->reverse ? 2 : 1) : (p->loop_on ? 2 : 1);
    case N_MINUS: case N_PLUS: case N_STOP: return 1;
    }
    return 0;
}

static void pad_colour(const struct app *a, int i, unsigned char c[3]) {
    const struct pad *pd = &a->e->pads[a->bank][i];
    int r = 0, g = 0, b = 0;
    if (a->bank_held && i < BANKS) g = b = i == a->bank ? 64 : 16;  // bank selector
    else if (a->pressed[i]) r = a->pressed[i], b = 127 - a->pressed[i];
    else if (!pd->smp.lr) ;
    else if (a->mute_mode) r = 64, g = pd->muted ? 0 : 20;  // muted red, unmuted yellow (green die is ~3x brighter)
    else r = g = b = i == a->pad ? 48 : 12;
    c[0] = r, c[1] = g, c[2] = b;
}

static void emit(struct app *a, const unsigned char *m, int n) {
    if (a->out_len + n <= (int)sizeof a->out) memcpy(a->out + a->out_len, m, n), a->out_len += n;
}

void app_tick(struct app *a, int frames) {
    a->now += frames;
    if (a->popup[0] && (int)(a->now - a->popup_until) > 0) a->popup[0] = 0, __atomic_store_n(&a->redraw, 1, __ATOMIC_RELEASE);
    for (size_t i = 0; i < NBUTTONS; i++) {
        int note = BUTTONS[i].note, v = led_value(note, led_level(a, note));
        if (!a->leds_valid || a->led_sent[note] != v) {
            unsigned char m[3] = {0xB0, note, v};
            emit(a, m, 3), a->led_sent[note] = v;
        }
    }
    for (int i = 0; i < BANKS; i++) {  // assumed bank A-H indicators, see docs/control-surface-protocol.md
        int v = i == a->bank ? 2 : 0;
        if (!a->leds_valid || a->led_sent[CC_BANK_LED + i] != v) {
            unsigned char m[3] = {0xB0, CC_BANK_LED + i, v};
            emit(a, m, 3), a->led_sent[CC_BANK_LED + i] = v;
        }
    }
    for (int i = 0; i < PADS; i++) {
        unsigned char c[3], m[12];
        pad_colour(a, i, c);
        if (a->leds_valid && !memcmp(c, a->pad_sent[i], 3)) continue;
        pad_color_msg(m, i, c[0], c[1], c[2]);
        emit(a, m, 12), memcpy(a->pad_sent[i], c, 3);
    }
    a->leds_valid = 1;
}
