// Background work: loading WAV files and making edited copies of samples. Everything slow or allocating runs in
// this thread, never in the audio thread. The two talk through single-producer single-consumer queues.
#pragma once
#include <pthread.h>
#include "engine.h"

enum { JOB_LOADED = 1, JOB_DONE, JOB_TRIM, JOB_NORMALIZE };

struct job {
    int type, track, bank, pad;  // LOADED goes to track 0
    float start, end;            // TRIM / NORMALIZE: range of the source, 0..1
    const struct sample *src;    // TRIM / NORMALIZE: source, kept alive by the app until the result is back
    struct sample *s;            // result: the new sample, NULL if it failed
    float value;                 // NORMALIZE: gain in dB; DONE: number of samples loaded
};

#define QSIZE 64  // power of two

// Lock-free ring: exactly one thread pushes and one pops. Neither call ever waits.
struct queue { struct job q[QSIZE]; unsigned head, tail; };

static inline int q_push(struct queue *q, const struct job *j) {
    unsigned h = q->head, t = __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE);
    if (h - t == QSIZE) return 0;
    q->q[h % QSIZE] = *j;
    __atomic_store_n(&q->head, h + 1, __ATOMIC_RELEASE);
    return 1;
}

static inline int q_pop(struct queue *q, struct job *j) {
    unsigned t = q->tail, h = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
    if (t == h) return 0;
    *j = q->q[t % QSIZE];
    __atomic_store_n(&q->tail, t + 1, __ATOMIC_RELEASE);
    return 1;
}

struct files {
    struct queue req, res;       // req: app -> file thread, res: file thread -> audio thread
    const char *dir;             // WAV folder loaded into banks A..H at start
    pthread_t thread;
    int quit;
};

int files_start(struct files *f, const char *dir);  // 0 on success
void files_stop(struct files *f);
void files_do(struct job *j);                       // runs one TRIM / NORMALIZE job here and now
