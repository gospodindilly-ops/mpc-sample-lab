// Sample Mode page, see ui.h. Layout: B1-B3 tabs right under their buttons, header (pad, sample name, flags),
// waveform with Start/End/Loop markers (and the amp envelope on AMP ENV), three knob parameters (the encoder's
// one framed), and a popup for short messages.
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"

enum { BG = 0x101014, HEADER = 0x22303c, TEXT = 0xe8e8e8, DIM = 0x787878, WAVE = 0x4fa3e0, WAVE_OUT = 0x27465c,
       START = 0x40d040, END = 0xe04040, LOOP = 0xe0c040, SEL = 0xffb000, BOX = 0x303840, POP = 0x34404e,
       ENV = 0xffffff };

static void fill(uint32_t *fb, int stride, int x0, int y0, int w, int h, uint32_t c) {
    for (int y = y0 < 0 ? 0 : y0; y < y0 + h && y < UI_H; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x0 + w && x < UI_W; x++) fb[y * stride + x] = c;
}

// Draws text clipped at x_max; returns the x after the last glyph. big: 8x16 font, else 6x12.
static int text(uint32_t *fb, int stride, int x, int y, const char *s, int big, uint32_t c, int x_max) {
    int w = big ? FONT_L_W : FONT_S_W, h = big ? FONT_L_H : FONT_S_H;
    for (; *s && x + w <= x_max; s++, x += w) {
        int ch = (unsigned char)*s;
        const unsigned char *g = big ? FONT_L[(ch < 32 || ch > 126 ? '?' : ch) - 32] : FONT_S[(ch < 32 || ch > 126 ? '?' : ch) - 32];
        for (int r = 0; r < h; r++)
            for (int col = 0; col < w; col++)
                if (g[r] & (0x80 >> col) && y + r >= 0 && y + r < UI_H) fb[(y + r) * stride + x + col] = c;
    }
    return x;
}

static void vline(uint32_t *fb, int stride, int x, int y0, int y1, uint32_t c) {
    if (x < 0 || x >= UI_W) return;
    for (int y = y0; y <= y1; y++) fb[y * stride + x] = c;
}

// Amp envelope gain at sample frame pos of the play range [lo, hi), with the engine's linear stages. A Note On
// release depends on when the pad is lifted, so only its attack and sustain are drawn.
static float env_gain(const struct pad_params *p, double pos, double lo, double hi) {
    double step = pow(2.0, (p->semi + p->fine / 100.0) / 12.0);  // sample frames per output frame
    double t = (p->reverse ? hi - pos : pos - lo) / step, left = (p->reverse ? pos - lo : hi - pos) / step;
    double a = env_seconds(p->attack) * RATE, d = env_seconds(p->decay) * RATE;
    double g = a < 1 || t >= a ? 1 : t / a;
    if (p->note_on) return (float)g;
    if (p->decay_from == DECAY_FROM_START) {
        double td = t - (a < 1 ? 0 : a);
        if (td > 0) g = d < 1 || td >= d ? 0 : 1 - td / d;
    } else if (!p->loop_on && left < d) g *= left / d;
    return (float)g;
}

// Per-column peaks of the shown sample. A long sample takes ~1 ms to scan, so it is rescanned only when the
// buffer, its length or its contents (gen) change; ui_free_retired drops it, since a freed address can return.
static struct { const float *lr; int frames; unsigned gen; float lo[UI_W], hi[UI_W]; } peaks;

// env: shape the waveform by the amp envelope and draw the envelope over it (full gain at the top).
static void waveform(const struct pad *pd, uint32_t *fb, int stride, int top, int height, int env) {
    const float *lr = __atomic_load_n(&pd->smp.lr, __ATOMIC_ACQUIRE);  // trim stores the length first
    int frames = pd->smp.frames;
    if (!lr) return;
    if (lr != peaks.lr || frames != peaks.frames || pd->smp.gen != peaks.gen) {
        peaks.lr = lr, peaks.frames = frames, peaks.gen = pd->smp.gen;
        for (int x = 0; x < UI_W; x++) {
            long a = (long)x * frames / UI_W, b = (long)(x + 1) * frames / UI_W;
            float lo = 0, hi = 0;
            for (long i = a; i < (b > a ? b : a + 1) && i < frames; i++) {
                float v = (lr[2 * i] + lr[2 * i + 1]) / 2;
                lo = v < lo ? v : lo, hi = v > hi ? v : hi;
            }
            peaks.lo[x] = lo, peaks.hi[x] = hi;
        }
    }
    const struct pad_params *p = &pd->p;
    int mid = top + height / 2, half = height / 2 - 1, bottom = top + height - 1, prev = -1;
    int xs = (int)(p->start * UI_W), xe = (int)(p->end * UI_W);
    for (int x = 0; x < UI_W; x++) {
        int in = x >= xs && x < xe;
        float g = env && in ? env_gain(p, (x + 0.5) * frames / UI_W, p->start * frames, p->end * frames) : 1;
        int y0 = mid - (int)(peaks.hi[x] * g * half), y1 = mid - (int)(peaks.lo[x] * g * half);
        vline(fb, stride, x, y0 < top ? top : y0, y1 > bottom ? bottom : y1, in ? WAVE : WAVE_OUT);
        if (env && in) {  // joined to the previous column, so steep attacks stay visible
            int y = bottom - (int)(g * (height - 1));
            vline(fb, stride, x, prev < 0 || y < prev ? y : prev, prev > y ? prev : y, ENV);
            prev = y;
        }
    }
    if (p->loop_on) vline(fb, stride, (int)((p->loop_lock ? p->start : p->loop) * UI_W), top, bottom, LOOP);
    vline(fb, stride, xs, top, bottom, START);
    vline(fb, stride, xe - 1, top, bottom, END);
}

