// Host self-test of the engine, the Sample Mode controller and the screen (no device needed).
// Build and run: make test  (optional argument: a path prefix; raw 320x240 RGB screenshots go to <prefix>-<page>.rgb)
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "app.h"
#include "seq.h"
#include "ui.h"
#include "wav.h"

#define F 4096
static struct engine e;
static struct app a;
static struct sequencer sq;
static int32_t out[2 * F];

static void load(int bank, int pad, int frames, float value) {  // constant-level sample: easy to reason about
    char name[64];
    snprintf(name, sizeof name, "test %c%02d", 'A' + bank, pad + 1);
    struct sample *s = sample_new(frames, name);
    for (int i = 0; i < 2 * frames; i++) s->lr[i] = value;
    free(e.tracks[0].pads[bank][pad].smp);
    e.tracks[0].pads[bank][pad].smp = s;
}

static struct queue jobs;  // the app's requests to the file thread; tests run them here with files_do

static void run_job(void) {  // what the file thread and the audio loop do for one request
    struct job j;
    assert(q_pop(&jobs, &j));
    files_do(&j);
    app_result(&a, &j);
}

static float level(int f, int ch) { return out[2 * f + ch] / 2147483520.0f; }
static int voices(void) { int n = 0; for (int i = 0; i < SLOTS; i++) n += e.voices[i].active; return n; }
static void midi(int s, int d1, int d2) { unsigned char m[3] = {s, d1, d2}; app_midi(&a, m); }
static void press(int note) { midi(0x90, note, 127), midi(0x80, note, 0); }
static void blocks(int n) { while (n--) app_tick(&a, 128), engine_render(&e, out, 128); }  // the audio loop

static void engine_tests(void) {
    engine_init(&e);
    e.master = 1;
    load(0, 0, 1000, 0.5f);
    engine_render(&e, out, 8);
    assert(out[0] == 0 && voices() == 0);

    engine_trigger(&e, 0, 0, 0, 127);  // one-shot plays to the end, fading over the last TAIL frames
    engine_render(&e, out, 1100);
    assert(fabsf(level(10, 0) - (0.5f - 4 / 27.0f * 0.125f)) < 1e-4f);  // soft clip is slightly below unity at 0.5
    assert(level(990, 0) < level(500, 0) && out[2 * 1050] == 0 && voices() == 0);

    e.tracks[0].pads[0][0].p.vol_db = VOL_MIN_DB;  // -INF
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 100);
    assert(out[2 * 50] == 0);
    e.tracks[0].pads[0][0].p.vol_db = 0;

    e.tracks[0].pads[0][0].p.vel_sens = 0;  // velocity ignored
    engine_trigger(&e, 0, 0, 0, 1), engine_render(&e, out, 100);
    float full = level(50, 0);
    engine_stop_all(&e), engine_render(&e, out, 300);
    e.tracks[0].pads[0][0].p.vel_sens = 127;
    engine_trigger(&e, 0, 0, 0, 64), engine_render(&e, out, 100);
    assert(full > 0.4f && level(50, 0) < full * 0.6f);
    engine_stop_all(&e), engine_render(&e, out, 300);

    e.tracks[0].pads[0][0].p.pan = -1;  // hard left: right side silent
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 100);
    assert(level(50, 0) > 0.4f && out[2 * 50 + 1] == 0);
    engine_stop_all(&e), engine_render(&e, out, 300);
    e.tracks[0].pads[0][0].p.pan = 0;

    e.tracks[0].pads[0][0].p.mono = 1;  // mono retrigger: the old voice fades out (no click), the new one starts
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 100);
    engine_trigger(&e, 0, 0, 0, 127);
    assert(voices() == 2);
    engine_render(&e, out, 300);
    assert(voices() == 1);
    engine_stop_all(&e), engine_render(&e, out, 300);

    load(0, 1, 1000, 0.25f);  // mute group: a hit on another pad of the group fades the first one
    e.tracks[0].pads[0][0].p.mute_group = e.tracks[0].pads[0][1].p.mute_group = 3;
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 50);
    engine_trigger(&e, 0, 0, 1, 127), engine_render(&e, out, 300);
    assert(voices() == 1 && e.voices[0].active + e.voices[1].active == 1 && engine_playing(&e, 0, 0, 1));
    engine_stop_all(&e), engine_render(&e, out, 1100);

    e.tracks[0].pads[0][0].p.semi = 12;  // an octave up plays the 1000 frames in ~500
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 520);
    assert(voices() == 0);
    e.tracks[0].pads[0][0].p.semi = 0;

    e.tracks[0].pads[0][0].p.loop_on = 1;  // one-shot loop: keeps going past the end, a second hit stops it
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 3000);
    assert(voices() == 1 && level(2500, 0) > 0.4f);
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 300);
    assert(voices() == 0);
    e.tracks[0].pads[0][0].p.loop_on = 0;

    e.tracks[0].pads[0][0].p.note_on = 1, e.tracks[0].pads[0][0].p.release = 0;  // note on: stops shortly after the pad is lifted
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 200);
    engine_release(&e, 0, 0, 0), engine_render(&e, out, 100);
    assert(voices() == 0);
    e.tracks[0].pads[0][0].p.note_on = 0;

    load(0, 2, 100, 0);  // reverse starts at the end of the range
    struct sample *s = e.tracks[0].pads[0][2].smp;
    for (int i = 0; i < 100; i++) s->lr[2 * i] = s->lr[2 * i + 1] = i / 200.0f;
    e.tracks[0].pads[0][2].p.reverse = 1;
    engine_trigger(&e, 0, 0, 2, 127), engine_render(&e, out, 4);
    assert(level(0, 0) > level(3, 0) && level(0, 0) > 0.45f);
    engine_render(&e, out, 200);

    e.tracks[0].pads[0][0].p.loop_on = 1;  // MAIN VOLUME glides over ~10 ms instead of stepping: knob moves do not crackle
    engine_trigger(&e, 0, 0, 0, 127), engine_render(&e, out, 100);
    e.master = 0, engine_render(&e, out, 2000);
    assert(level(0, 0) > 0.45f && level(200, 0) > 0.2f && level(200, 0) < 0.4f && level(1999, 0) < 0.01f);
    e.master = 1, engine_stop_all(&e), engine_render(&e, out, 4000);
    e.tracks[0].pads[0][0].p.loop_on = 0;

    e.master = 1, e.tracks[0].vol_db = 6;  // many loud voices clip softly instead of wrapping
    for (int i = 3; i < 16; i++) load(0, i, 1000, 1.0f), engine_trigger(&e, 0, 0, i, 127);
    engine_render(&e, out, 10);
    assert(level(5, 0) > 0.99f && level(5, 0) <= 1.0f);
    engine_stop_all(&e), engine_render(&e, out, 600);  // voices end before their samples go
    for (int b = 0; b < BANKS; b++)
        for (int i = 0; i < PADS; i++) free(e.tracks[0].pads[b][i].smp), e.tracks[0].pads[b][i].smp = NULL;

    // tracks: each its own volume, mute groups stay inside a track
    e.tracks[0].vol_db = 0, e.tracks[1].vol_db = VOL_MIN_DB, engine_render(&e, out, 4000);  // volumes glide: let them settle
    load(0, 0, 4000, 0.1f);
    struct pad *t2 = &e.tracks[1].pads[0][0];
    t2->smp = sample_new(4000, "t2");
    for (int i = 0; i < 8000; i++) t2->smp->lr[i] = 0.1f;
    e.tracks[1].type = TRACK_DRUM;
    e.tracks[0].pads[0][0].p.mute_group = t2->p.mute_group = 1;
    engine_trigger(&e, 0, 0, 0, 127), engine_trigger(&e, 1, 0, 0, 127), engine_render(&e, out, 100);
    assert(voices() == 2 && fabsf(level(99, 0) - 0.1f) < 0.002f);  // track 2 is silent, track 1 is not choked
    e.tracks[1].vol_db = 0, engine_render(&e, out, 2000);  // track 2 fades in over ~10 ms, no step
    assert(level(1, 0) < 0.11f && fabsf(level(1999, 0) - 0.2f) < 0.005f);
    struct pad *t2b = &e.tracks[1].pads[0][1];  // another pad of the same track and group does choke
    t2b->smp = sample_new(4000, "t2b"), t2b->p.mute_group = 1;
    for (int i = 0; i < 8000; i++) t2b->smp->lr[i] = 0.1f;
    engine_trigger(&e, 1, 0, 1, 127), engine_render(&e, out, 600);
    assert(voices() == 2 && engine_playing(&e, 0, 0, 0) && engine_playing(&e, 1, 0, 1) && !engine_playing(&e, 1, 0, 0));
    engine_stop_all(&e), engine_render(&e, out, 600);
    free(t2->smp), free(t2b->smp), t2->smp = t2b->smp = NULL, e.tracks[1].type = TRACK_EMPTY;
    free(e.tracks[0].pads[0][0].smp), e.tracks[0].pads[0][0].smp = NULL;

    engine_init(&e), e.master = 1;  // the 33rd voice: the oldest of 32 fades out in its slot instead of being cut
    for (int i = 0; i < 40; i++) load(i / PADS, i % PADS, 4000, 0.01f), e.tracks[0].pads[i / PADS][i % PADS].p.loop_on = 1;
    for (int i = 0; i < 32; i++) engine_trigger(&e, 0, i / PADS, i % PADS, 127), engine_render(&e, out, 4);
    assert(voices() == 32);
    engine_trigger(&e, 0, 2, 0, 127);
    int fading = 0;
    for (int i = 0; i < SLOTS; i++) fading += e.voices[i].active && e.voices[i].fade;
    assert(voices() == 33 && fading == 1 && e.voices[0].fade);
    engine_render(&e, out, 300);  // FADE is 256 frames
    assert(voices() == 32);
    for (int i = 33; i < 40; i++) engine_trigger(&e, 0, i / PADS, i % PADS, 127);  // more at once: never past the slots
    assert(voices() <= SLOTS);
    engine_stop_all(&e), engine_render(&e, out, 300);
    for (int i = 0; i < 40; i++) free(e.tracks[0].pads[i / PADS][i % PADS].smp), e.tracks[0].pads[i / PADS][i % PADS].smp = NULL;
    puts("engine ok");
}

