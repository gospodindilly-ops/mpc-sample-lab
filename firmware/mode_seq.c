// Sequence Mode (MPC Sample User Guide, "Sequence Mode"): pads pick sequences (banks A..H of 16) or, while recording
// is armed or with SEQ + SAMPLE, play the selected track; B1 held sets the tempo with the encoder, B2 switches it
// between the sequence's own and the global one, B3 turns Rec Quantize on and off; the encoder moves the playhead.
// MUTE here is Track Mute (MPC 3): the pads are the 16 tracks.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "ui.h"

static void pad(struct app *a, int i, int vel) {
    struct sequencer *s = a->sq;
    int n = a->bank * PADS + i;
    if (a->seq_pads) { app_track_mode(a)->pad(a, i, vel); return; }
    if (!vel) return;
    if (a->erase_held) {  // asks first, as the piano roll does: a whole sequence is a lot to lose to a slip
        if (seq_used(s, n)) a->confirm = n;
        else app_popup(a, "Seq %c%02d is empty", 'A' + a->bank, i + 1);
        return;
    }
    if (s->playing) s->next = n == s->cur ? -1 : n;  // takes over at the end of the current one
    else s->cur = n, s->next = -1, seq_locate(s, 0);
}

static int button(struct app *a, int note, int down) {
    struct sequencer *s = a->sq;
    if (a->confirm >= 0 && (note == N_B1 || note == N_B3)) {  // B1 Cancel, B3 Do It!
        if (down && note == N_B3) seq_clear(s, a->confirm), app_popup(a, "Erased Seq %c%02d", 'A' + a->confirm / PADS, a->confirm % PADS + 1);
        if (down) a->confirm = -1;
        return 1;
    }
    if (a->shift || note < N_B1 || note > N_B3) return a->seq_pads && down ? app_track_mode(a)->button(a, note, down) : 0;
    if (note == N_B1) {  // the guide's Sequence BPM window: open while B1 is held
        if ((a->bpm_edit = down)) app_popup(a, "Tempo %.2f", seq_bpm(s));
    } else if (down && note == N_B2) {
        s->bpm_global = !s->bpm_global;
    } else if (down) {
        s->rec_quant = !s->rec_quant;
    }
    return 1;
}

static int control(struct app *a, int cc, int v) {  // the encoder moves the playhead by beats (SHIFT: by Q)
    struct sequencer *s = a->sq;
    if (cc != CC_ENCODER) return 0;
    seq_locate(s, s->pos + (v > 64 ? v - 128 : v) * (a->shift ? Q_TICKS[s->q] : PPQ * 4 / SIGS[s->sig][1]));
    return 1;
}

static int led(const struct app *a, int note) {
    int flash = 1 + (a->now / (RATE / 4) & 1);
    if (note == N_SEQ) return a->seq_pads ? flash : 2;
    if (note == N_SAMPLE) return a->seq_pads ? flash : 1;  // both flash in the guide's Sample/Seq Mode
    if (note == N_CHOP || note == N_LOOP) return a->seq_pads ? app_track_mode(a)->led(a, note) : 0;
    return -1;
}

static void colour(const struct app *a, int i, unsigned char c[3]) {  // the guide's colours: green for sequences
    const struct sequencer *s = a->sq;
    int n = a->bank * PADS + i, flash = a->now / (RATE / 4) & 1;
    if (a->seq_pads) { app_track_mode(a)->colour(a, i, c); return; }
    if (n == s->next || (n == s->cur && !s->playing)) c[1] = flash ? 64 : 8;  // queued: flashing
    else c[1] = n == s->cur ? 64 : seq_used(s, n) ? 20 : 3;  // playing bright, with events light, empty dim
}

static void transport(const struct sequencer *s, uint32_t *fb, int stride, int x, int y) {  // 17x17: stop, play, record
    if (s->recording) {
        for (int r = -8; r <= 8; r++) ui_fill(fb, stride, x + 8 - (int)sqrtf(72 - r * r), y + 8 + r, 2 * (int)sqrtf(72 - r * r) + 1, 1, C_RED);
    } else if (s->playing) {
        for (int r = 0; r < 17; r++) ui_fill(fb, stride, x + 2, y + r, 1 + (r < 8 ? r : 16 - r) * 2, 1, C_TEXT);
    } else {
        ui_fill(fb, stride, x, y, 17, 17, C_TEXT);
    }
}

