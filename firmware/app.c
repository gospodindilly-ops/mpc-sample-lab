// Controller, see app.h. Behaviour follows the MPC Sample User Guide; functions of later phases answer with a
// "later" popup and keep their LEDs off ("not available", as the guide's LED convention says).
#include "app.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "surface.h"

static const char *SHIFT_PAD[16] = {"Full Level", "Half Seq", "Double Seq", "Count-In", "Compressor", "Half Speed",
                                    "Double Speed", "MIDI Config", "Fader", "Rec Quantize", "Resample", "Song",
                                    "Trim Sample", "Time Correct", "Warp", "Project"};

void app_popup(struct app *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->popup, sizeof a->popup, fmt, ap);
    va_end(ap);
    a->popup_until = a->now + RATE * 6 / 5;
}

struct pad *app_pad(const struct app *a) { return &a->e->tracks[a->track].pads[a->bank][a->pad]; }

void app_init(struct app *a, struct engine *e, struct sequencer *s) {
    memset(a, 0, sizeof *a);
    a->e = e, a->sq = s, a->mode = &SAMPLE_MODE, a->repeat_div = 1, a->roll_sel = a->confirm = -1;  // Note Repeat at 1/16
    a->knob[0] = a->knob[1] = a->knob[2] = a->fader = a->touched = -1;
    for (int i = 0; i < PADS; i++) a->note_out[i] = a->rec_ev[i] = -1;
    a->redraw = 1;
}

void app_midi_out(struct app *a, int port, int status, int d1, int d2) {
    unsigned char m[3] = {status, d1, d2};
    if (a->ext_len[port] + 3 <= (int)sizeof a->ext[port]) memcpy(a->ext[port] + a->ext_len[port], m, 3), a->ext_len[port] += 3;
}

const struct mode *app_track_mode(const struct app *a) {
    int type = a->e->tracks[a->track].type;
    return type == TRACK_MIDI ? &MIDI_MODE : type == TRACK_AUDIO ? &AUDIO_MODE : &SAMPLE_MODE;
}

static void set_mode(struct app *a, const struct mode *m) {  // pages start over: modes have different page groups
    if (a->mode == m) return;
    a->mode = m, a->group = a->sel = 0, a->touched = a->confirm = -1;  // an unanswered question goes with its screen
    a->page[0] = a->page[1] = a->page[2] = 0;
}

// ---- parameters ---------------------------------------------------------------------------------------

static void *base(const struct app *a, int id) {  // where PARAMS[id] is stored for the selected pad and track
    struct sequence *q = &a->sq->seqs[a->sq->cur];
    switch (PARAMS[id].in) {
    case IN_TRACK: return &a->e->tracks[a->track];
    case IN_SEQ: return q;
    case IN_SQ: return a->sq;
    case IN_SEQTRACK: return &q->trk[a->track];
    case IN_EVENT: return &a->sq->ev[a->roll_sel < 0 ? 0 : a->roll_sel];
    }
    return &app_pad(a)->p;
}
static float pget(const struct app *a, int id) { return param_get(base(a, id), id); }
static void pset(struct app *a, int id, float v) { param_set(base(a, id), id, v); }

float param_norm(const struct app *a, int id) { return (pget(a, id) - PARAMS[id].lo) / (PARAMS[id].hi - PARAMS[id].lo); }
static void set_norm(struct app *a, int id, float n) { pset(a, id, PARAMS[id].lo + n * (PARAMS[id].hi - PARAMS[id].lo)); }
static void touch(struct app *a, int k) { a->touched = k, a->touched_until = a->now + RATE * 3 / 2; }  // its value shows

static int editable(const struct app *a, int id) {  // a pad's settings need a sample on it, a note must be selected
    if (id == P_NONE) return 0;
    if (PARAMS[id].in == IN_EVENT) return seq_holds(a->sq, a->sq->cur, a->track, a->roll_sel);
    return PARAMS[id].in != IN_PAD || app_pad(a)->smp != NULL;
}

static void let_go(struct app *a) {  // before the bank, track or track type changes: held pads would never be released
    for (int i = 0; i < PADS; i++)
        if (a->pressed[i]) a->pressed[i] = 0, a->mode->pad(a, i, 0);
}

