// Sample Mode (MPC Sample User Guide, "Sample Mode"): a pad plays its sample and selects it, SAMPLE + pad selects
// without sound, B1..B3 cycle the pages; the screen is the guide's: tabs, pad header, stereo waveform, knob row.
#include <stdio.h>
#include "app.h"
#include "ui.h"

static void erase_pad(struct app *a, int i) {
    struct pad *pd = &a->e->tracks[a->track].pads[a->bank][i];
    if (!pd->smp) return;
    app_swap_sample(a, a->track, a->bank, i, NULL);
    pad_defaults(&pd->p);
    pd->muted = 0;
    app_popup(a, "Erased %c%02d", 'A' + a->bank, i + 1);
}

static void pad(struct app *a, int i, int vel) {
    if (!vel) { engine_release(a->e, a->track, a->bank, i), app_released(a, i); return; }
    if (a->erase_held) {  // the guide: stopped, ERASE + pad erases the sample; playing, it erases the pad's events
        if (!a->sq->playing) erase_pad(a, i);
        return;
    }
    if (i != a->pad) a->fader_engaged = 0;
    a->pad = i;
    if (!a->sample_held) engine_trigger(a->e, a->track, a->bank, i, vel), app_played(a, i, vel);
}

static int button(struct app *a, int note, int down) {
    struct pad_params *p = &app_pad(a)->p;
    if (!down) return 0;
    switch (note) {
    case N_CHOP:
        if (a->shift) p->note_on = !p->note_on, app_popup(a, "Note On %s", p->note_on ? "On" : "Off");
        else app_popup(a, "Chop: later");
        return 1;
    case N_LOOP:
        if (a->shift) p->reverse = !p->reverse, app_popup(a, "Reverse %s", p->reverse ? "On" : "Off");
        else p->loop_on = !p->loop_on, app_popup(a, "Loop %s", p->loop_on ? "On" : "Off");
        return 1;
    case N_B1:
        if (!a->shift) return 0;
        p->loop_lock = !p->loop_lock, app_popup(a, "Loop Lock %s", p->loop_lock ? "On" : "Off");
        return 1;
    case N_B2:
        if (!a->shift) return 0;
        app_edit_sample(a, JOB_NORMALIZE);
        return 1;
    }
    return 0;
}

static int led(const struct app *a, int note) {
    const struct pad_params *p = &app_pad(a)->p;
    switch (note) {
    case N_SAMPLE: return 2;
    case N_CHOP: return a->shift ? (p->note_on ? 2 : 1) : 0;  // Chop itself comes later
    case N_LOOP: return a->shift ? (p->reverse ? 2 : 1) : (p->loop_on ? 2 : 1);
    }
    return -1;
}

static void colour(const struct app *a, int i, unsigned char c[3]) {
    if (a->e->tracks[a->track].pads[a->bank][i].smp) c[0] = c[1] = c[2] = i == a->pad ? 48 : 12;
}

static void draw(const struct app *a, uint32_t *fb, int stride) {
    const struct pad *pd = app_pad(a);
    const struct sample *smp = __atomic_load_n(&pd->smp, __ATOMIC_ACQUIRE);  // read once: the audio thread may swap it
    const struct pad_params *p = &pd->p;
    struct tab t[3];
    ui_page_tabs(a, t);
    if (a->shift) t[0] = (struct tab){"Loop Lock", p->loop_lock, 0, 0}, t[1] = (struct tab){"Normalize", 0, 0, 0};
    ui_tabs(fb, stride, t);

    char buf[16];  // header: pad and sample name; Note On, Reverse, Loop, 16 Levels
    snprintf(buf, sizeof buf, "%c%02d", 'A' + a->bank, a->pad + 1);
    ui_text(fb, stride, 16, 55, buf, 1, C_YELLOW, UI_W);
    ui_text_fit(fb, stride, 56, 55, smp ? smp->name : "", 1, C_YELLOW, 198);
    ui_icon(fb, stride, 201, 52, ICON_NOTE, p->note_on, C_TEXT);
    ui_icon(fb, stride, 228, 52, ICON_REVERSE, p->reverse, C_TEXT);
    ui_icon(fb, stride, 256, 52, ICON_LOOP, p->loop_on, C_BLUE);
    ui_icon(fb, stride, 283, 52, ICON_16, 0, C_TEXT);

    if (smp) ui_wave(fb, stride, pd, smp, page_param(a, 0) == P_ATTACK);
    else ui_text(fb, stride, 124, 130, "Empty pad", 1, C_DIM, UI_W);

    struct cell c[3];  // envelope stages in the colours they have over the waveform; Loop in blue with its lock
    for (int k = 0; k < 3; k++) {
        int id = page_param(a, k);
        c[k] = (struct cell){id == P_ATTACK ? C_ORANGE : id == P_DECAY || id == P_RELEASE ? C_TEAL : id == P_LOOP ? C_BLUE : C_TEXT,
                             id == P_LOOP && p->loop_lock ? ICON_LOCK : -1, 0};
    }
    ui_knobs(a, fb, stride, c);
    ui_shift_icon(fb, stride, a->shift);
}

const struct mode SAMPLE_MODE = {
    .pages = {{{"Trim", {P_START, P_END, P_LOOP}, {0}},
               {"Mix", {P_VOL, P_NONE, P_PAN}, {P_NONE, P_KITVOL, P_NONE}},
               {"Amp Env", {P_ATTACK, P_DECAY, P_VELSENS}, {P_NONE, P_DECAYFROM, P_NONE}}},
              {{"Tune", {P_SEMI, P_FINE, P_NONE}, {0}}, {"Play", {P_MONO, P_MUTEGRP, P_NONE}, {0}}},
              {{"Filter", {0}, {0}}, {"Filt Env", {0}, {0}}}},  // phase 2: empty pages say "later"
    .npages = {3, 2, 2},
    .pad = pad, .button = button, .led = led, .colour = colour, .draw = draw,
};
