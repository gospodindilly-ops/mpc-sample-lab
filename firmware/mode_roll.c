// Piano roll: the guide's Step Edit (SHIFT + SEQ) drawn as MPC 3's Grid. Rows are the current bank's 16 pads (notes
// on a MIDI track), pad 1 at the bottom; time runs right in Q steps of the track's own length. Controls are the guide's
// Step Edit ones: the encoder moves the cursor step (SHIFT: a bar), -/+ step through the notes on it, a pad selects
// its note there (or, ours, puts one there), the fader nudges it, K3 sets its velocity, B3 erases it, ERASE + pad
// erases all of a pad's notes after asking, B1 held sets the tempo. Ours on top: B2 zoom, SHIFT + K3 note length.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "ui.h"

#define GX 30  // the grid: left edge and width; 16 rows of RH from GY
#define GW 284
#define GY 53
#define RH 9

static const char *const ZOOM[4] = {"1 Bar", "2 Bars", "4 Bars", "All"};

static long qt(const struct app *a) { return Q_TICKS[a->sq->q]; }
static long tlen(const struct app *a) { return seq_track_len(a->sq, a->sq->cur, a->track); }
static int32_t head(const struct app *a) { return a->sq->seqs[a->sq->cur].trk[a->track].head; }
static int key(const struct app *a, int i) { return a->bank * PADS + i; }
static int32_t sel(const struct app *a) { return seq_holds(a->sq, a->sq->cur, a->track, a->roll_sel) ? a->roll_sel : -1; }

static long playing(const struct app *a) {  // the playhead in the track's own ticks; -1 while stopped or counting in
    const struct sequencer *s = a->sq;
    return s->playing && !s->count ? (long)fmod(s->pos, tlen(a)) : -1;
}

static void pick(struct app *a) {  // the selected row's note on the cursor step, if there is one
    a->roll_sel = seq_at(a->sq, a->sq->cur, a->track, a->roll_tick, a->roll_tick + qt(a), key(a, a->pad));
}

static void pad(struct app *a, int i, int vel) {
    struct sequencer *s = a->sq;
    int type = a->e->tracks[a->track].type;
    if (vel && a->erase_held) { a->confirm = key(a, i); return; }  // asks first, as the guide's Step Edit
    app_track_mode(a)->pad(a, i, vel);  // it sounds, and records like anywhere while recording
    if (!vel || s->recording || a->sample_held || (type != TRACK_DRUM && type != TRACK_MIDI)) return;
    a->pad = i;
    pick(a);
    if (a->roll_sel < 0 && (a->roll_sel = seq_add(s, s->cur, a->track, a->roll_tick, key(a, i), vel)) >= 0)
        s->ev[a->roll_sel].len = qt(a), s->ev[a->roll_sel].take = 0;  // step input, not a take: Undo leaves it
}

static void step_notes(struct app *a, int dir) {  // the guide's -/+: the next pad with a note on the cursor step
    const struct sequencer *s = a->sq;
    unsigned here = 0;
    for (int32_t i = head(a); i >= 0 && s->ev[i].tick < a->roll_tick + qt(a); i = s->ev[i].next)
        if (s->ev[i].tick >= a->roll_tick && s->ev[i].key / PADS == a->bank) here |= 1u << s->ev[i].key % PADS;
    for (int k = 1; k <= PADS; k++) {
        int p = ((a->pad + dir * k) % PADS + PADS) % PADS;
        if (here >> p & 1) { a->pad = p, pick(a); return; }
    }
}

static int button(struct app *a, int note, int down) {
    struct sequencer *s = a->sq;
    if (a->confirm >= 0 && (note == N_B1 || note == N_B3)) {  // the question ERASE + pad asked: B1 Cancel, B3 Do It!
        if (down && note == N_B3) app_popup(a, "Erased %d notes", seq_erase_key(s, s->cur, a->track, a->confirm));
        if (down) a->confirm = -1;
        return 1;
    }
    if (a->shift) return 0;  // SHIFT + -: Undo, as everywhere
    int32_t e = sel(a);
    switch (note) {
    case N_B1: if ((a->bpm_edit = down)) app_popup(a, "Tempo %.2f", seq_bpm(s)); return 1;
    case N_B2: if (down) a->roll_zoom = (a->roll_zoom + 1) % 4; return 1;
    case N_B3: if (down && e >= 0) seq_erase(s, s->cur, a->track, e), a->roll_sel = -1; return 1;
    case N_MINUS: case N_PLUS: if (down) step_notes(a, note == N_PLUS ? 1 : -1); return 1;
    }
    return 0;
}

static int control(struct app *a, int cc, int v) {
    struct sequencer *s = a->sq;
    long len = tlen(a);
    if (cc == CC_ENCODER) {  // the cursor, a step (SHIFT: a bar) at a time, round the track's own length
        long step = a->shift ? seq_bar(s) : qt(a), t = (a->roll_tick / step + (v > 64 ? v - 128 : v)) * step % len;
        a->roll_tick = (uint32_t)(t < 0 ? t + len : t);
        pick(a);
        return 1;
    }
    int32_t e = sel(a), prev = a->fader;  // the fader nudges the selected note, 4 ticks a step (MPC 3's Don't Snap)
    a->fader = v;
    if (e >= 0 && prev >= 0) {
        long t = (long)s->ev[e].tick + (v - prev) * 4;
        seq_move(s, s->cur, a->track, e, (uint32_t)(t < 0 ? 0 : t >= len ? len - 1 : t));
    }
    return 1;
}