static void changed(struct app *a, int id) {
    if (id == P_TTYPE) let_go(a), engine_stop_track(a->e, a->track);  // its mode follows when SAMPLE is let go
    if (id == P_SEQBARS || id == P_SEQBARS1 || id == P_SIG || id == P_TLEN) seq_locate(a->sq, a->sq->pos);  // lengths moved
}

static void step_param(struct app *a, int k, int steps) {
    int id = page_param(a, k);
    if (editable(a, id)) pset(a, id, pget(a, id) + steps * PARAMS[id].step), touch(a, k), changed(a, id);
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
    if (a->track_view) return k == 0 ? P_TTYPE : k == 1 ? P_TLEN : P_NONE;  // the track grid: type, length here
    const struct page *pg = &a->mode->pages[a->group][a->page[a->group]];
    int id = a->shift && pg->shift_k[k] ? pg->shift_k[k] : pg->k[k];
    return id == P_DECAY && app_pad(a)->p.note_on ? P_RELEASE : id;  // a Note On pad's envelope fades out on release
}

const char *page_name(const struct app *a, int g) { return a->mode->pages[g][a->page[g]].name ? a->mode->pages[g][a->page[g]].name : ""; }

void param_format(const struct app *a, int id, char *buf, int n) { param_text(id, pget(a, id), buf, n); }

// ---- samples ------------------------------------------------------------------------------------------

static void retire(struct app *a, struct sample *buf) {  // the UI thread may still be drawing it: it frees after its frame
    if (!buf) return;
    if (buf == a->pinned) { a->pinned_dropped = 1; return; }  // the file thread still reads it: retired when back
    unsigned n = a->grave_n;
    if (n - __atomic_load_n(&a->grave_freed, __ATOMIC_ACQUIRE) >= GRAVE) return;  // shortcut: leaks if the UI is 64 edits behind
    a->grave[n % GRAVE] = buf;
    __atomic_store_n(&a->grave_n, n + 1, __ATOMIC_RELEASE);
}

void app_swap_sample(struct app *a, int t, int bank, int i, struct sample *s) {
    struct pad *pd = &a->e->tracks[t].pads[bank][i];
    engine_kill(a->e, t, bank, i);  // the old buffer goes away: no fade may read it
    retire(a, pd->smp);
    __atomic_store_n(&pd->smp, s, __ATOMIC_RELEASE);
}

// Trim and Normalize make a new sample in the file thread; the result comes back through app_result.
void app_edit_sample(struct app *a, int type) {
    struct pad *pd = app_pad(a);
    struct job j = {.type = type, .track = a->track, .bank = a->bank, .pad = a->pad, .start = pd->p.start, .end = pd->p.end, .src = pd->smp};
    if (!pd->smp) { app_popup(a, type == JOB_TRIM ? "Nothing to trim" : "Nothing to normalize"); return; }
    if (a->pinned || !a->jobs || !q_push(a->jobs, &j)) { app_popup(a, "Busy, try again"); return; }  // one edit at a time
    a->pinned = pd->smp;
}

void app_result(struct app *a, const struct job *j) {
    struct pad *pd = &a->e->tracks[j->track].pads[j->bank][j->pad];
    __atomic_store_n(&a->redraw, 1, __ATOMIC_RELEASE);
    if (j->type == JOB_DONE) {
        if (j->value) app_popup(a, "Loaded %d samples", (int)j->value);
        else app_popup(a, "No WAV files found");
        return;
    }
    if (j->src) {  // an edit is back: its source may go now
        struct sample *src = (struct sample *)j->src;
        int dropped = a->pinned_dropped;
        a->pinned = NULL, a->pinned_dropped = 0;
        if (dropped) retire(a, src);
    }
    if (!j->s) { app_popup(a, j->type == JOB_NORMALIZE ? "Nothing to normalize" : "Nothing to trim"); return; }  // silent or too short
    if (pd->smp != j->src) { retire(a, j->s); return; }  // the pad changed meanwhile: drop the result
    app_swap_sample(a, j->track, j->bank, j->pad, j->s);
    if (j->type == JOB_TRIM) {
        struct pad_params *p = &pd->p;
        float len = j->end - j->start;
        p->loop = fminf(fmaxf((p->loop - j->start) / len, 0), 1);
        p->start = 0, p->end = 1;
        app_popup(a, "Trimmed: %.2f s", j->s->frames / (double)RATE);
    } else if (j->type == JOB_NORMALIZE) {
        app_popup(a, "Normalized %+.1f dB", j->value);
    }
}