static int sent(int status, int d1, int d2) {  // did app_tick emit this 3-byte message?
    for (int i = 0; i + 2 < a.out_len; i++)
        if (a.out[i] == status && a.out[i + 1] == d1 && a.out[i + 2] == d2) return 1;
    return 0;
}

static void snap(const char *prefix, const char *name) {  // one raw RGB screenshot for docs and review
    static uint32_t fb[UI_W * UI_H];
    char path[512];
    ui_draw(&a, fb, UI_W);
    snprintf(path, sizeof path, "%s-%s.rgb", prefix, name);
    FILE *f = fopen(path, "wb");
    for (int i = 0; f && i < UI_W * UI_H; i++) fputc(fb[i] >> 16, f), fputc(fb[i] >> 8, f), fputc(fb[i], f);
    if (f) fclose(f), printf("screenshot: %s\n", path);
}

static void app_tests(const char *shot) {
    engine_init(&e);
    seq_init(&sq);
    app_init(&a, &e, &sq);
    a.jobs = &jobs;
    load(0, 0, 44100, 0.3f), load(0, 1, 44100, 0.3f), load(1, 0, 44100, 0.3f);

    app_tick(&a, 128);
    assert(sent(0xB0, 6, 8) && sent(0xB0, 57, 2) && sent(0xB0, 58, 0));  // SAMPLE bright, bank A indicator
    a.out_len = 0, app_tick(&a, 128);
    assert(a.out_len == 0);  // unchanged LEDs are not resent

    press(35);  // PAD BANK cycles A -> B, SHIFT+PAD BANK goes back
    assert(a.bank == 1);
    midi(0x90, 49, 127), press(35), midi(0x80, 49, 0);
    assert(a.bank == 0);
    midi(0x90, 35, 127), midi(0x99, 42, 100), midi(0x89, 42, 0), midi(0x80, 35, 0);  // hold PAD BANK + pad 3
    assert(a.bank == 2);
    press(35), press(35), press(35), press(35), press(35), press(35);  // C -> A (wraps after H)
    assert(a.bank == 0);

    midi(0x99, 36, 100);  // pad 2 (note 36): selects and plays
    assert(a.pad == 1 && engine_playing(&e, 0, 0, 1));
    midi(0x89, 36, 0);

    midi(0x90, 49, 127), press(74), midi(0x80, 49, 0);  // SHIFT+LOOP: reverse; LOOP: loop
    press(74);
    assert(e.tracks[0].pads[0][1].p.reverse && e.tracks[0].pads[0][1].p.loop_on);

    press(43);  // MUTE mode: pad 2 mutes, a second MUTE press leaves the mode
    midi(0x99, 36, 100), midi(0x89, 36, 0);
    assert(e.tracks[0].pads[0][1].muted && a.mute_mode);
    press(43);
    assert(!a.mute_mode);
    midi(0x90, 49, 127), press(43), midi(0x80, 49, 0);  // SHIFT+MUTE: unmute all
    assert(!e.tracks[0].pads[0][1].muted);

    a.page[0] = 0, a.group = 0;  // TRIM page: K2 = End, scaled takeover never jumps and reaches the end with the knob
    midi(0xB0, 17, 30);
    assert(e.tracks[0].pads[0][1].p.end == 1);  // the first move only calibrates
    midi(0xB0, 17, 20);
    assert(a.touched == 1 && a.sel == 1);  // the moved knob shows its value in place of its name for a moment
    float end1 = e.tracks[0].pads[0][1].p.end;
    midi(0xB0, 17, 0);
    assert(end1 < 1 && end1 > 0.6f && e.tracks[0].pads[0][1].p.end <= e.tracks[0].pads[0][1].p.start + 0.002f);
    midi(0xB0, 17, 127);
    assert(e.tracks[0].pads[0][1].p.end == 1);

    press(12);  // B1 again: TRIM -> MIX; SHIFT+K2 is Kit Volume
    assert(a.page[0] == 1 && page_param(&a, 0) == P_VOL);
    midi(0x90, 49, 127);
    assert(page_param(&a, 1) == P_TRKVOL);
    midi(0x80, 49, 0);

    midi(0xB0, 20, 127);  // fader CC20 (Pad Volume, Pickup): does nothing until it crosses the current value
    float vol = e.tracks[0].pads[0][1].p.vol_db;
    midi(0xB0, 20, 120);
    assert(e.tracks[0].pads[0][1].p.vol_db == vol);
    midi(0xB0, 20, 0), midi(0xB0, 20, 0);
    assert(e.tracks[0].pads[0][1].p.vol_db == VOL_MIN_DB);

    a.sel = 2;  // encoder CC100, relative like the stock parser reads it: K3 (Pan) +5, then -1
    midi(0xB0, 100, 5);
    assert(fabsf(e.tracks[0].pads[0][1].p.pan - 0.1f) < 1e-6f);
    midi(0xB0, 100, 127);
    assert(fabsf(e.tracks[0].pads[0][1].p.pan - 0.08f) < 1e-6f);

    struct sample *old = e.tracks[0].pads[0][1].smp;  // ERASE + pad: empty pad; the buffer waits for the UI thread
    midi(0x90, 9, 127), midi(0x99, 36, 100), midi(0x89, 36, 0), midi(0x80, 9, 0);
    assert(!e.tracks[0].pads[0][1].smp && a.grave_n == 1 && a.grave[0] == old);
    ui_free_retired(&a, a.grave_n);
    assert(a.grave_freed == 1);

    midi(0x99, 37, 100), midi(0x89, 37, 0);  // SHIFT+pad 13: trim A01 to Start..End, made by the file thread
    e.tracks[0].pads[0][0].p.start = 0.25f, e.tracks[0].pads[0][0].p.end = 0.75f;
    old = e.tracks[0].pads[0][0].smp;
    midi(0x90, 49, 127), midi(0x99, 49, 100), midi(0x89, 49, 0);
    assert(a.pinned == old && e.tracks[0].pads[0][0].smp == old);  // nothing changes until the result is back
    midi(0x99, 49, 100), midi(0x89, 49, 0), midi(0x80, 49, 0);  // a second edit while one is in flight is refused
    assert(strstr(a.popup, "Busy"));
    run_job();
    assert(!q_pop(&jobs, &(struct job){0}) && !a.pinned);
    assert(e.tracks[0].pads[0][0].smp->frames == 22050 && e.tracks[0].pads[0][0].p.start == 0 && e.tracks[0].pads[0][0].p.end == 1);
    assert(a.grave[(a.grave_n - 1) % GRAVE] == old);
    ui_free_retired(&a, a.grave_n);

    load(1, 1, 1000, 0.5f);  // erased while its normalize is in flight: both buffers are freed once, after the result
    a.bank = 1, a.pad = 1;
    old = e.tracks[0].pads[1][1].smp;
    unsigned before = a.grave_n;
    midi(0x90, 49, 127), press(13), midi(0x80, 49, 0);  // SHIFT+B2: normalize
    midi(0x90, 9, 127), midi(0x99, 36, 100), midi(0x89, 36, 0), midi(0x80, 9, 0);  // ERASE + pad 2
    assert(!e.tracks[0].pads[1][1].smp && a.grave_n == before);  // pinned: not freed yet
    run_job();
    assert(!e.tracks[0].pads[1][1].smp && a.grave_n == before + 2 && !a.pinned);
    ui_free_retired(&a, a.grave_n);
    a.bank = 0, a.pad = 0;

    static uint32_t fb[UI_W * UI_H];  // the screen draws, and the header shows the selected pad's name in yellow
    a.popup[0] = 0;
    ui_draw(&a, fb, UI_W);
    int lit = 0;
    for (int i = UI_W * 53; i < UI_W * 71; i++) lit += fb[i] == C_YELLOW;
    assert(lit > 50);

    static uint32_t front[UI_W * UI_H];  // only changed rectangles go to the panel
    int rect[UI_H][4];
    memcpy(front, fb, sizeof front);
    assert(ui_changes(fb, front, rect) == 0);
    fb[10 * UI_W + 5] ^= 1, fb[11 * UI_W + 300] ^= 1, fb[200 * UI_W + 7] ^= 1, fb[UI_W * UI_H - 1] ^= 1;
    assert(ui_changes(fb, front, rect) == 3 && !memcmp(front, fb, sizeof front));
    assert(rect[0][0] == 5 && rect[0][1] == 10 && rect[0][2] == 301 && rect[0][3] == 12);
    assert(rect[1][0] == 7 && rect[1][1] == 200 && rect[1][2] == 8 && rect[1][3] == 201);
    assert(rect[2][0] == 319 && rect[2][1] == 239 && rect[2][2] == 320 && rect[2][3] == 240);

    load(0, 2, 44100, 0.1f);  // normalize swaps in a louder copy; the cached waveform follows it
    a.pad = 2;
    ui_draw(&a, fb, UI_W);
    midi(0x90, 49, 127), press(13), midi(0x80, 49, 0);  // SHIFT+B2
    run_job();
    assert(fabsf(e.tracks[0].pads[0][2].smp->lr[0] - 1.0f) < 1e-6f);
    ui_draw(&a, front, UI_W);
    assert(memcmp(fb + 88 * UI_W, front + 88 * UI_W, 100 * UI_W * 4));
    ui_free_retired(&a, a.grave_n);
    a.pad = 0;

    a.group = 0, a.page[0] = 2;  // AMP ENV: a slow attack shrinks the dimmed waveform and draws the attack in orange
    ui_draw(&a, front, UI_W);
    e.tracks[0].pads[0][0].p.attack = 100;
    ui_draw(&a, fb, UI_W);
    int wave_flat = 0, wave_env = 0, env_px = 0;
    for (int i = 88 * UI_W; i < 190 * UI_W; i++) wave_flat += front[i] == C_WAVE_DIM, wave_env += fb[i] == C_WAVE_DIM, env_px += fb[i] == C_ORANGE;
    assert(wave_env < wave_flat / 2 && env_px >= 200);
    e.tracks[0].pads[0][0].p.attack = 0;

    a.page[0] = 0;  // SAMPLE + encoder: the 16 tracks show, the encoder picks one; an empty track's pads stay silent
    midi(0x90, 6, 127), midi(0xB0, 100, 3);
    assert(a.track == 3 && a.track_view);
    ui_draw(&a, fb, UI_W);
    assert(fb[(32 + 3 * 41 + 4) * UI_W + 4 + 3 * 79 + 2] == C_TEXT);  // T4 is lit, where pad 4 is: bottom right
    midi(0xB0, 100, 127 - 9);  // -10 stops at track 1
    assert(a.track == 0);
    midi(0xB0, 100, 1), midi(0x80, 6, 0);
    assert(a.track == 1 && !a.track_view);
    engine_stop_all(&e), engine_render(&e, out, 600);
    midi(0x99, 37, 100), midi(0x89, 37, 0);
    assert(voices() == 0);
    e.tracks[0].pads[0][0].p.note_on = 1;  // a held Note On pad is released when the track changes under it
    midi(0x90, 6, 127), midi(0xB0, 100, 127);
    midi(0x80, 6, 0), midi(0x99, 37, 100);
    assert(a.track == 0 && engine_playing(&e, 0, 0, 0));
    midi(0x90, 6, 127), midi(0xB0, 100, 1);
    assert(a.track == 1 && e.voices[0].released && !a.pressed[0]);
    midi(0xB0, 100, 127), midi(0x80, 6, 0), midi(0x89, 37, 0);
    e.tracks[0].pads[0][0].p.note_on = 0;
    engine_stop_all(&e), engine_render(&e, out, 600);

    // track types: in the grid (SAMPLE + encoder) K1 or -/+ set the type; letting SAMPLE go opens that track's screen
    midi(0x90, 6, 127), midi(0xB0, 100, 1);
    assert(a.track == 1 && a.track_view && page_param(&a, 0) == P_TTYPE && a.sel == 0);
    press(54), press(54), press(54);  // Empty -> Drum -> Audio -> MIDI
    assert(e.tracks[1].type == TRACK_MIDI);
    midi(0x80, 6, 0);
    assert(a.mode == &MIDI_MODE && !a.track_view);
    press(13);  // MIDI has one page: B2 does nothing (and does not divide by zero)
    a.ext_len[0] = a.ext_len[1] = 0;
    midi(0x99, 37, 100);  // pad 1 (bank A) sends C1 on channel 1 of the External port
    assert(a.ext_len[PORT_EXTERNAL] == 3 && a.ext[0][0] == 0x90 && a.ext[0][1] == 36 && a.ext[0][2] == 100 && voices() == 0);
    press(111), midi(0xB0, 100, 2), press(111), press(111);  // encoder on K2 (Channel): 1 -> 3, while the note is held
    midi(0x89, 37, 0);  // the note ends where it started
    assert(e.tracks[1].midi_ch == 3 && a.ext_len[0] == 6 && a.ext[0][3] == 0x80 && a.ext[0][4] == 36);
    e.tracks[1].midi_port = PORT_USB, a.bank = 1;
    midi(0x99, 36, 90);  // pad 2 of bank B: note 36 + 16 + 1 on channel 3 of USB
    assert(a.ext_len[PORT_USB] == 3 && a.ext[1][0] == 0x92 && a.ext[1][1] == 53 && a.pad == 1);
    midi(0x90, 6, 127), midi(0xB0, 100, 1), midi(0xB0, 100, 127), press(55);  // a type change ends a held note first
    assert(e.tracks[1].type == TRACK_AUDIO && a.ext_len[1] == 6 && a.ext[1][3] == 0x82 && !a.pressed[1]);
    midi(0x89, 36, 0), midi(0x99, 37, 100), midi(0x89, 37, 0);  // SAMPLE + pad: a silent select, nothing goes out
    assert(a.ext_len[1] == 6 && a.pad == 0);
    midi(0x80, 6, 0);
    assert(a.mode == &AUDIO_MODE);
    a.ext_len[0] = a.ext_len[1] = 0, a.bank = 0;
    midi(0x99, 37, 100), midi(0x89, 37, 0);  // pads play nothing on an audio track
    assert(voices() == 0 && !a.ext_len[0] && !a.ext_len[1]);
    midi(0xB0, 100, 127 - 3);  // its volume needs no sample: -2 dB
    assert(e.tracks[1].vol_db == -2);
    midi(0x90, 6, 127), midi(0xB0, 100, 127), midi(0x80, 6, 0);  // back to track 1: Sample Mode again
    assert(a.track == 0 && a.mode == &SAMPLE_MODE);

    e.master = 1, e.tracks[0].vol_db = 0, a.meter = 0;  // the level strip: up at once with sound, down slowly after
    load(0, 5, 44100, 0.5f);
    engine_trigger(&e, 0, 0, 5, 127);
    for (int i = 0; i < 20; i++) engine_render(&e, out, 128), app_tick(&a, 128);
    int up = a.meter;
    assert(up > 200 && up < 256);  // -6 dBFS
    engine_stop_all(&e);
    for (int i = 0; i < 40; i++) engine_render(&e, out, 128), app_tick(&a, 128);
    assert(a.meter < up && a.meter > 0);
    for (int i = 0; i < 400; i++) engine_render(&e, out, 128), app_tick(&a, 128);
    assert(a.meter == 0);

    // step 5: sequences on 16 tracks. Track 1 is a Drum track with a sample on A01; 120 BPM: a beat is 172 blocks
    a.track = 0, a.bank = 0, a.mode = &SAMPLE_MODE, a.mute_mode = 0;
    load(0, 0, 4000, 0.2f);
    sq.bpm_global = 1, sq.bpm = 120, sq.metro = METRO_OFF;
    press(7);  // SEQ: Sequence Mode, where pads pick sequences
    assert(a.mode == &SEQ_MODE && !a.seq_pads);
    midi(0x99, 42, 100), midi(0x89, 42, 0);  // pad 3: sequence A03
    assert(sq.cur == 2);
    midi(0x90, 49, 127), midi(0x99, 82, 100), midi(0x89, 82, 0), midi(0x80, 49, 0);  // SHIFT + pad 4: count-in off
    assert(!sq.count_in);
    press(80);  // SEQ RECORD arms; the pads play the track meanwhile (the guide's Sample/Seq Mode)
    assert(sq.armed && a.seq_pads == 2);
    press(82);  // PLAY: recording from the start
    assert(sq.playing && sq.recording);
    blocks(172);
    midi(0x99, 37, 100), blocks(20), midi(0x89, 37, 0);  // pad 1 on beat 2, held for 20 blocks
    int32_t ev = sq.seqs[2].trk[0].head;
    assert(ev >= 0 && sq.ev[ev].key == 0 && sq.ev[ev].len > 100 && labs((long)sq.ev[ev].tick - PPQ) < 20);
    press(81);  // STOP: playing and recording end, the pads pick sequences again
    assert(!sq.playing && !sq.recording && a.seq_pads == 0);
    unsigned hits = e.clock;
    press(82), blocks(180), press(81);  // PLAY: the hit sounds on its beat
    assert(e.clock == hits + 1);

    press(43), midi(0x99, 37, 100), midi(0x89, 37, 0), press(43);  // MUTE here is Track Mute: pad 1 is track 1
    assert(e.tracks[0].muted && !a.mute_mode);
    hits = e.clock;
    press(82), blocks(180), press(81);
    assert(e.clock == hits);  // a muted track's events do not play
    press(43), midi(0x99, 37, 100), midi(0x89, 37, 0), press(43);
    assert(!e.tracks[0].muted);

    sq.seqs[2].bars = 1, seq_add(&sq, 5, 0, 0, 0, 100);  // a pad pressed while playing queues its sequence for the end
    press(82), midi(0x99, 38, 100), midi(0x89, 38, 0);
    assert(sq.next == 5 && sq.cur == 2);
    blocks(4 * 22050 / 128 + 2);
    assert(sq.cur == 5 && sq.next == -1);
    press(81), sq.cur = 2;

    midi(0x90, 9, 127), midi(0x99, 38, 100), midi(0x89, 38, 0), midi(0x80, 9, 0);  // ERASE + pad 6 asks before erasing A06
    assert(a.confirm == 5 && seq_used(&sq, 5));
    press(12);  // B1: Cancel
    assert(a.confirm == -1 && seq_used(&sq, 5));
    midi(0x90, 9, 127), midi(0x99, 38, 100), midi(0x89, 38, 0), midi(0x80, 9, 0), press(14);  // B3: Do It!
    assert(!seq_used(&sq, 5) && a.confirm == -1);
    midi(0x90, 9, 127), midi(0x99, 38, 100), midi(0x89, 38, 0), midi(0x80, 9, 0);  // an empty one: nothing to ask
    assert(a.confirm == -1 && strstr(a.popup, "empty"));

    press(6), press(11);  // Sample Mode; NOTE REPEAT tapped: latched on; B2: 1/16, a hit every 5512 frames
    assert(a.mode == &SAMPLE_MODE && a.repeat);
    press(13);
    hits = e.clock;
    midi(0x99, 37, 100), blocks(172), midi(0x89, 37, 0);  // held for a beat while stopped: 1 + 3 or 4 repeats
    assert(e.clock - hits >= 4 && e.clock - hits <= 5);
    press(11);  // pressed again: off
    assert(!a.repeat);
    midi(0x90, 11, 127), midi(0x99, 37, 100), midi(0x89, 37, 0), midi(0x80, 11, 0);  // held while playing: momentary
    assert(!a.repeat);
    engine_stop_all(&e), blocks(4);

    press(53), blocks(172), press(53);  // TAP TEMPO: two taps half a second apart
    assert(fabsf(sq.bpm - 120.2f) < 0.5f);
    sq.bpm = 120;

    e.tracks[1].type = TRACK_MIDI, e.tracks[1].midi_port = PORT_EXTERNAL, e.tracks[1].midi_ch = 2;  // a MIDI track's events
    sq.cur = 7, ev = seq_add(&sq, 7, 1, 0, 0, 90), sq.ev[ev].len = PPQ / 2;
    a.ext_len[0] = 0;
    press(82), blocks(1);
    assert(a.ext_len[0] == 3 && a.ext[0][0] == 0x91 && a.ext[0][1] == 36 && a.ext[0][2] == 90);
    blocks(90);  // half a beat later its note-off
    assert(a.ext_len[0] == 6 && a.ext[0][3] == 0x81 && a.ext[0][4] == 36);
    press(81), e.tracks[1].type = TRACK_EMPTY, sq.cur = 2;

    midi(0x90, 9, 127), press(82), midi(0x99, 37, 100);  // playing: ERASE + held pad erases its events, keeps the sample
    blocks(180), midi(0x89, 37, 0), midi(0x80, 9, 0), press(81);
    assert(!seq_used(&sq, 2) && e.tracks[0].pads[0][0].smp);

    press(80), press(82), blocks(10), midi(0x99, 37, 100), midi(0x89, 37, 0), press(81);  // a take, then SHIFT+MINUS
    assert(seq_used(&sq, 2));
    midi(0x90, 49, 127), press(55), midi(0x80, 49, 0);
    assert(!seq_used(&sq, 2) && strstr(a.popup, "Undo"));
    engine_stop_all(&e), blocks(4), a.mode = &SAMPLE_MODE, a.popup[0] = 0;

    // the piano roll (SHIFT + SEQ): the guide's Step Edit controls on a grid
    sq.cur = 3, a.track = 0, a.bank = 0, a.pad = 0, sq.q = 4;
    midi(0x90, 49, 127), press(7), midi(0x80, 49, 0);
    assert(a.mode == &ROLL_MODE && a.roll_tick == 0);
    midi(0xB0, 100, 2);  // the encoder: two 1/16 steps on
    assert(a.roll_tick == 480);
    midi(0x99, 36, 90), midi(0x89, 36, 0);  // pad 2 puts a 1/16 note on the cursor step and selects it
    int32_t n1 = sq.seqs[3].trk[0].head;
    assert(n1 >= 0 && a.roll_sel == n1 && sq.ev[n1].tick == 480 && sq.ev[n1].key == 1 && sq.ev[n1].vel == 90);
    assert(sq.ev[n1].len == 240 && sq.ev[n1].take == 0);
    midi(0x99, 42, 70), midi(0x89, 42, 0);  // pad 3 on the same step: a second note
    midi(0x99, 36, 100), midi(0x89, 36, 0);  // pad 2 again selects its note and adds nothing
    int32_t n2 = sq.ev[n1].next;
    assert(a.roll_sel == n1 && n2 >= 0 && sq.ev[n2].key == 2 && sq.ev[n2].next < 0);
    press(54);  // +: the next note on the step
    assert(a.pad == 2 && a.roll_sel == n2);
    midi(0xB0, 18, 64), midi(0xB0, 18, 127);  // K3: its velocity, up to 127 with the knob
    assert(sq.ev[n2].vel == 127);
    midi(0xB0, 20, 60);
    uint32_t t2 = sq.ev[n2].tick;
    midi(0xB0, 20, 70);  // the fader nudges it: 10 steps, 40 ticks later
    assert(sq.ev[n2].tick == t2 + 40 && a.roll_sel == n2);
    press(14);  // B3 erases the selected note
    assert(!seq_holds(&sq, 3, 0, n2) && a.roll_sel == -1);
    midi(0x90, 9, 127), midi(0x99, 36, 100), midi(0x89, 36, 0), midi(0x80, 9, 0);  // ERASE + pad 2 asks first
    assert(a.confirm == 1 && seq_holds(&sq, 3, 0, n1));
    press(12);  // B1: Cancel
    assert(a.confirm == -1 && seq_holds(&sq, 3, 0, n1));
    midi(0x90, 9, 127), midi(0x99, 36, 100), midi(0x89, 36, 0), midi(0x80, 9, 0), press(14);  // B3: Do It!
    assert(!seq_used(&sq, 3) && a.confirm == -1);
    a.roll_zoom = 0, a.pad = 0;  // a note longer than the page also shows on the next page, where it still sounds
    sq.ev[seq_add(&sq, 3, 0, 0, 0, 100)].len = 2 * 4 * PPQ, a.roll_tick = 4 * PPQ;
    static uint32_t page[UI_W * UI_H];
    ui_draw(&a, page, UI_W);
    assert(page[(53 + 15 * 9 + 3) * UI_W + 30 + 5] != 0x1c1c1c && page[(53 + 15 * 9 + 3) * UI_W + 30 + 5] != C_BG);  // row 1, left
    seq_clear(&sq, 3), a.roll_tick = 480, a.pad = 2;
    press(13);  // B2: zoom 2 bars
    assert(a.roll_zoom == 1);
    midi(0x90, 6, 127), midi(0xB0, 100, 1), midi(0x80, 6, 0);  // SAMPLE + encoder picks a track and comes back here
    assert(a.track == 1 && a.mode == &ROLL_MODE);
    midi(0x90, 6, 127), midi(0xB0, 100, 127), midi(0x80, 6, 0);
    assert(a.track == 0 && a.mode == &ROLL_MODE);
    seq_add(&sq, 3, 0, 0, 0, 127), a.pad = 5;  // playing, the pads chase the playing step: pad 1 lit on step 1 only
    press(82), blocks(2);
    assert(a.pad_sent[0][0] == 64 && a.pad_sent[0][1] == 0 && a.pad_sent[0][2] == 0);
    blocks(50);  // a 1/16 step at 120 BPM is 43 blocks
    assert(a.pad_sent[0][0] == 6 && a.pad_sent[0][1] == 6);
    press(81), seq_clear(&sq, 3);
    press(6);  // a SAMPLE tap: Sample Mode
    assert(a.mode == &SAMPLE_MODE);
    a.roll_zoom = 0;

    if (shot) {  // a drum-like stereo hit on the pages of the guide's screenshots, and the track grid
        load(0, 3, 30000, 0);
        struct pad *pd = &e.tracks[0].pads[0][3];
        snprintf(pd->smp->name, sizeof pd->smp->name, "BoomBap-Kik-Test-Long-Name");
        for (int i = 0; i < 30000; i++)
            pd->smp->lr[2 * i] = sinf(i * 0.05f) * expf(-i / 9000.0f), pd->smp->lr[2 * i + 1] = sinf(i * 0.047f) * expf(-i / 7000.0f);
        a.pad = 3, a.group = 0, a.page[0] = 0, a.sel = 2, a.popup[0] = 0, a.touched = -1;
        pd->p.loop_on = 1, pd->p.loop_lock = 0, pd->p.loop = 0.3f, pd->p.end = 0.9f, pd->p.pan = 0.3f;
        snap(shot, "trim");
        a.touched = 2, snap(shot, "trim-value"), a.touched = -1;
        a.shift = 1, snap(shot, "shift"), a.shift = 0;
        pd->p.loop_on = 0, pd->p.end = 1, a.page[0] = 2, a.sel = 1, pd->p.attack = 60, pd->p.decay = 95;
        snap(shot, "ampenv");
        pd->muted = 1, a.page[0] = 0, snap(shot, "muted"), pd->muted = 0;
        load(1, 0, 1000, 0.1f), e.tracks[2].type = TRACK_DRUM, e.tracks[1].type = TRACK_MIDI, e.tracks[3].type = TRACK_AUDIO;
        a.track_view = 1, a.track = 2, a.sel = 0, snap(shot, "tracks"), a.track_view = 0;
        a.track = 1, a.mode = &MIDI_MODE, a.pad = 5, a.pressed[9] = 100, snap(shot, "midi"), a.pressed[9] = 0;
        a.track = 3, a.mode = &AUDIO_MODE, snap(shot, "audio");
        a.track = 0, a.mode = &SAMPLE_MODE, a.meter = 0;
        sq.cur = 2, sq.pos = 2 * PPQ + 561, sq.rec_quant = 1, sq.next = 5;
        seq_add(&sq, 2, 0, 0, 0, 100), a.mode = &SEQ_MODE, a.sel = 0, snap(shot, "seq");
        sq.playing = sq.recording = 1, a.shift = 1, snap(shot, "seq-rec"), sq.playing = sq.recording = 0, a.shift = 0;
        a.confirm = 2, snap(shot, "seq-erase"), a.confirm = -1;
        e.tracks[2].muted = 1, a.mute_mode = 1, snap(shot, "trackmute"), a.mute_mode = 0;
        a.mode = &SAMPLE_MODE, a.repeat = 1, snap(shot, "repeat"), a.repeat = 0;
        seq_clear(&sq, 3), sq.cur = 3, sq.seqs[3].bars = 2, sq.q = 4, sq.next = -1;  // a two-bar beat in the piano roll
        for (int i = 0; i < 32; i++) {
            int ev;
            if (i % 8 == 0 || i == 10) ev = seq_add(&sq, 3, 0, i * 240, 0, 110), sq.ev[ev].len = 240;     // kick
            if (i % 8 == 4) ev = seq_add(&sq, 3, 0, i * 240, 1, 120), sq.ev[ev].len = 480;                // snare
            if (i % 2 == 0) ev = seq_add(&sq, 3, 0, i * 240, 2, 30 + i * 3), sq.ev[ev].len = 120;         // hats
            if (i % 16 == 14) ev = seq_add(&sq, 3, 0, i * 240, 3, 90), sq.ev[ev].len = 240;               // open hat
        }
        a.mode = &ROLL_MODE, a.track = 0, a.bank = 0, a.pad = 1, a.roll_tick = 12 * 240, a.roll_zoom = 1, a.sel = 2;
        a.roll_sel = seq_at(&sq, 3, 0, 12 * 240, 13 * 240, 1);
        snap(shot, "roll");
        sq.playing = 1, sq.pos = 5 * 240 + 100, snap(shot, "roll-play"), sq.playing = 0;
        a.confirm = 2, snap(shot, "roll-erase"), a.confirm = -1;
        e.tracks[1].type = TRACK_MIDI, a.track = 1, a.pad = 4, a.roll_tick = 0;
        int notes[8] = {0, 4, 7, 4, 2, 5, 9, 5};
        for (int i = 0; i < 8; i++) sq.ev[seq_add(&sq, 3, 1, i * 960, notes[i], 100)].len = 900;  // a slow MIDI line
        a.roll_sel = seq_at(&sq, 3, 1, 0, 240, 4), snap(shot, "roll-midi");
        e.tracks[1].type = TRACK_EMPTY, a.track = 0, a.mode = &SAMPLE_MODE, a.roll_sel = -1;
    }
    puts("app ok");
}

