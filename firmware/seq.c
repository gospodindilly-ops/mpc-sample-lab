// Sequencer core, see seq.h. Each track's events form a list sorted by tick inside one shared pool, so recording
// never allocates; a cursor per track remembers where the player is, so a block costs only the events it plays.
#include "seq.h"
#include <math.h>
#include <string.h>

const int Q_TICKS[8] = {960, 640, 480, 320, 240, 160, 120, 80};
const unsigned char SIGS[12][2] = {{4, 4}, {3, 4}, {2, 4}, {5, 4}, {6, 4}, {7, 4}, {3, 8}, {5, 8}, {6, 8}, {7, 8}, {9, 8}, {12, 8}};

void seq_init(struct sequencer *s) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < EVENTS; i++) s->ev[i].next = i + 1 < EVENTS ? i + 1 : -1;
    for (int n = 0; n < SEQS; n++) {
        s->seqs[n].bpm = 90, s->seqs[n].bars = 4;  // as the guide's Sequence Mode screen: 90 BPM, 4 bars
        for (int t = 0; t < TRACKS; t++) s->seqs[n].trk[t].head = s->seqs[n].trk[t].cursor = -1;
    }
    s->bpm = 90, s->q = 4, s->swing = 50, s->metro = METRO_REC, s->metro_vol = 70, s->count_in = 1, s->next = -1;
}

float seq_bpm(const struct sequencer *s) { return s->bpm_global ? s->bpm : s->seqs[s->cur].bpm; }
int seq_bar(const struct sequencer *s) { return SIGS[s->sig][0] * PPQ * 4 / SIGS[s->sig][1]; }
uint32_t seq_len(const struct sequencer *s, int n) { return (uint32_t)s->seqs[n].bars * seq_bar(s); }

uint32_t seq_track_len(const struct sequencer *s, int n, int t) {
    uint32_t own = (uint32_t)s->seqs[n].trk[t].len_beats * PPQ, len = seq_len(s, n);
    return own && own < len ? own : len;
}

int seq_used(const struct sequencer *s, int n) {
    for (int t = 0; t < TRACKS; t++)
        if (s->seqs[n].trk[t].head >= 0) return 1;
    return 0;
}

int seq_add(struct sequencer *s, int n, int t, uint32_t tick, int key, int vel) {
    int32_t i = s->free_list;
    if (i < 0) return -1;
    s->free_list = s->ev[i].next;
    s->ev[i] = (struct event){.tick = tick, .key = key, .vel = vel, .take = s->take};
    struct seqtrack *st = &s->seqs[n].trk[t];
    int32_t *at = &st->head;  // after the events on the same tick: they keep the order they were played in
    while (*at >= 0 && s->ev[*at].tick <= tick) at = &s->ev[*at].next;
    s->ev[i].next = *at, *at = i, st->dirty = 1;
    return i;
}

static void unlink(struct sequencer *s, struct seqtrack *st, int32_t *at) {  // *at: the link that points at the event
    int32_t i = *at;
    *at = s->ev[i].next, s->ev[i].next = s->free_list, s->free_list = i, st->dirty = 1;
}

void seq_erase(struct sequencer *s, int n, int t, int32_t ev) {
    struct seqtrack *st = &s->seqs[n].trk[t];
    for (int32_t *at = &st->head; *at >= 0; at = &s->ev[*at].next)
        if (*at == ev) { unlink(s, st, at); return; }
}

int seq_erase_key(struct sequencer *s, int n, int t, int key) {
    struct seqtrack *st = &s->seqs[n].trk[t];
    int gone = 0;
    for (int32_t *at = &st->head; *at >= 0;)
        if (s->ev[*at].key == key) unlink(s, st, at), gone++;
        else at = &s->ev[*at].next;
    return gone;
}

void seq_move(struct sequencer *s, int n, int t, int32_t ev, uint32_t tick) {  // out of the list and back in at tick
    struct seqtrack *st = &s->seqs[n].trk[t];
    int32_t *at = &st->head;
    while (*at >= 0 && *at != ev) at = &s->ev[*at].next;
    if (*at < 0) return;
    *at = s->ev[ev].next, s->ev[ev].tick = tick;
    for (at = &st->head; *at >= 0 && s->ev[*at].tick <= tick;) at = &s->ev[*at].next;
    s->ev[ev].next = *at, *at = ev, st->dirty = 1;
}

int32_t seq_at(const struct sequencer *s, int n, int t, uint32_t from, uint32_t to, int key) {
    for (int32_t i = s->seqs[n].trk[t].head; i >= 0 && s->ev[i].tick < to; i = s->ev[i].next)
        if (s->ev[i].tick >= from && s->ev[i].key == key) return i;
    return -1;
}

int seq_holds(const struct sequencer *s, int n, int t, int32_t ev) {
    for (int32_t i = ev < 0 ? -1 : s->seqs[n].trk[t].head; i >= 0; i = s->ev[i].next)
        if (i == ev) return 1;
    return 0;
}

void seq_clear(struct sequencer *s, int n) {
    for (int t = 0; t < TRACKS; t++)
        while (s->seqs[n].trk[t].head >= 0) unlink(s, &s->seqs[n].trk[t], &s->seqs[n].trk[t].head);
}

int seq_undo(struct sequencer *s) {
    int gone = 0;
    for (int n = 0; s->take && n < SEQS; n++)
        for (int t = 0; t < TRACKS; t++)
            for (int32_t *at = &s->seqs[n].trk[t].head; *at >= 0;)
                if (s->ev[*at].take == s->take) unlink(s, &s->seqs[n].trk[t], at), gone++;
                else at = &s->ev[*at].next;
    return gone;
}