// ---- notes and the sequencer --------------------------------------------------------------------------

static void later_off(struct app *a, int t, int key, int port, int ch, int frames) {
    if (a->noffs < (int)(sizeof a->offs / sizeof a->offs[0])) a->offs[a->noffs++] = (struct off){t, key, port, ch, frames};
}

static void off_now(struct app *a, const struct off *o) {
    if (o->port < 0) engine_release(a->e, o->track, o->key / PADS, o->key % PADS);
    else app_midi_out(a, o->port, 0x80 | o->ch, midi_note(o->key), 0);
}

// The one way Note Repeat and the sequence reach a track (architecture §6): a drum pad's voice starts on its exact
// frame, a MIDI note goes out with its note-off due `len` frames later. Live pads take the same road in their mode.
static void track_on(struct app *a, int t, int key, int vel, int offset, int len) {
    struct track *tr = &a->e->tracks[t];
    if (tr->type == TRACK_DRUM) {
        engine_trigger_at(a->e, t, key / PADS, key % PADS, vel, offset);
        if (tr->pads[key / PADS][key % PADS].p.note_on) later_off(a, t, key, -1, 0, offset + len);
    } else if (tr->type == TRACK_MIDI) {
        app_midi_out(a, tr->midi_port, 0x90 | (tr->midi_ch - 1), midi_note(key), vel);
        later_off(a, t, key, tr->midi_port, tr->midi_ch - 1, offset + (len > 0 ? len : 1));
    }
}

void app_played(struct app *a, int i, int vel) {
    struct sequencer *s = a->sq;
    int type = a->e->tracks[a->track].type;
    a->noting |= 1u << i, a->repeat_used = 1;
    a->rec_ev[i] = type == TRACK_DRUM || type == TRACK_MIDI ? seq_rec(s, a->track, s->pos, a->bank * PADS + i, vel) : -1;
    a->rec_at[i] = s->clock;
}

void app_released(struct app *a, int i) {
    struct sequencer *s = a->sq;
    int32_t e = a->rec_ev[i];
    a->noting &= ~(1u << i);
    if (e >= 0 && s->ev[e].key == a->bank * PADS + i) s->ev[e].len = (uint32_t)(s->clock - a->rec_at[i]);  // Undo may have taken it
    a->rec_ev[i] = -1;
}

static const int REPEAT_TICKS[6] = {480, 240, 120, 320, 160, 80};  // B1..B3: 1/8, 1/16, 1/32; with SHIFT their triplets

// Note Repeat: held pads hit again on the division's grid, with the swing, as hard as they are pressed now. The grid
// is the sequence's while it plays, else the free-running clock. Runs before the player moves over the block.
static void repeat(struct app *a, int frames) {
    struct sequencer *s = a->sq;
    if (!a->repeat || !a->noting) return;
    double tpf = seq_bpm(s) * PPQ / (60.0 * RATE), t0 = s->playing && !s->count ? s->pos : s->clock;
    int step = REPEAT_TICKS[a->repeat_div];
    for (double g = ceil(t0 / step) * step; g < t0 + frames * tpf; g += step) {
        int off = (int)((g - t0 + seq_swing(s, (uint32_t)g)) / tpf + 0.5);
        for (int i = 0; i < PADS; i++) {
            if (!(a->noting >> i & 1)) continue;
            int key = a->bank * PADS + i, e;
            track_on(a, a->track, key, a->pressed[i], off, (int)(step / 2 / tpf));
            if ((e = seq_rec(s, a->track, g, key, a->pressed[i])) >= 0) s->ev[e].len = step / 2;
        }
    }
}

