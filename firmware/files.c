// File thread, see files.h.
#include "files.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "wav.h"

static void nap(long ms) {
    struct timespec t = {0, ms * 1000000};
    nanosleep(&t, NULL);
}

static int quitting(struct files *f) { return __atomic_load_n(&f->quit, __ATOMIC_ACQUIRE); }

static void deliver(struct files *f, const struct job *j) {  // this thread may wait for room; the audio thread never does
    while (!q_push(&f->res, j) && !quitting(f)) nap(1);
}

void files_do(struct job *j) {
    j->s = NULL;
    if (j->type == JOB_TRIM) {
        j->s = sample_slice(j->src, j->start, j->end);
    } else if (j->type == JOB_NORMALIZE) {
        float peak = sample_peak(j->src, j->start, j->end);
        if (peak > 0) j->s = sample_gain(j->src, 1 / peak), j->value = -20 * log10f(peak);
    }
}

static int is_wav(const struct dirent *d) {
    size_t n = strlen(d->d_name);
    return d->d_name[0] != '.' && n > 4 && !strcasecmp(d->d_name + n - 4, ".wav");  // skips macOS "._" files
}

static void load_dir(struct files *f) {  // the first BANKS*PADS WAV files, in name order, onto banks A..H
    struct dirent **list;
    int n = scandir(f->dir, &list, is_wav, alphasort), loaded = 0;
    if (n < 0) perror(f->dir);
    for (int i = 0; i < n; i++) {
        if (i < BANKS * PADS && !quitting(f)) {
            char path[1024];
            const char *err = "path too long";
            snprintf(path, sizeof path, "%s/%s", f->dir, list[i]->d_name);
            struct sample *s = strlen(f->dir) + strlen(list[i]->d_name) + 2 <= sizeof path ? wav_load(path, &err) : NULL;
            if (s) {
                printf("%c%02d: %s (%.2f s)\n", 'A' + i / PADS, i % PADS + 1, list[i]->d_name, s->frames / (double)RATE);
                deliver(f, &(struct job){.type = JOB_LOADED, .bank = i / PADS, .pad = i % PADS, .s = s});
                loaded++;
            } else {
                printf("%c%02d: %s skipped: %s\n", 'A' + i / PADS, i % PADS + 1, list[i]->d_name, err);
            }
        }
        free(list[i]);
    }
    if (n > 0) free(list);
    if (n > BANKS * PADS) printf("%d more WAV files ignored\n", n - BANKS * PADS);
    fflush(stdout);
    deliver(f, &(struct job){.type = JOB_DONE, .value = loaded});
}

static void *run(void *arg) {
    struct files *f = arg;
    struct job j;
    load_dir(f);
    while (!quitting(f)) {
        if (q_pop(&f->req, &j)) files_do(&j), deliver(f, &j);
        else nap(10);  // shortcut: polls every 10 ms; switch to an eventfd if latency of edits ever matters
    }
    return NULL;
}

int files_start(struct files *f, const char *dir) {
    f->dir = dir, f->quit = 0;
    return pthread_create(&f->thread, NULL, run, f);
}

void files_stop(struct files *f) {
    __atomic_store_n(&f->quit, 1, __ATOMIC_RELEASE);
    pthread_join(f->thread, NULL);
}
