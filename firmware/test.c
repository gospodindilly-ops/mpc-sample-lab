// Host self-test of the engine, the Sample Mode controller and the screen (no device needed).
// Build and run: make test  (optional argument: path of a raw 320x240 RGB screenshot to write)
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "ui.h"
#include "wav.h"

#define F 4096
static struct engine e;
static struct app a;
static int32_t out[2 * F];

static void load(int bank, int pad, int frames, float value) {  // constant-level sample: easy to reason about
    struct sample *s = &e.pads[bank][pad].smp;
    s->lr = malloc(sizeof(float) * 2 * frames), s->frames = frames;
    for (int i = 0; i < 2 * frames; i++) s->lr[i] = value;
    snprintf(s->name, sizeof s->name, "test %c%02d", 'A' + bank, pad + 1);
}

static float level(int f, int ch) { return out[2 * f + ch] / 2147483520.0f; }
static int voices(void) { int n = 0; for (int i = 0; i < VOICES; i++) n += e.voices[i].active; return n; }
static void midi(int s, int d1, int d2) { unsigned char m[3] = {s, d1, d2}; app_midi(&a, m); }
static void press(int note) { midi(0x90, note, 127), midi(0x80, note, 0); }

static void engine_tests(void) {
    engine_init(&e);
    e.master = 1;
    load(0, 0, 1000, 0.5f);
    engine_render(&e, out, 8);
    assert(out[0] == 0 && voices() == 0);

    engine_trigger(&e, 0, 0, 127);  // one-shot plays to the end, fading over the last TAIL frames
    engine_render(&e, out, 1100);
    assert(fabsf(level(10, 0) - (0.5f - 4 / 27.0f * 0.125f)) < 1e-4f);  // soft clip is slightly below unity at 0.5
    assert(level(990, 0) < level(500, 0) && out[2 * 1050] == 0 && voices() == 0);

    e.pads[0][0].p.vol_db = VOL_MIN_DB;  // -INF
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 100);
    assert(out[2 * 50] == 0);
    e.pads[0][0].p.vol_db = 0;

    e.pads[0][0].p.vel_sens = 0;  // velocity ignored
    engine_trigger(&e, 0, 0, 1), engine_render(&e, out, 100);
    float full = level(50, 0);
    engine_stop_all(&e), engine_render(&e, out, 300);
    e.pads[0][0].p.vel_sens = 127;
    engine_trigger(&e, 0, 0, 64), engine_render(&e, out, 100);
    assert(full > 0.4f && level(50, 0) < full * 0.6f);
    engine_stop_all(&e), engine_render(&e, out, 300);

    e.pads[0][0].p.pan = -1;  // hard left: right side silent
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 100);
    assert(level(50, 0) > 0.4f && out[2 * 50 + 1] == 0);
    engine_stop_all(&e), engine_render(&e, out, 300);
    e.pads[0][0].p.pan = 0;

    e.pads[0][0].p.mono = 1;  // mono retrigger: the old voice fades out (no click), the new one starts
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 100);
    engine_trigger(&e, 0, 0, 127);
    assert(voices() == 2);
    engine_render(&e, out, 300);
    assert(voices() == 1);
    engine_stop_all(&e), engine_render(&e, out, 300);

    load(0, 1, 1000, 0.25f);  // mute group: a hit on another pad of the group fades the first one
    e.pads[0][0].p.mute_group = e.pads[0][1].p.mute_group = 3;
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 50);
    engine_trigger(&e, 0, 1, 127), engine_render(&e, out, 300);
    assert(voices() == 1 && e.voices[0].active + e.voices[1].active == 1 && engine_playing(&e, 0, 1));
    engine_stop_all(&e), engine_render(&e, out, 1100);

    e.pads[0][0].p.semi = 12;  // an octave up plays the 1000 frames in ~500
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 520);
    assert(voices() == 0);
    e.pads[0][0].p.semi = 0;

    e.pads[0][0].p.loop_on = 1;  // one-shot loop: keeps going past the end, a second hit stops it
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 3000);
    assert(voices() == 1 && level(2500, 0) > 0.4f);
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 300);
    assert(voices() == 0);
    e.pads[0][0].p.loop_on = 0;

    e.pads[0][0].p.note_on = 1, e.pads[0][0].p.release = 0;  // note on: stops shortly after the pad is lifted
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 200);
    engine_release(&e, 0, 0), engine_render(&e, out, 100);
    assert(voices() == 0);
    e.pads[0][0].p.note_on = 0;

    struct sample *s = &e.pads[0][2].smp;  // reverse starts at the end of the range
    load(0, 2, 100, 0);
    for (int i = 0; i < 100; i++) s->lr[2 * i] = s->lr[2 * i + 1] = i / 200.0f;
    e.pads[0][2].p.reverse = 1;
    engine_trigger(&e, 0, 2, 127), engine_render(&e, out, 4);
    assert(level(0, 0) > level(3, 0) && level(0, 0) > 0.45f);
    engine_render(&e, out, 200);

    e.pads[0][0].p.loop_on = 1;  // MAIN VOLUME glides over ~10 ms instead of stepping: knob moves do not crackle
    engine_trigger(&e, 0, 0, 127), engine_render(&e, out, 100);
    e.master = 0, engine_render(&e, out, 2000);
    assert(level(0, 0) > 0.45f && level(200, 0) > 0.2f && level(200, 0) < 0.4f && level(1999, 0) < 0.01f);
    e.master = 1, engine_stop_all(&e), engine_render(&e, out, 4000);
    e.pads[0][0].p.loop_on = 0;

    e.master = 1, e.kit_db = 6;  // many loud voices clip softly instead of wrapping
    for (int i = 3; i < 16; i++) load(0, i, 1000, 1.0f), engine_trigger(&e, 0, i, 127);
    engine_render(&e, out, 10);
    assert(level(5, 0) > 0.99f && level(5, 0) <= 1.0f);
    for (int b = 0; b < BANKS; b++)
        for (int i = 0; i < PADS; i++) free(e.pads[b][i].smp.lr), e.pads[b][i].smp.lr = NULL;
    puts("engine ok");
}