static void sequence(struct app *a, int frames) {
    struct due d[64];
    int n = seq_advance(a->sq, frames, d, 64);
    for (int i = 0; i < n; i++) {
        int t = d[i].track, key = d[i].key;
        if (t < 0) { engine_click(a->e, key, a->sq->metro_vol * 0.004f, d[i].offset); continue; }
        if (a->erase_held && t == a->track && key / PADS == a->bank && a->pressed[key % PADS]) {
            seq_erase(a->sq, a->sq->cur, t, d[i].ev);  // ERASE + a held pad: its events go as the player meets them
            continue;
        }
        if (!a->e->tracks[t].muted) track_on(a, t, key, d[i].vel, d[i].offset, d[i].len);
    }
}

static void set_bpm(struct app *a, float bpm) {
    struct sequencer *s = a->sq;
    bpm = bpm < 30 ? 30 : bpm > 300 ? 300 : bpm;
    *(s->bpm_global ? &s->bpm : &s->seqs[s->cur].bpm) = bpm;
    app_popup(a, "Tempo %.2f", bpm);
}

static void all_offs(struct app *a) {
    for (int i = 0; i < a->noffs; i++) off_now(a, &a->offs[i]);
    a->noffs = 0;
}

// ---- input --------------------------------------------------------------------------------------------

static void set_bank(struct app *a, int b) {
    let_go(a);
    a->bank = b, a->fader_engaged = 0;
    app_popup(a, "Bank %c", 'A' + b);
}

static void pad_event(struct app *a, int i, int vel) {
    if (!vel) {
        if (a->pressed[i]) a->pressed[i] = 0, a->mode->pad(a, i, 0);
        return;
    }
    if (a->bank_held && i < BANKS) { a->bank_used = 1, set_bank(a, i); return; }  // before pressed[]: let_go clears it
    a->pressed[i] = vel;
    if (a->shift) {  // the SHIFT+pad functions printed under the pads
        struct sequencer *s = a->sq;
        if (i == 0) a->e->full_level = !a->e->full_level, app_popup(a, "Full Level %s", a->e->full_level ? "On" : "Off");
        else if (i == 3) s->count_in = !s->count_in, app_popup(a, "Count-In %s", s->count_in ? "On" : "Off");
        else if (i == 9) s->rec_quant = !s->rec_quant, app_popup(a, "Rec Quantize %s", s->rec_quant ? "On" : "Off");
        else if (i == 12) app_edit_sample(a, JOB_TRIM);
        else app_popup(a, "%s: later", SHIFT_PAD[i]);
        return;
    }
    if (a->mute_mode) {  // MUTE works in every mode: on the Sequence screen the pads are the 16 tracks (MPC 3 Track Mute)
        a->mute_used = 1;
        if (a->mode == &SEQ_MODE && !a->seq_pads) {
            struct track *tr = &a->e->tracks[i];
            if (tr->type != TRACK_EMPTY) tr->muted = !tr->muted;
            return;
        }
        struct pad *pd = &a->e->tracks[a->track].pads[a->bank][i];
        if ((pd->muted = !pd->muted)) engine_stop(a->e, a->track, a->bank, i);
        return;
    }
    a->mode->pad(a, i, vel);
}