void ui_draw(const struct app *a, uint32_t *fb, int stride) {
    const struct pad *pd = &a->e->pads[a->bank][a->pad];
    char buf[64];
    fill(fb, stride, 0, 0, UI_W, UI_H, BG);

    for (int g = 0; g < 3; g++) {  // tabs under B1..B3: each shows its group's current page, the active one lit
        int x = g * 107 + 1, on = g == a->group;
        const char *name = page_name(a, g);
        fill(fb, stride, x, 0, 105, 18, on ? SEL : BOX);
        text(fb, stride, x + (105 - (int)strlen(name) * FONT_L_W) / 2, 1, name, 1, on ? BG : TEXT, x + 105);
    }

    fill(fb, stride, 0, 20, UI_W, 20, HEADER);  // header: pad, sample name, mode flags
    snprintf(buf, sizeof buf, "%c%02d", 'A' + a->bank, a->pad + 1);
    text(fb, stride, 4, 22, buf, 1, SEL, UI_W);
    snprintf(buf, sizeof buf, "%s%s%s%s%s%s", a->shift ? "SHIFT " : "", a->e->full_level ? "FULL " : "",
             pd->p.note_on ? "N" : "", pd->p.loop_on ? "L" : "", pd->p.reverse ? "R" : "", pd->muted ? "M" : "");
    int fx = UI_W - 4 - (int)strlen(buf) * FONT_S_W;
    text(fb, stride, 36, 22, pd->smp.lr ? pd->smp.name : "(empty)", 1, pd->smp.lr ? TEXT : DIM, fx - 4);
    text(fb, stride, fx, 25, buf, 0, a->shift ? SEL : LOOP, UI_W);

    if (pd->smp.lr) waveform(pd, fb, stride, 43, 128, page_param(a, 0) == P_ATTACK);
    else text(fb, stride, 120, 99, "Empty pad", 1, DIM, UI_W);

    for (int k = 0; k < 3; k++) {  // K1..K3
        int x = k * 107, id = page_param(a, k);
        fill(fb, stride, x + 1, 176, 104, 62, k == a->sel ? SEL : BOX);
        fill(fb, stride, x + 3, 178, 100, 58, BG);
        text(fb, stride, x + 7, 182, id ? param_name(a, id) : "-", 0, DIM, x + 104);
        if (id && pd->smp.lr) param_format(a, id, buf, sizeof buf);
        else snprintf(buf, sizeof buf, "-");
        text(fb, stride, x + 7, 199, buf, 1, TEXT, x + 104);
        if ((id == P_ATTACK || id == P_DECAY) && pd->smp.lr) {  // envelope stages in time as well
            float sec = env_seconds(id == P_ATTACK ? pd->p.attack : pd->p.note_on ? pd->p.release : pd->p.decay);
            snprintf(buf, sizeof buf, sec < 1 ? "%.0f ms" : "%.2f s", sec < 1 ? sec * 1000 : sec);
            text(fb, stride, x + 7, 220, buf, 0, DIM, x + 104);
        }
    }

    if (a->popup[0]) {
        int w = (int)strlen(a->popup) * FONT_L_W + 24;
        w = w > UI_W - 16 ? UI_W - 16 : w;
        int x = (UI_W - w) / 2;
        fill(fb, stride, x, 92, w, 32, SEL);
        fill(fb, stride, x + 2, 94, w - 4, 28, POP);
        text(fb, stride, x + 12, 100, a->popup, 1, TEXT, x + w - 8);
    }
}

int ui_changes(const uint32_t *back, uint32_t *front, int rect[UI_H][4]) {
    int n = 0;
    for (int y = 0; y < UI_H;) {
        int y0 = y, x0 = UI_W, x1 = 0;
        for (; y < UI_H; y++) {  // a band: consecutive changed rows
            const uint32_t *b = back + y * UI_W, *f = front + y * UI_W;
            int l = 0, r = UI_W;
            while (l < UI_W && b[l] == f[l]) l++;
            if (l == UI_W) break;
            while (b[r - 1] == f[r - 1]) r--;
            x0 = l < x0 ? l : x0, x1 = r > x1 ? r : x1;
        }
        if (y == y0) { y++; continue; }  // an unchanged row
        for (int row = y0; row < y; row++) memcpy(front + row * UI_W + x0, back + row * UI_W + x0, (x1 - x0) * 4);
        rect[n][0] = x0, rect[n][1] = y0, rect[n][2] = x1, rect[n][3] = y, n++;
    }
    return n;
}

void ui_free_retired(struct app *a, unsigned upto) {
    if (a->grave_freed == upto) return;
    for (unsigned i = a->grave_freed; i != upto; i++) free(a->grave[i % GRAVE]);
    peaks.lr = NULL;
    __atomic_store_n(&a->grave_freed, upto, __ATOMIC_RELEASE);
}