static void wav_tests(void) {
    unsigned char b[64] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x44\xac\0\0\x88\x58\x01\0\x02\0\x10\0data\x04\0\0\0\0\x40\0\xc0";
    const char *err = NULL;
    struct sample *w = wav_decode(b, 48, "t", &err);  // mono 16-bit: +0.5, -0.5 on both sides
    assert(w && w->frames == 2 && w->lr[0] == 0.5f && w->lr[1] == 0.5f && w->lr[2] == -0.5f);
    free(w);
    b[24] = 0x80, b[25] = 0xbb;  // 48000 Hz is refused
    assert(!wav_decode(b, 48, "t", &err) && strstr(err, "44100"));
    puts("wav ok");
}

static void params_tests(void) {  // every row: sane range, its ends stored and read back, text for each step
    static struct engine en;
    engine_init(&en), seq_init(&sq);
    for (int id = 1; id < P_COUNT; id++) {
        const struct param *p = &PARAMS[id];
        assert(p->name[0] && p->lo < p->hi && p->step > 0 && p->scale > 0);
        void *b = p->in == IN_TRACK ? (void *)&en.tracks[0] : p->in == IN_SEQ ? (void *)&sq.seqs[0] : p->in == IN_SQ ? (void *)&sq
                : p->in == IN_SEQTRACK ? (void *)&sq.seqs[0].trk[0] : p->in == IN_EVENT ? (void *)&sq.ev[0]
                : (void *)&en.tracks[0].pads[0][0].p;
        pad_defaults(&en.tracks[0].pads[0][0].p);
        en.tracks[0].pads[0][0].p.start = 0, en.tracks[0].pads[0][0].p.end = 1;
        for (float v = p->lo; v <= p->hi; v += p->step) {
            char buf[32];
            param_set(b, id, v);
            float got = param_get(b, id);
            assert(fabsf(got - ((p->is_int || p->whole) ? roundf(v) : v)) < 0.01f || id == P_START || id == P_END);
            param_text(id, got, buf, sizeof buf);
            assert(buf[0]);
        }
        param_set(b, id, p->hi + 1000), assert(fabsf(param_get(b, id) - p->hi) < 0.2f);
        param_set(b, id, p->lo - 1000), assert(fabsf(param_get(b, id) - p->lo) < 0.2f);
    }
    char buf[32];
    param_text(P_PAN, -12, buf, sizeof buf), assert(!strcmp(buf, "12L"));
    param_text(P_MONO, 1, buf, sizeof buf), assert(!strcmp(buf, "Mono"));
    param_text(P_VOL, VOL_MIN_DB, buf, sizeof buf), assert(!strcmp(buf, "-INF"));
    param_set(&sq.seqs[0], P_SEQBARS, 3), assert(sq.seqs[0].bars == 8);  // K1 steps the bars by powers of two
    param_set(&sq.seqs[0], P_SEQBARS1, 3), param_text(P_SEQBARS, param_get(&sq.seqs[0], P_SEQBARS), buf, sizeof buf);
    assert(!strcmp(buf, "3 Bars"));  // SHIFT+K1 sets any length, and K1 still shows it right
    param_text(P_TLEN, 0, buf, sizeof buf), assert(!strcmp(buf, "Seq"));
    puts("params ok");
}