static void button(struct app *a, int note, int down) {
    switch (note) {
    case N_SHIFT: a->shift = down; return;
    case N_SAMPLE:
        a->sample_held = down;
        if (down && a->seq_held) { a->seq_pads = 1; return; }  // SEQ + SAMPLE: the Sequence screen, pads play the track
        if (down) {
            if (a->shift) app_popup(a, "Input Config: later");
            a->back = a->mode, set_mode(a, app_track_mode(a)), a->seq_pads = 0;  // the selected track's own screen
            return;
        }
        if (a->track_view && (a->back == &SEQ_MODE || a->back == &ROLL_MODE)) set_mode(a, a->back);  // a track picked: back
        else if (a->mode != &SEQ_MODE) set_mode(a, app_track_mode(a));  // its type may have changed in the grid
        a->track_view = 0;
        return;
    case N_SEQ:
        a->seq_held = down;
        if (!down) return;
        if (a->shift) { set_mode(a, &ROLL_MODE), a->seq_pads = 0, a->mute_mode = 0; return; }  // the guide's STEP EDIT
        if (a->sample_held) { set_mode(a, &SEQ_MODE), a->seq_pads = 1; return; }
        set_mode(a, &SEQ_MODE), a->seq_pads = 0, a->mute_mode = 0;
        return;
    case N_REPEAT:  // a tap latches Note Repeat, a press while it is on ends it, holding it while playing is momentary
        if (down && a->repeat) { a->repeat = 0; return; }
        if (down) a->repeat = a->repeat_tap = 1, a->repeat_used = 0;
        else if (a->repeat_tap) a->repeat = !a->repeat_used, a->repeat_tap = 0;
        return;
    case N_TAP:
        a->tap_held = down;
        if (!down) return;
        if (a->shift) {
            static const char *const M[3] = {"Off", "On", "Rec"};
            a->sq->metro = (a->sq->metro + 1) % 3;
            app_popup(a, "Metronome %s", M[a->sq->metro]);
        } else {
            unsigned gap = a->now - a->tap_at;
            if (gap > RATE / 5 && gap < RATE * 2) {  // 30..300 BPM: two taps make a tempo, more taps steady it
                a->tap_gap = a->tap_gap > 0 ? (a->tap_gap + gap) / 2 : gap;
                set_bpm(a, 60.0f * RATE / a->tap_gap);
            } else {
                a->tap_gap = 0;
            }
            a->tap_at = a->now;
        }
        return;
    case N_PADBANK:
        if (down) { a->bank_held = 1, a->bank_used = 0; return; }
        a->bank_held = 0;
        if (!a->bank_used) set_bank(a, (a->bank + (a->shift ? BANKS - 1 : 1)) % BANKS);
        return;
    case N_ERASE:
        a->erase_held = down;
        if (down && a->shift) app_popup(a, "Copy: later");
        return;
    case N_MUTE:
        if (!down) {
            if (a->mute_held && a->mute_used) a->mute_mode = 0;  // held and used: it was a momentary mute
            a->mute_held = 0;
            return;
        }
        if (a->shift) {
            for (int i = 0; i < PADS; i++) a->e->tracks[a->track].pads[a->bank][i].muted = 0;
            app_popup(a, "Unmute All");
            return;
        }
        a->mute_mode = !a->mute_mode, a->mute_held = a->mute_mode, a->mute_used = 0;
        return;
    }
    if (down && a->repeat && note >= N_B1 && note <= N_B3) {  // Note Repeat on: B1..B3 pick its division
        a->repeat_div = note - N_B1 + (a->shift ? 3 : 0);
        return;
    }
    if (a->mode->button(a, note, down) || !down) return;
    struct sequencer *s = a->sq;
    switch (note) {
    case N_PLAY:
        if (s->armed) a->seq_pads = a->seq_pads ? a->seq_pads : 2;
        seq_play(s, !a->shift);  // SHIFT: Continue from where it stopped
        return;
    case N_SEQREC:
        if (a->shift) { app_popup(a, "Recall: later"); return; }
        if (s->playing) seq_punch(s, !s->recording);
        else if ((s->armed = !s->armed)) set_mode(a, &SEQ_MODE), a->seq_pads = 2;  // the guide's Sample/Seq while armed
        else if (a->seq_pads == 2) a->seq_pads = 0;
        return;
    case N_B1: case N_B2: case N_B3: {  // the next page of the group; what SHIFT+B does is the mode's
        int g = note - N_B1;
        if (a->shift || !a->mode->npages[g]) return;
        if (a->group == g) a->page[g] = (a->page[g] + 1) % a->mode->npages[g];
        a->group = g, a->sel = 0, a->touched = -1;
        if (!a->mode->pages[g][a->page[g]].k[0]) app_popup(a, "%s: later", page_name(a, g));
        return;
    }
    case N_ENCPUSH:
        if (a->bpm_edit || a->tap_held) a->bpm_fine = !a->bpm_fine;  // whole BPM or hundredths, as the guide's BPM window
        else a->sel = (a->sel + (a->shift ? 2 : 1)) % 3;
        return;
    case N_MINUS: case N_PLUS:
        if (a->shift && note == N_MINUS) {
            int n = seq_undo(s);  // shortcut: undoes the last recording pass only; full Undo comes with the project (step 7)
            n ? app_popup(a, "Undo: %d events", n) : app_popup(a, "Nothing to undo");
        } else if (a->shift) {
            app_popup(a, "Redo: later");
        } else {
            step_param(a, a->sel, note == N_PLUS ? 1 : -1);
        }
        return;
    case N_STOP:
        if (a->now - a->stop_at < RATE * 2 / 5) engine_stop_all(a->e);  // a double press stops all sound
        a->stop_at = a->now;
        seq_stop(s), all_offs(a);
        if (a->seq_pads == 2) a->seq_pads = 0;
        return;
    }
    const char *name = button_name(note);
    if (name) app_popup(a, "%s: later", name);
}

