// MIDI track (MPC 3 MIDI track; port and channel as in the MPC Sample guide's MIDI Configuration): pads send notes
// out of the track's port and channel. The screen shows the bank's pads as they sit on the panel, with their notes.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "ui.h"

static int note_of(const struct app *a, int i) { return midi_note(a->bank * PADS + i); }

static void note_name(int n, char *buf, int len) {  // MPC naming: note 60 is C3
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    snprintf(buf, len, "%s%d", N[n % 12], n / 12 - 2);
}

static void off(struct app *a, int i) {  // on the port and channel the note went out on, even if they changed since
    int h = a->note_out[i];
    if (h >= 0) app_midi_out(a, h >> 12, 0x80 | (h >> 8 & 15), h & 127, 0), a->note_out[i] = -1;
}

static void pad(struct app *a, int i, int vel) {
    const struct track *t = &a->e->tracks[a->track];
    off(a, i);
    if (!vel) { app_released(a, i); return; }
    a->pad = i;
    if (a->sample_held) return;  // SAMPLE + pad: select without sound
    int n = note_of(a, i), ch = t->midi_ch - 1;
    app_midi_out(a, t->midi_port, 0x90 | ch, n, a->e->full_level ? 127 : vel);
    a->note_out[i] = n | ch << 8 | t->midi_port << 12;
    app_played(a, i, vel);
}

static int button(struct app *a, int note, int down) { (void)a, (void)note, (void)down; return 0; }

static int led(const struct app *a, int note) {
    (void)a;
    return note == N_SAMPLE ? 2 : note == N_CHOP || note == N_LOOP ? 0 : -1;  // no Chop or Loop on MIDI notes
}

static void colour(const struct app *a, int i, unsigned char c[3]) { c[1] = i == a->pad ? 32 : 6, c[2] = i == a->pad ? 64 : 16; }

static void draw(const struct app *a, uint32_t *fb, int stride) {
    const struct track *t = &a->e->tracks[a->track];
    struct tab tb[3];
    char buf[32];
    ui_page_tabs(a, tb);
    ui_tabs(fb, stride, tb);
    snprintf(buf, sizeof buf, "%c%02d", 'A' + a->bank, a->pad + 1);
    ui_text(fb, stride, 16, 55, buf, 1, C_YELLOW, UI_W);
    snprintf(buf, sizeof buf, "T%d MIDI %s Ch %d", a->track + 1, PARAMS[P_MIDIPORT].list[t->midi_port], t->midi_ch);
    ui_text_fit(fb, stride, 56, 55, buf, 1, C_YELLOW, UI_W - 8);
    for (int i = 0; i < PADS; i++) {  // pad 1 bottom left, as on the panel
        int x = 8 + i % 4 * 77, y = 80 + (3 - i / 4) * 29, on = a->pressed[i] > 0;
        uint32_t fg = on ? C_BG : i == a->pad ? C_YELLOW : C_TEXT;
        if (on) ui_fill(fb, stride, x, y, 73, 26, C_TEXT);
        else ui_frame(fb, stride, x, y, 73, 26, i == a->pad ? C_YELLOW : C_LINE);
        snprintf(buf, sizeof buf, "%c%02d", 'A' + a->bank, i + 1);
        ui_text(fb, stride, x + 4, y + 7, buf, 0, on ? C_BG : C_DIM, x + 73);
        note_name(note_of(a, i), buf, sizeof buf);
        ui_text(fb, stride, x + 68 - (int)strlen(buf) * 8, y + 5, buf, 1, fg, x + 73);
    }
    ui_knobs(a, fb, stride, (struct cell[3]){{C_TEXT, -1, 0}, {C_TEXT, -1, 0}, {C_TEXT, -1, 0}});
}

const struct mode MIDI_MODE = {
    .pages = {{{"MIDI", {P_MIDIPORT, P_MIDICH, P_NONE}, {0}}}},
    .npages = {1, 0, 0},
    .pad = pad, .button = button, .led = led, .colour = colour, .draw = draw,
};