static int led(const struct app *a, int note) {
    (void)a;
    return note == N_SEQ ? 2 : note == N_SAMPLE ? 1 : note == N_CHOP || note == N_LOOP ? 0 : -1;
}

// Pads show the grid's column on the playing step (the cursor's while stopped): its notes by velocity, yellow to red
// (the MPC's Classic Velocity), pads with notes elsewhere dim, the selected pad white; playing, they chase the beat.
// One walk of the track serves all 16: the LED refresh asks for pad 0 first (a held pad 0 skips it; then the others
// use the last walk for one refresh).
static void colour(const struct app *a, int i, unsigned char c[3]) {
    static int here[PADS], used[PADS];
    const struct sequencer *s = a->sq;
    if (i == 0) {
        long p = playing(a), from = p >= 0 ? p / qt(a) * qt(a) : (long)a->roll_tick;
        memset(here, 0, sizeof here), memset(used, 0, sizeof used);
        for (int32_t e = head(a); e >= 0; e = s->ev[e].next) {
            if (s->ev[e].key / PADS != a->bank) continue;
            used[s->ev[e].key % PADS] = 1;
            if ((long)s->ev[e].tick >= from && (long)s->ev[e].tick < from + qt(a)) here[s->ev[e].key % PADS] = s->ev[e].vel;
        }
    }
    if (i == a->pad && !(here[i] && playing(a) >= 0)) c[0] = c[1] = c[2] = 48;
    else if (here[i]) c[0] = 64, c[1] = 40 - here[i] * 40 / 127;
    else if (used[i]) c[0] = c[1] = c[2] = 6;
}

static void row_label(const struct track *tr, int k, char *buf, int n) {  // a pad (A01) or, on a MIDI track, a note (C1)
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (tr->type == TRACK_MIDI) snprintf(buf, n, "%s%d", N[midi_note(k) % 12], midi_note(k) / 12 - 2);
    else snprintf(buf, n, "%c%02d", 'A' + k / PADS, k % PADS + 1);
}

static uint32_t vel_colour(int vel) {  // C_YELLOW at velocity 1 to C_RED at 127
    int r = 0xf0 + (0xe0 - 0xf0) * vel / 127, g = 0xa8 + (0x24 - 0xa8) * vel / 127, b = 0x28 + (0x5e - 0x28) * vel / 127;
    return (uint32_t)(r << 16 | g << 8 | b);
}