static int sent(int status, int d1, int d2) {  // did app_tick emit this 3-byte message?
    for (int i = 0; i + 2 < a.out_len; i++)
        if (a.out[i] == status && a.out[i + 1] == d1 && a.out[i + 2] == d2) return 1;
    return 0;
}

static void app_tests(const char *shot) {
    engine_init(&e);
    app_init(&a, &e);
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
    assert(a.pad == 1 && engine_playing(&e, 0, 1));
    midi(0x89, 36, 0);

    midi(0x90, 49, 127), press(74), midi(0x80, 49, 0);  // SHIFT+LOOP: reverse; LOOP: loop
    press(74);
    assert(e.pads[0][1].p.reverse && e.pads[0][1].p.loop_on);

    press(43);  // MUTE mode: pad 2 mutes, a second MUTE press leaves the mode
    midi(0x99, 36, 100), midi(0x89, 36, 0);
    assert(e.pads[0][1].muted && a.mute_mode);
    press(43);
    assert(!a.mute_mode);
    midi(0x90, 49, 127), press(43), midi(0x80, 49, 0);  // SHIFT+MUTE: unmute all
    assert(!e.pads[0][1].muted);

    a.page[0] = 0, a.group = 0;  // TRIM page: K2 = End, scaled takeover never jumps and reaches the end with the knob
    midi(0xB0, 17, 30);
    assert(e.pads[0][1].p.end == 1);  // the first move only calibrates
    midi(0xB0, 17, 20);
    float end1 = e.pads[0][1].p.end;
    midi(0xB0, 17, 0);
    assert(end1 < 1 && end1 > 0.6f && e.pads[0][1].p.end <= e.pads[0][1].p.start + 0.002f);
    midi(0xB0, 17, 127);
    assert(e.pads[0][1].p.end == 1);

    press(12);  // B1 again: TRIM -> MIX; SHIFT+K2 is Kit Volume
    assert(a.page[0] == 1 && page_param(&a, 0) == P_VOL);
    midi(0x90, 49, 127);
    assert(page_param(&a, 1) == P_KITVOL);
    midi(0x80, 49, 0);

    midi(0xB0, 20, 127);  // fader CC20 (Pad Volume, Pickup): does nothing until it crosses the current value
    float vol = e.pads[0][1].p.vol_db;
    midi(0xB0, 20, 120);
    assert(e.pads[0][1].p.vol_db == vol);
    midi(0xB0, 20, 0), midi(0xB0, 20, 0);
    assert(e.pads[0][1].p.vol_db == VOL_MIN_DB);

    a.sel = 2;  // encoder CC100, relative like the stock parser reads it: K3 (Pan) +5, then -1
    midi(0xB0, 100, 5);
    assert(fabsf(e.pads[0][1].p.pan - 0.1f) < 1e-6f);
    midi(0xB0, 100, 127);
    assert(fabsf(e.pads[0][1].p.pan - 0.08f) < 1e-6f);

    float *old = e.pads[0][1].smp.lr;  // ERASE + pad: empty pad; the buffer waits for the UI thread
    midi(0x90, 9, 127), midi(0x99, 36, 100), midi(0x89, 36, 0), midi(0x80, 9, 0);
    assert(!e.pads[0][1].smp.lr && a.grave_n == 1 && a.grave[0] == old);
    ui_free_retired(&a, a.grave_n);
    assert(a.grave_freed == 1);

    midi(0x99, 37, 100), midi(0x89, 37, 0);  // SHIFT+pad 13: trim A01 to Start..End
    e.pads[0][0].p.start = 0.25f, e.pads[0][0].p.end = 0.75f;
    midi(0x90, 49, 127), midi(0x99, 49, 100), midi(0x89, 49, 0), midi(0x80, 49, 0);
    assert(e.pads[0][0].smp.frames == 22050 && e.pads[0][0].p.start == 0 && e.pads[0][0].p.end == 1);
    ui_free_retired(&a, a.grave_n);

    static uint32_t fb[UI_W * UI_H];  // the screen draws, and the header shows the selected pad's name
    a.popup[0] = 0;
    ui_draw(&a, fb, UI_W);
    int lit = 0;
    for (int i = UI_W * 20; i < UI_W * 40; i++) lit += fb[i] != 0x22303c;
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

    load(0, 2, 44100, 0.1f);  // the cached waveform follows an in-place change (normalize)
    a.pad = 2;
    ui_draw(&a, fb, UI_W);
    sample_scale(&e.pads[0][2].smp, 5);
    ui_draw(&a, front, UI_W);
    assert(memcmp(fb + 30 * UI_W, front + 30 * UI_W, 100 * UI_W * 4));
    a.pad = 0;

    a.group = 0, a.page[0] = 2;  // AMP ENV: a slow attack shrinks the drawn waveform and adds the envelope line
    ui_draw(&a, front, UI_W);
    e.pads[0][0].p.attack = 100;
    ui_draw(&a, fb, UI_W);
    int wave_flat = 0, wave_env = 0, env_px = 0;
    for (int i = 43 * UI_W; i < 171 * UI_W; i++) wave_flat += front[i] == 0x4fa3e0, wave_env += fb[i] == 0x4fa3e0, env_px += fb[i] == 0xffffff;
    assert(wave_env < wave_flat / 2 && env_px >= UI_W);
    e.pads[0][0].p.attack = 0;

    if (shot) {  // a drum-like hit on the AMP ENV page: slow attack, decay from the end
        load(0, 3, 30000, 0);
        for (int i = 0; i < 30000; i++) e.pads[0][3].smp.lr[2 * i] = e.pads[0][3].smp.lr[2 * i + 1] = sinf(i * 0.05f) * expf(-i / 9000.0f);
        a.pad = 3, a.sel = 1, e.pads[0][3].p.attack = 60, e.pads[0][3].p.decay = 95;
        ui_draw(&a, fb, UI_W);
        FILE *f = fopen(shot, "wb");
        for (int i = 0; f && i < UI_W * UI_H; i++) fputc(fb[i] >> 16, f), fputc(fb[i] >> 8, f), fputc(fb[i], f);
        if (f) fclose(f), printf("screenshot: %s\n", shot);
    }
    puts("app ok");
}

static void wav_tests(void) {
    unsigned char b[64] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x44\xac\0\0\x88\x58\x01\0\x02\0\x10\0data\x04\0\0\0\0\x40\0\xc0";
    int frames;
    const char *err = NULL;
    float *w = wav_decode(b, 48, &frames, &err);  // mono 16-bit: +0.5, -0.5 on both sides
    assert(w && frames == 2 && w[0] == 0.5f && w[1] == 0.5f && w[2] == -0.5f);
    free(w);
    b[24] = 0x80, b[25] = 0xbb;  // 48000 Hz is refused
    assert(!wav_decode(b, 48, &frames, &err) && strstr(err, "44100"));
    puts("wav ok");
}

int main(int argc, char **argv) {
    engine_tests();
    app_tests(argc > 1 ? argv[1] : NULL);
    wav_tests();
    puts("selftest ok");
    return 0;
}