static int run_blocks(int blocks, struct due *got, int max, int *at_block) {  // what the player gives over some blocks
    int n = 0;
    struct due d[64];
    for (int b = 0; b < blocks; b++) {
        int k = seq_advance(&sq, 128, d, 64);
        for (int i = 0; i < k && n < max; i++) got[n] = d[i], at_block[n++] = b;
    }
    return n;
}

static void seq_tests(void) {
    struct due d[256];
    int blk[256], n;
    seq_init(&sq);
    sq.bpm_global = 1, sq.bpm = 120, sq.metro = METRO_OFF;  // 120 BPM: a beat is 22050 frames
    assert(seq_len(&sq, 0) == 4 * 4 * PPQ && !seq_used(&sq, 0));
    seq_add(&sq, 0, 0, PPQ, 5, 100);  // beat 2 of track 1: on frame 22050 = block 172, 34 frames in
    seq_play(&sq, 1);
    n = run_blocks(200, d, 256, blk);
    assert(n == 1 && blk[0] == 172 && d[0].offset == 34 && d[0].track == 0 && d[0].key == 5 && d[0].vel == 100);

    seq_init(&sq);  // a track one beat long goes round 4 times in a bar; the 4-bar sequence plays its event once
    sq.bpm_global = 1, sq.bpm = 120, sq.metro = METRO_OFF;
    sq.seqs[0].trk[1].len_beats = 1;
    seq_add(&sq, 0, 1, 0, 7, 90), seq_add(&sq, 0, 2, 0, 9, 90);
    seq_play(&sq, 1);
    n = run_blocks(4 * 22050 / 128, d, 256, blk);
    int t1 = 0, t2 = 0;
    for (int i = 0; i < n; i++) t1 += d[i].track == 1, t2 += d[i].track == 2;
    assert(t1 == 4 && t2 == 1);

    seq_init(&sq);  // at the end the queued sequence takes over, from its start
    sq.bpm_global = 1, sq.bpm = 120, sq.metro = METRO_OFF;
    sq.seqs[0].bars = 1;
    seq_add(&sq, 1, 3, 0, 1, 64);
    seq_play(&sq, 1), sq.next = 1;
    n = run_blocks(4 * 22050 / 128 + 2, d, 256, blk);
    assert(n == 1 && sq.cur == 1 && sq.next == -1 && d[0].track == 3 && blk[0] == 4 * 22050 / 128);

    seq_init(&sq);  // metronome On: a click on each beat, the bar's first one accented
    sq.bpm_global = 1, sq.bpm = 120, sq.metro = METRO_ON;
    seq_play(&sq, 1);
    n = run_blocks(4 * 22050 / 128, d, 256, blk);  // 689 blocks end just before beat 5
    assert(n == 4 && d[0].track == -1 && d[0].key == 1 && d[1].key == 0 && blk[1] == 172);

    seq_init(&sq);  // recording: a count-in bar first, then quantized hits; a hit ahead of the player is not doubled
    sq.bpm_global = 1, sq.bpm = 120, sq.metro = METRO_OFF, sq.rec_quant = 1, sq.armed = 1;
    seq_play(&sq, 1);
    assert(sq.recording && sq.count == 4 * PPQ && seq_rec(&sq, 0, sq.pos, 3, 100) < 0);
    run_blocks(4 * 22050 / 128 + 1, d, 256, blk);
    assert(sq.count == 0 && sq.pos == 0);
    run_blocks(160, d, 256, blk);  // just before beat 2 (block 172): 1/16 quantize moves the hit onto it
    int e = seq_rec(&sq, 0, sq.pos, 3, 100);
    assert(e >= 0 && sq.ev[e].tick == PPQ && sq.ev[e].skip);
    n = run_blocks(4 * 4 * 22050 / 128 + 40, d, 256, blk);  // once round and a bit: it plays only on the next pass
    assert(n == 1 && d[0].key == 3 && blk[0] > 4 * 22050 / 128);
    assert(seq_undo(&sq) == 1 && !seq_used(&sq, 0));

    seq_init(&sq);  // RT Swing 66% at 1/16: the second 16th comes 0.32 of a 16th late
    sq.bpm_global = 1, sq.bpm = 120, sq.metro = METRO_OFF, sq.swing = 66;
    seq_add(&sq, 0, 0, 240, 1, 100), seq_add(&sq, 0, 0, 480, 2, 100);
    seq_play(&sq, 1);
    n = run_blocks(200, d, 256, blk);
    double tpf = 120.0 * PPQ / (60 * RATE);
    assert(n == 2 && fabs(blk[0] * 128 + d[0].offset - (240 + 0.32 * 240) / tpf) < 2 && fabs(blk[1] * 128 + d[1].offset - 480 / tpf) < 2);

    seq_init(&sq);  // erase and clear give events back to the pool
    int a1 = seq_add(&sq, 5, 0, 10, 1, 1);
    seq_add(&sq, 5, 1, 20, 1, 1);
    seq_erase(&sq, 5, 0, a1);
    assert(sq.seqs[5].trk[0].head < 0 && seq_used(&sq, 5) && sq.free_list == a1);
    seq_clear(&sq, 5);
    assert(!seq_used(&sq, 5));
    puts("seq ok");
}

