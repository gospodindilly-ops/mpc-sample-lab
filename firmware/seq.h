// Sequencer core: the clock (960 ticks per quarter note, as MPC 3), 128 sequences holding the note events of 16
// tracks, a length of its own for each track in each sequence, and the player that says which events start in the
// coming audio block and on which frame. No I/O and no allocation: it runs in the audio thread. What an event does
// (a drum hit, a MIDI note) is the app's business.
#pragma once
#include <stdint.h>
#include "engine.h"

#define PPQ 960
#define SEQS 128      // banks A..H of 16, as the MPC Sample's sequences
#define EVENTS 65536  // note events shared by all sequences: 16 B each, 1 MB

enum { METRO_OFF, METRO_ON, METRO_REC };  // the guide's metronome states: never, always, while recording

struct event { uint32_t tick, len; int32_t next; uint8_t key, vel, take, skip; };  // key: bank * 16 + pad
struct seqtrack { int32_t head, cursor; int len_beats, dirty; };  // events by tick; len_beats 0: the sequence's length
struct sequence { float bpm; int bars; struct seqtrack trk[TRACKS]; };
struct due { int track, key, vel, offset, len; int32_t ev; };  // starts `offset` frames into the block, lasts `len` frames;
                                                                // track -1: a metronome click, key 1 on a bar's first beat

struct sequencer {
    struct sequence seqs[SEQS];
    struct event ev[EVENTS];
    int32_t free_list;
    float bpm;                     // the global tempo (GBL); a sequence's own is seqs[n].bpm (SEQ)
    int bpm_global, sig, q, swing, rec_quant, metro, metro_vol, count_in;  // sig: SIGS index; q: Q_TICKS index
    int cur, next;                 // current sequence; the one that follows it at its end (-1: the same again)
    int playing, recording, armed;
    double pos;                    // ticks into the current sequence
    double count;                  // count-in ticks left before recording starts
    double clock;                  // ticks since start, running also when stopped: Note Repeat keeps to it
    uint8_t take;                  // recording pass; Undo removes its events
};

extern const int Q_TICKS[8];      // 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T
extern const unsigned char SIGS[12][2];

void seq_init(struct sequencer *s);
float seq_bpm(const struct sequencer *s);
int seq_bar(const struct sequencer *s);                    // ticks per bar
uint32_t seq_len(const struct sequencer *s, int n);
uint32_t seq_track_len(const struct sequencer *s, int n, int t);
int seq_used(const struct sequencer *s, int n);            // has events
int seq_add(struct sequencer *s, int n, int t, uint32_t tick, int key, int vel);  // event index; -1: pool full
void seq_erase(struct sequencer *s, int n, int t, int32_t ev);
int seq_erase_key(struct sequencer *s, int n, int t, int key);          // every event of a pad or note; how many
void seq_move(struct sequencer *s, int n, int t, int32_t ev, uint32_t tick);  // keeps the list in order
int32_t seq_at(const struct sequencer *s, int n, int t, uint32_t from, uint32_t to, int key);  // first in [from, to); -1
int seq_holds(const struct sequencer *s, int n, int t, int32_t ev);     // is ev still one of that track's events
void seq_clear(struct sequencer *s, int n);                // every event of sequence n
int seq_undo(struct sequencer *s);                         // removes the last recording pass; how many events
int seq_rec(struct sequencer *s, int t, double at, int key, int vel);  // record at sequence tick `at`; event or -1
void seq_locate(struct sequencer *s, double tick);
void seq_play(struct sequencer *s, int from_start);        // also starts recording when armed (after the count-in)
void seq_punch(struct sequencer *s, int on);              // recording on or off while playing
void seq_stop(struct sequencer *s);
int seq_advance(struct sequencer *s, int frames, struct due *out, int max);  // moves the clock one audio block
double seq_swing(const struct sequencer *s, uint32_t tick);  // RT Swing delay in ticks for a hit on `tick`