static void draw(const struct app *a, uint32_t *fb, int stride) {
    const struct sequencer *s = a->sq;
    const struct track *tr = &a->e->tracks[a->track];
    char buf[48];
    long len = tlen(a), q = qt(a), bar = seq_bar(s), beat = PPQ * 4 / SIGS[s->sig][1];
    long span = a->roll_zoom < 3 ? (1L << a->roll_zoom) * bar : len;
    long play = playing(a), step = play >= 0 ? play / q * q : -1;  // the playhead and the step it is on
    long view = (play >= 0 ? play : (long)a->roll_tick) / span * span;  // the page with the playhead, else the cursor
    int32_t e = sel(a);
    int bottom = GY + PADS * RH - 2;
#define X(t) (GX + (int)(((t) - view) * GW / span))

    struct tab t[3];
    if (a->confirm >= 0) {
        // the question is drawn last, over the grid
    } else if (a->repeat) {
        ui_page_tabs(a, t), ui_tabs(fb, stride, t);
    } else {  // as the guide's Step Edit: tempo left, Erase right; the zoom ours
        float bpm = seq_bpm(s);
        snprintf(buf, sizeof buf, "%d", (int)bpm);
        int x = ui_text(fb, stride, 39, 18, buf, 1, C_TEXT, UI_W);
        snprintf(buf, sizeof buf, "%02d", (int)lroundf(bpm * 100) % 100), ui_text(fb, stride, x + 1, 13, buf, 0, C_TEXT, UI_W);
        ui_frame(fb, stride, 116, 13, 86, 25, C_DIM);
        ui_text(fb, stride, 159 - (int)strlen(ZOOM[a->roll_zoom]) * 4, 18, ZOOM[a->roll_zoom], 1, C_TEXT, UI_W);
        ui_text(fb, stride, 243, 18, "Erase", 1, e >= 0 ? C_RED : C_DIM, UI_W);
    }
    snprintf(buf, sizeof buf, "Seq %c%02d  T%d %s", 'A' + s->cur / PADS, s->cur % PADS + 1, a->track + 1,
             PARAMS[P_TTYPE].list[tr->type <= TRACK_MIDI ? tr->type : 0]);
    ui_text(fb, stride, 4, 38, buf, 0, C_YELLOW, UI_W);
    long at = step >= 0 ? step : (long)a->roll_tick;  // where we are: the playing step, else the cursor's
    snprintf(buf, sizeof buf, "Step %ld/%ld %03ld.%02ld", at / q + 1, (len + q - 1) / q, at / bar + 1, at % bar / beat + 1);
    ui_text(fb, stride, UI_W - 4 - (int)strlen(buf) * 6, 38, buf, 0, step >= 0 ? C_TEAL : C_TEXT, UI_W);

    if (view + span > len) ui_fill(fb, stride, X(len), GY, GX + GW - X(len), bottom - GY + 1, 0x141414);  // past its length
    for (int r = 0; r < PADS; r++) {  // rows, pad 1 at the bottom; the selected one lit
        int y = GY + (PADS - 1 - r) * RH, k = key(a, r), n = midi_note(k) % 12, black = n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
        if (r == a->pad) ui_fill(fb, stride, 0, y, UI_W, RH - 1, 0x1c1c1c);
        else if (tr->type == TRACK_MIDI && black) ui_fill(fb, stride, GX, y, GW, RH - 1, 0x0c0c0c);  // a keyboard's black keys
        const struct pad *pd = &tr->pads[a->bank][r];
        row_label(tr, k, buf, sizeof buf);
        ui_tiny(fb, stride, 4, y + 2, buf, tr->type == TRACK_MIDI ? (n == 0 ? C_TEXT : C_DIM) : pd->smp && !pd->muted ? C_TEXT : C_DIM);
    }
    if (step >= view && step < view + span) ui_fill(fb, stride, X(step) + 1, GY, X(step + q) - X(step), bottom - GY + 1, 0x0d3029);
    for (long t = view; t < view + span; t += q) {  // the step ruler: a block a step, the playing one teal, the cursor's yellow
        int x = X(t) + 1, w = X(t + q) - x;
        uint32_t c = t == step ? C_TEAL : t == (long)a->roll_tick ? C_YELLOW : t % beat == 0 ? 0x505050 : C_LINE;
        if (w >= 2 || c == C_TEAL || c == C_YELLOW) ui_fill(fb, stride, x, GY - 3, w > 2 ? w : 2, 2, c);
    }
    for (long t = (view + q - 1) / q * q; t < view + span; t += q) {  // steps, beats, bars
        uint32_t c = t % bar == 0 ? 0x505050 : t % beat == 0 ? 0x303030 : 0x1a1a1a;
        if (c == 0x1a1a1a && q * GW / span < 4) continue;  // steps closer than 4 px would only blur the grid
        ui_fill(fb, stride, X(t), GY, 1, bottom - GY + 1, c);
    }
    for (int32_t i = head(a); i >= 0; i = s->ev[i].next) {  // the notes: velocity colour, the selected one white
        const struct event *ev = &s->ev[i];
        long start = ev->tick, end = start + ev->len;
        if (ev->key / PADS != a->bank || start >= view + span || end <= view) continue;  // also notes from an earlier page
        int x0 = X(start > view ? start : view), x1 = X(end < view + span ? end : view + span);
        int now = step >= 0 && (((long)ev->tick >= step && (long)ev->tick < step + q) ||  // hit on the playing step,
                                ((long)ev->tick <= play && play < (long)ev->tick + ev->len));  // or still sounding
        ui_fill(fb, stride, x0, GY + (PADS - 1 - ev->key % PADS) * RH, x1 - x0 > 2 ? x1 - x0 : 2, RH - 1,
                now ? C_TEAL : i == e ? C_TEXT : vel_colour(ev->vel));
    }
    if ((long)a->roll_tick >= view && (long)a->roll_tick < view + span) {  // the cursor: where a pad's note would go
        int x = X(a->roll_tick), w = X(a->roll_tick + q) - x;
        ui_fill(fb, stride, x, GY, 1, bottom - GY + 1, C_DIM);
        ui_frame(fb, stride, x, GY + (PADS - 1 - a->pad) * RH - 1, w > 3 ? w + 1 : 4, RH + 1, C_YELLOW);
    }
    if (play >= 0) ui_fill(fb, stride, X(play), GY, 2, bottom - GY + 1, C_TEAL);  // the playhead itself
    if (a->confirm >= 0) {
        char what[8];
        row_label(tr, a->confirm, what, sizeof what), snprintf(buf, sizeof buf, "Erase all %s notes?", what);
        ui_confirm(fb, stride, buf);
    }
#undef X
    ui_knobs(a, fb, stride, (struct cell[3]){{C_TEXT, -1, 1}, {C_TEXT, -1, 1}, {C_RED, -1, 0}});  // velocity red, as the guide
    ui_shift_icon(fb, stride, a->shift);
}

const struct mode ROLL_MODE = {
    .pages = {{{"Roll", {P_SEQBARS, P_SEQQ, P_EVVEL}, {P_SEQBARS1, P_SIG, P_EVLEN}}}},
    .npages = {1, 0, 0},
    .pad = pad, .button = button, .led = led, .colour = colour, .draw = draw, .control = control,
};