static void control(struct app *a, int cc, int v) {
    if (cc == CC_MAIN_VOLUME) {
        a->e->master = (v / 127.0f) * (v / 127.0f);  // squared: closer to perceived loudness
        app_popup(a, "Main Volume %d%%", v * 100 / 127);
    } else if (cc >= CC_K1 && cc <= CC_K3) {
        int k = cc - CC_K1, id = page_param(a, k);
        if (a->knob[k] >= 0 && editable(a, id)) {
            if (fabsf(a->shadow[k] - param_norm(a, id)) * (PARAMS[id].hi - PARAMS[id].lo) > PARAMS[id].step)
                a->shadow[k] = param_norm(a, id);  // resync
            a->shadow[k] = scaled(a->shadow[k], a->knob[k], v);
            set_norm(a, id, a->shadow[k]);
            a->sel = k, touch(a, k), changed(a, id);
        }
        a->knob[k] = v;
    } else if ((cc == CC_FADER || cc == CC_ENCODER) && !a->sample_held && !a->bpm_edit && !a->tap_held && a->mode->control &&
               a->mode->control(a, cc, v)) {
        // the mode's own use: the playhead in Sequence Mode, the cursor and nudge in the piano roll
    } else if (cc == CC_FADER) {  // Pad Volume, Pickup takeover: follows only after crossing the current value
        float t = v / 127.0f, c = param_norm(a, P_VOL);
        if (!a->fader_engaged && a->fader >= 0 && (a->fader / 127.0f - c) * (t - c) <= 0) a->fader_engaged = 1;
        if (a->fader_engaged && app_pad(a)->smp) set_norm(a, P_VOL, t);
        a->fader = v;
    } else if (cc == CC_ENCODER) {  // relative, as the stock parser reads it: 1..64 = +n, 65..127 = -63..-1
        int d = v > 64 ? v - 128 : v;
        if (a->bpm_edit || a->tap_held) {
            struct sequencer *s = a->sq;
            set_bpm(a, (s->bpm_global ? s->bpm : s->seqs[s->cur].bpm) + d * (a->bpm_fine ? 0.01f : 1));
        } else if (a->sample_held) {  // SAMPLE + encoder: track select; the 16 tracks show until SAMPLE is let go
            int t = a->track + d;
            t = t < 0 ? 0 : t > TRACKS - 1 ? TRACKS - 1 : t;
            if (t != a->track) let_go(a), a->track = t, a->fader_engaged = 0;
            if (!a->track_view) a->track_view = 1, a->sel = 0, a->touched = -1;
        } else {
            step_param(a, a->sel, d);
        }
    }
}