static void new_take(struct sequencer *s) {
    if (++s->take == 0) {  // after 255 passes: older events become take 0, which Undo never touches
        for (int i = 0; i < EVENTS; i++) s->ev[i].take = 0;
        s->take = 1;
    }
}

int seq_rec(struct sequencer *s, int t, double at, int key, int vel) {
    if (!s->playing || !s->recording || s->count > 0) return -1;
    double len = seq_track_len(s, s->cur, t), now = fmod(s->pos, len), tick = fmod(at, len);
    if (s->rec_quant) {
        tick = floor(tick / Q_TICKS[s->q] + 0.5) * Q_TICKS[s->q];
        if (tick >= len) tick -= len;
    }
    int i = seq_add(s, s->cur, t, (uint32_t)tick, key, vel);
    if (i >= 0) s->ev[i].skip = tick > now;  // still ahead of the player in this pass: it has just sounded live
    return i;
}

static void dirty(struct sequencer *s, int n) {
    for (int t = 0; t < TRACKS; t++) s->seqs[n].trk[t].dirty = 1;
}

void seq_locate(struct sequencer *s, double tick) {
    double len = seq_len(s, s->cur);
    s->pos = fmod(fmod(tick, len) + len, len);
    dirty(s, s->cur);
}

void seq_play(struct sequencer *s, int from_start) {
    if (from_start) s->pos = 0;
    s->playing = 1, s->recording = s->armed, s->armed = 0;
    if (s->recording) s->count = s->count_in ? seq_bar(s) : 0, new_take(s);
    dirty(s, s->cur);
}

void seq_punch(struct sequencer *s, int on) {  // recording on or off while playing
    if (on && !s->recording) new_take(s);
    s->recording = on;
}

void seq_stop(struct sequencer *s) { s->playing = s->recording = s->armed = 0, s->count = 0; }

struct out { struct due *d; int n, max; double tpf; };

static int frame(double f) { return (int)(f + 0.5); }  // nearest: ticks summed block by block drift by a hair

static void clicks(const struct sequencer *s, double a, double b, double off, struct out *o) {
    int beat = PPQ * 4 / SIGS[s->sig][1], bar = seq_bar(s);
    for (double g = ceil(a / beat) * beat; g < b && o->n < o->max; g += beat)
        o->d[o->n++] = (struct due){-1, fmod(g, bar) == 0, 0, frame(off + (g - a) / o->tpf), 0, -1};
}

double seq_swing(const struct sequencer *s, uint32_t tick) {  // RT Swing: the off-beats of the Q grid come late
    int q = Q_TICKS[s->q];
    return s->swing > 50 && !(s->q & 1) && tick % (2 * q) == (uint32_t)q ? (s->swing - 50) * 2.0 * q / 100 : 0;
}

// The events of one track in [x, y) of its own length, off frames into the block at x.
static void play(struct sequencer *s, struct seqtrack *st, int t, double x, double y, double off, struct out *o) {
    int32_t c = st->cursor;
    if (st->dirty || x == 0) {  // edited, moved, or around again: find the first event from the start
        for (c = st->head; c >= 0 && s->ev[c].tick < x;) c = s->ev[c].next;
        st->dirty = 0;
    }
    for (; c >= 0 && s->ev[c].tick < y && o->n < o->max; c = s->ev[c].next) {
        struct event *e = &s->ev[c];
        if (e->skip) { e->skip = 0; continue; }
        o->d[o->n++] = (struct due){t, e->key, e->vel, frame(off + (e->tick - x + seq_swing(s, e->tick)) / o->tpf),
                                    frame(e->len / o->tpf), c};
    }
    st->cursor = c;
}

static void segment(struct sequencer *s, int n, double a, double b, double off, struct out *o) {  // [a, b) of sequence n
    if (s->metro == METRO_ON || (s->metro == METRO_REC && s->recording)) clicks(s, a, b, off, o);
    for (int t = 0; t < TRACKS; t++) {
        struct seqtrack *st = &s->seqs[n].trk[t];
        if (st->head < 0) continue;
        double len = seq_track_len(s, n, t), ta = fmod(a, len), tb = ta + (b - a);  // a shorter track goes round
        play(s, st, t, ta, fmin(tb, len), off, o);
        if (tb > len) play(s, st, t, 0, tb - len, off + (len - ta) / o->tpf, o);
    }
}

int seq_advance(struct sequencer *s, int frames, struct due *out, int max) {
    struct out o = {out, 0, max, seq_bpm(s) * PPQ / (60.0 * RATE)};
    double span = frames * o.tpf;
    s->clock += span;
    if (!s->playing) return 0;
    if (s->count > 0) {  // count-in: a bar of clicks while the sequence waits; it starts with the next block
        double a = seq_bar(s) - s->count;
        if (s->metro != METRO_OFF) clicks(s, a, a + span, 0, &o);
        s->count = s->count > span ? s->count - span : 0;
        return o.n;
    }
    double len = seq_len(s, s->cur), t0 = fmod(s->pos, len);
    segment(s, s->cur, t0, fmin(t0 + span, len), 0, &o);
    if (t0 + span < len) { s->pos = t0 + span; return o.n; }
    if (s->next >= 0) s->cur = s->next, s->next = -1;  // the end: the queued sequence, or this one again
    s->pos = t0 + span - len;
    segment(s, s->cur, 0, s->pos, (len - t0) / o.tpf, &o);
    return o.n;
}