static struct queue tq;
static void *producer(void *arg) {
    (void)arg;
    for (int i = 0; i < 100000;)
        if (q_push(&tq, &(struct job){.bank = i})) i++;
    return NULL;
}

static void queue_tests(void) {
    static struct queue q;
    struct job j;
    assert(!q_pop(&q, &j));
    for (int round = 0; round < 3; round++) {  // fill, refuse when full, drain in order; again past the wrap
        for (int i = 0; i < QSIZE; i++) assert(q_push(&q, &(struct job){.pad = i}));
        assert(!q_push(&q, &(struct job){0}));
        for (int i = 0; i < QSIZE; i++) assert(q_pop(&q, &j) && j.pad == i);
        assert(!q_pop(&q, &j));
    }
    pthread_t t;  // one writer, one reader on two threads: nothing lost, order kept
    assert(!pthread_create(&t, NULL, producer, NULL));
    for (int i = 0; i < 100000;)
        if (q_pop(&tq, &j)) assert(j.bank == i++);
    pthread_join(t, NULL);
    puts("queue ok");
}

static void put(const char *dir, const char *name, const unsigned char *b, size_t n) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(b, 1, n, f) == n);
    fclose(f);
}

static void files_tests(void) {  // the file thread loads a folder in name order and says when it is done
    unsigned char wav[64] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x44\xac\0\0\x88\x58\x01\0\x02\0\x10\0data\x04\0\0\0\0\x40\0\xc0";
    char dir[256];
    const char *tmp = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/mpcfw-test-XXXXXX", tmp ? tmp : "/tmp");
    assert(mkdtemp(dir));
    put(dir, "b kick.wav", wav, 48), put(dir, "a snare.WAV", wav, 48), put(dir, "notes.txt", wav, 4);
    wav[24] = 0x80, wav[25] = 0xbb;
    put(dir, "c bad.wav", wav, 48);  // 48 kHz: skipped, its pad stays empty
    static struct files f;
    assert(!files_start(&f, dir));
    struct job got[4], j;
    int n = 0;
    for (int ms = 0; ms < 3000 && (n == 0 || got[n - 1].type != JOB_DONE); ms++) {
        while (n < 4 && q_pop(&f.res, &j)) got[n++] = j;
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    files_stop(&f);
    assert(n == 3 && got[2].type == JOB_DONE && got[2].value == 2);
    assert(got[0].type == JOB_LOADED && got[0].pad == 0 && !strcmp(got[0].s->name, "a snare") && got[0].s->frames == 2);
    assert(got[1].pad == 1 && !strcmp(got[1].s->name, "b kick"));
    free(got[0].s), free(got[1].s);
    const char *names[] = {"a snare.WAV", "b kick.wav", "c bad.wav", "notes.txt"};
    for (int i = 0; i < 4; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        unlink(path);
    }
    rmdir(dir);
    puts("files ok");
}

int main(int argc, char **argv) {
    engine_tests();
    app_tests(argc > 1 ? argv[1] : NULL);
    wav_tests();
    seq_tests();
    params_tests();
    queue_tests();
    files_tests();
    puts("selftest ok");
    return 0;
}
