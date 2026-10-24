// Audio track (MPC 3 Audio track): one recording placed on the sequence's timeline. Recording into it comes with
// Sample Record and playing it with the sequencer (roadmap step 5); until then the screen says so and K1 sets the
// track volume. Pads play nothing on an audio track, as in MPC 3.
#include <stdio.h>
#include "app.h"
#include "ui.h"

static void pad(struct app *a, int i, int vel) { if (vel) a->pad = i; }
static int button(struct app *a, int note, int down) { (void)a, (void)note, (void)down; return 0; }
static int led(const struct app *a, int note) { (void)a; return note == N_SAMPLE ? 2 : note == N_CHOP || note == N_LOOP ? 0 : -1; }
static void colour(const struct app *a, int i, unsigned char c[3]) { (void)a, (void)i, (void)c; }  // dark: pads do nothing

static void draw(const struct app *a, uint32_t *fb, int stride) {
    struct tab tb[3];
    char buf[24];
    ui_page_tabs(a, tb);
    ui_tabs(fb, stride, tb);
    snprintf(buf, sizeof buf, "T%d Audio", a->track + 1);
    ui_text(fb, stride, 16, 55, buf, 1, C_YELLOW, UI_W);
    ui_frame(fb, stride, 17, 90, 286, 47, C_DIM), ui_frame(fb, stride, 17, 139, 286, 47, C_DIM);  // where the recording will show
    ui_text(fb, stride, 112, 106, "No recording", 1, C_DIM, UI_W);
    ui_text(fb, stride, 82, 157, "Records with Sample Record", 0, C_DIM, UI_W);
    ui_knobs(a, fb, stride, (struct cell[3]){{C_TEXT, -1, 0}, {C_TEXT, -1, 0}, {C_TEXT, -1, 0}});
}

const struct mode AUDIO_MODE = {
    .pages = {{{"Audio", {P_TRKVOL, P_NONE, P_NONE}, {0}}}},
    .npages = {1, 0, 0},
    .pad = pad, .button = button, .led = led, .colour = colour, .draw = draw,
};