void app_midi(struct app *a, const unsigned char *m) {
    int st = m[0] & 0xF0, ch = (m[0] & 0x0F) + 1, i;
    if (ch == 10 && (st == 0x90 || st == 0x80) && (i = pad_index(m[1])) >= 0) pad_event(a, i, st == 0x90 ? m[2] : 0);
    else if (ch == 1 && (st == 0x90 || st == 0x80)) button(a, m[1], st == 0x90 && m[2] > 0);
    else if (ch == 1 && st == 0xB0) control(a, m[1], m[2]);
    else if (ch == 10 && st == 0xA0 && (i = pad_index(m[1])) >= 0 && a->pressed[i]) a->pressed[i] = m[2] ? m[2] : 1;  // pressure
    else return;
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
    int m = a->mode->led(a, note);
    if (m >= 0) return m;
    switch (note) {
    case N_SHIFT: return a->shift ? 2 : 1;
    case N_PADBANK: return a->bank_held ? 2 : 1;
    case N_ERASE: return a->erase_held ? 2 : 1;
    case N_MUTE: return a->mute_mode ? 2 : 1;
    case N_MINUS: case N_PLUS: case N_STOP: return 1;
    case N_PLAY: return a->sq->playing ? 2 : 1;
    case N_SEQREC: return a->sq->recording ? 2 : a->sq->armed ? 1 + (a->now / (RATE / 4) & 1) : 1;  // armed: flashing
    case N_REPEAT: return a->repeat ? 2 : 1;
    case N_TAP: return a->sq->playing && fmod(a->sq->pos, PPQ) < PPQ / 4 ? 2 : 1;  // flashes on the beat
    }
    return 0;
}

static void pad_colour(const struct app *a, int i, unsigned char c[3]) {
    const struct pad *pd = &a->e->tracks[a->track].pads[a->bank][i];
    c[0] = c[1] = c[2] = 0;
    if (a->bank_held && i < BANKS) c[1] = c[2] = i == a->bank ? 64 : 16;  // bank selector
    else if (a->pressed[i]) c[0] = a->pressed[i], c[2] = 127 - a->pressed[i];
    else if (a->mute_mode && a->mode == &SEQ_MODE && !a->seq_pads) {  // Track Mute, as MPC 3: muted orange, others yellow
        if (a->e->tracks[i].type != TRACK_EMPTY) c[0] = 64, c[1] = a->e->tracks[i].muted ? 6 : 20;
    }
    else if (a->mute_mode) { if (pd->smp) c[0] = 64, c[1] = pd->muted ? 0 : 20; }  // muted red, unmuted yellow (green die is ~3x brighter)
    else a->mode->colour(a, i, c);
}

static void emit(struct app *a, const unsigned char *m, int n) {
    if (a->out_len + n <= (int)sizeof a->out) memcpy(a->out + a->out_len, m, n), a->out_len += n;
}

void app_tick(struct app *a, int frames) {  // the audio thread, before the block is rendered
    a->now += frames;
    for (int i = 0; i < a->noffs;)  // note-offs due in the past block
        if ((a->offs[i].left -= frames) <= 0) off_now(a, &a->offs[i]), a->offs[i] = a->offs[--a->noffs];
        else i++;
    repeat(a, frames);
    sequence(a, frames);
    if (a->popup[0] && (int)(a->now - a->popup_until) > 0) a->popup[0] = 0, __atomic_store_n(&a->redraw, 1, __ATOMIC_RELEASE);
    if (a->touched >= 0 && (int)(a->now - a->touched_until) > 0) a->touched = -1, __atomic_store_n(&a->redraw, 1, __ATOMIC_RELEASE);
    a->meter_peak = fmaxf(a->meter_peak, a->e->peak);
    if ((int)(a->now - a->meter_at) >= RATE / 20) {  // ~20 moves a second at most: each one redraws the screen
        float db = 20 * log10f(a->meter_peak + 1e-9f);
        int m = db <= -48 ? 0 : db >= 0 ? 256 : (int)((db + 48) / 48 * 256);  // -48..0 dBFS across the screen
        m = m > a->meter - 13 ? m : a->meter - 13;  // rises at once, falls over about a second
        if (m != a->meter) a->meter = m < 0 ? 0 : m, __atomic_store_n(&a->redraw, 1, __ATOMIC_RELEASE);
        if (a->sq->playing && (a->mode == &SEQ_MODE || a->mode == &ROLL_MODE)) __atomic_store_n(&a->redraw, 1, __ATOMIC_RELEASE);
        a->meter_peak = 0, a->meter_at = a->now;
    }
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