static void draw(const struct app *a, uint32_t *fb, int stride) {
    const struct sequencer *s = a->sq;
    char buf[32];
    if (a->mute_mode && !a->seq_pads) { ui_tracks(a, fb, stride, 1); return; }
    if (a->confirm >= 0) {
        // the question is drawn last, over everything
    } else if (a->repeat) {  // Note Repeat on: B1..B3 pick its division, as everywhere
        struct tab t[3];
        ui_page_tabs(a, t), ui_tabs(fb, stride, t);
    } else {
        float bpm = seq_bpm(s);
        snprintf(buf, sizeof buf, "%d", (int)bpm);
        int x = ui_text(fb, stride, 39, 18, buf, 1, C_TEXT, UI_W);
        snprintf(buf, sizeof buf, "%02d", (int)lroundf(bpm * 100) % 100);  // hundredths raised, as the guide
        ui_text(fb, stride, x + 1, 13, buf, 0, C_TEXT, UI_W);
        ui_frame(fb, stride, 116, 13, 86, 25, C_DIM);
        ui_text(fb, stride, 127, 18, s->bpm_global ? "BPM: GBL" : "BPM: SEQ", 1, C_TEXT, UI_W);
        if (s->rec_quant) {
            ui_fill(fb, stride, 220, 13, 87, 25, C_TEXT);
            ui_text(fb, stride, ui_text(fb, stride, 228, 18, "Rec ", 1, C_RED, UI_W), 18, "Quant", 1, C_BG, UI_W);
        } else {
            ui_frame(fb, stride, 220, 13, 87, 25, C_DIM);
            ui_text(fb, stride, 228, 18, "Rec Quant", 1, C_DIM, UI_W);
        }
    }
    snprintf(buf, sizeof buf, "Seq %c%02d", 'A' + s->cur / PADS, s->cur % PADS + 1);
    ui_text(fb, stride, 20, 52, buf, 2, C_YELLOW, UI_W);
    if (s->next >= 0) snprintf(buf, sizeof buf, "Next %c%02d", 'A' + s->next / PADS, s->next % PADS + 1), ui_text(fb, stride, 150, 62, buf, 1, C_YELLOW, UI_W);
    snprintf(buf, sizeof buf, "%d/%d", SIGS[s->sig][0], SIGS[s->sig][1]);
    ui_text(fb, stride, 20, 89, buf, 2, C_YELLOW, UI_W);
    static const char *const METRO[3] = {"Off", "On", "Rec"};
    ui_icon(fb, stride, 128, 97, ICON_METRO, 0, C_TEXT);
    ui_text(fb, stride, 146, 96, METRO[s->metro], 1, C_TEXT, UI_W);
    uint32_t ci = s->count_in ? C_RED : C_DIM;  // the guide's count-in sign: digits growing to the bar's downbeat
    ui_text(fb, stride, 244, 104, "1", 0, ci, UI_W), ui_text(fb, stride, 251, 104, "2", 0, ci, UI_W);
    ui_text(fb, stride, 259, 98, "3", 1, ci, UI_W), ui_text(fb, stride, 269, 86, "4", 2, ci, UI_W);

    ui_fill(fb, stride, 20, 133, 283, 50, C_LINE);  // position: bar . beat and ticks, or the count-in
    transport(s, fb, stride, 36, 149);
    int beat = PPQ * 4 / SIGS[s->sig][1], bar = seq_bar(s), p = (int)s->pos;
    if (s->count > 0) {
        snprintf(buf, sizeof buf, "Count %d", (int)((bar - s->count) / beat) + 1);
        ui_text(fb, stride, 104, 145, buf, 2, C_TEXT, UI_W);
    } else {  // as the guide: bar, a dot, beat, and the ticks small at the right
        snprintf(buf, sizeof buf, "%03d", p / bar + 1), ui_text(fb, stride, 120, 145, buf, 2, C_TEXT, UI_W);
        ui_text(fb, stride, 172, 145, ".", 2, C_TEXT, UI_W);
        snprintf(buf, sizeof buf, "%02d", p % bar / beat + 1), ui_text(fb, stride, 196, 145, buf, 2, C_TEXT, UI_W);
        snprintf(buf, sizeof buf, "%03d", p % beat), ui_text(fb, stride, 263, 156, buf, 1, C_TEXT, UI_W);
    }

    if (a->confirm >= 0) snprintf(buf, sizeof buf, "Erase Seq %c%02d?", 'A' + a->confirm / PADS, a->confirm % PADS + 1), ui_confirm(fb, stride, buf);
    ui_knobs(a, fb, stride, (struct cell[3]){{C_TEXT, -1, 1}, {C_TEXT, -1, 1}, {C_TEXT, -1, 0}});  // values, as the guide
    ui_shift_icon(fb, stride, a->shift);
}

const struct mode SEQ_MODE = {
    .pages = {{{"Seq", {P_SEQBARS, P_SEQQ, P_SWING}, {P_SEQBARS1, P_SIG, P_METROVOL}}}},
    .npages = {1, 0, 0},
    .pad = pad, .button = button, .led = led, .colour = colour, .draw = draw, .control = control,
};
