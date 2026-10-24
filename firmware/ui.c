// Screen widgets and the 16-track view, see ui.h. Every position is measured from the guide's screenshots scaled
// to the panel's 320x240 (they are printed at ~1.9x); the 8x16 Terminus font has the guide's 10 px capitals.
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"

// ---- primitives ---------------------------------------------------------------------------------------

void ui_fill(uint32_t *fb, int stride, int x0, int y0, int w, int h, uint32_t c) {
    for (int y = y0 < 0 ? 0 : y0; y < y0 + h && y < UI_H; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x0 + w && x < UI_W; x++) fb[y * stride + x] = c;
}

// Returns the x after the last glyph. big: 8x16 font, else 6x12.
static int bit(const unsigned char *g, int r, int col) { return r >= 0 && r < FONT_L_H && col >= 0 && col < 8 && g[r] & (0x80 >> col); }

// 2x with Scale2x (EPX): each pixel becomes four, and a corner of the four takes its neighbours' colour where two of
// them agree, so curves and diagonals come out smooth instead of in doubled steps.
static void glyph2x(uint32_t *fb, int stride, int x, int y, const unsigned char *g, uint32_t c) {
    for (int r = 0; r < FONT_L_H; r++)
        for (int col = 0; col < FONT_L_W; col++) {
            int p = bit(g, r, col), A = bit(g, r - 1, col), B = bit(g, r, col + 1), C = bit(g, r, col - 1), D = bit(g, r + 1, col);
            int e[4] = {C == A && C != D && A != B ? A : p, A == B && A != C && B != D ? B : p,
                        D == C && D != B && C != A ? C : p, B == D && B != A && D != C ? D : p};
            for (int q = 0; q < 4; q++) {
                int px = x + col * 2 + (q & 1), py = y + r * 2 + (q >> 1);
                if (e[q] && px < UI_W && py >= 0 && py < UI_H) fb[py * stride + px] = c;
            }
        }
}

int ui_text(uint32_t *fb, int stride, int x, int y, const char *s, int big, uint32_t c, int x_max) {
    int w = big ? FONT_L_W : FONT_S_W, h = big ? FONT_L_H : FONT_S_H;
    if (big == 2) w *= 2;
    for (; *s && x + w <= x_max; s++, x += w) {
        int ch = (unsigned char)*s;
        const unsigned char *g = big ? FONT_L[(ch < 32 || ch > 126 ? '?' : ch) - 32] : FONT_S[(ch < 32 || ch > 126 ? '?' : ch) - 32];
        if (big == 2) { glyph2x(fb, stride, x, y, g, c); continue; }
        for (int r = 0; r < h; r++)
            for (int col = 0; col < w; col++)
                if (g[r] & (0x80 >> col) && y + r >= 0 && y + r < UI_H) fb[(y + r) * stride + x + col] = c;
    }
    return x;
}

void ui_text_fit(uint32_t *fb, int stride, int x, int y, const char *s, int big, uint32_t c, int x_max) {
    int room = (x_max - x) / (big ? FONT_L_W : FONT_S_W);
    char buf[80];
    if ((int)strlen(s) > room && room > 3) snprintf(buf, sizeof buf, "%.*s...", room - 3, s), s = buf;
    ui_text(fb, stride, x, y, s, big, c, x_max);
}

static void vline(uint32_t *fb, int stride, int x, int y0, int y1, uint32_t c) {
    if (x < 0 || x >= UI_W) return;
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < UI_H; y++) fb[y * stride + x] = c;
}

void ui_frame(uint32_t *fb, int stride, int x, int y, int w, int h, uint32_t c) {
    ui_fill(fb, stride, x, y, w, 1, c), ui_fill(fb, stride, x, y + h - 1, w, 1, c);
    vline(fb, stride, x, y, y + h - 1, c), vline(fb, stride, x + w - 1, y, y + h - 1, c);
}

// Labels of the piano roll's 9 px rows: a 3x5 font of just the characters they use (pads A01, notes C#1, C-1).
void ui_tiny(uint32_t *fb, int stride, int x, int y, const char *s, uint32_t c) {
    static const char CH[] = "0123456789ABCDEFGH#-";
    static const unsigned char G[20][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, {5, 5, 7, 1, 1},
                                           {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
                                           {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7},
                                           {7, 4, 6, 4, 4}, {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {5, 7, 5, 7, 5}, {0, 0, 7, 0, 0}};
    for (; *s; s++, x += 4) {
        const char *p = strchr(CH, *s);
        for (int r = 0; p && r < 5; r++)
            for (int col = 0; col < 3; col++)
                if (G[p - CH][r] >> (2 - col) & 1 && x + col < UI_W && y + r < UI_H) fb[(y + r) * stride + x + col] = c;
    }
}

static void glyph(uint32_t *fb, int stride, int x, int y, const char *const rows[13], uint32_t c) {
    for (int r = 0; r < 13 && rows[r]; r++)
        for (int i = 0; rows[r][i]; i++)
            if (rows[r][i] == '#' && y + r < UI_H && x + i < UI_W) fb[(y + r) * stride + x + i] = c;
}

static const char *const GLYPHS[][13] = {
    [ICON_NOTE] = {"......##...", "......#.#..", "......#..#.", "......#....", "......#....", "......#....",
                   "...####....", "..#####....", "...###....."},
    [ICON_REVERSE] = {"", "", "...#.......", "..##.......", ".#########.", "..##.......", "...#......."},
    [ICON_LOOP] = {"...#####...", "..#.....#..", ".#.......#.", ".#......###", ".#.......#.", ".#.........",
                   "..#.....#..", "...#####..."},
    [ICON_METRO] = {"....###....", "....#.#....", "...#...#...", "...#..##...", "..#..##.#..", "..#.##..#..",
                    ".#.##....#.", ".###.....#.", "###.......#", "#.........#", "###########"},
    [ICON_LOCK] = {"...####...", "..##..##..", ".##....##.", ".##....##.", ".##....##.", "##########", "##########",
                   "####..####", "####..####", "####..####", "##########", "##########", "##########"},
};

// 23x20 box as on the guide's pad header; ICON_LOCK is a bare 10x13 glyph.
void ui_icon(uint32_t *fb, int stride, int x, int y, int icon, int on, uint32_t on_colour) {
    if (icon == ICON_LOCK || icon == ICON_METRO) { glyph(fb, stride, x, y, GLYPHS[icon], on_colour); return; }
    uint32_t fg = on ? C_BG : C_DIM;
    if (on) ui_fill(fb, stride, x, y, 23, 20, on_colour);
    else ui_frame(fb, stride, x, y, 23, 20, C_DIM);
    if (icon == ICON_16) ui_text(fb, stride, x + 4, y + 2, "16", 1, fg, UI_W);
    else glyph(fb, stride, x + 6, y + 6, GLYPHS[icon], fg);
}

// ---- tabs and knob row --------------------------------------------------------------------------------

void ui_tabs(uint32_t *fb, int stride, const struct tab t[3]) {
    for (int g = 0; g < 3; g++) {
        int c = 55 + g * 104, tw = (int)strlen(t[g].label) * FONT_L_W;
        if (t[g].on) {  // a white box, its corners cut
            ui_fill(fb, stride, c - 42, 13, 85, 19, C_TEXT);
            fb[13 * stride + c - 42] = fb[13 * stride + c + 42] = fb[31 * stride + c - 42] = fb[31 * stride + c + 42] = C_BG;
        }
        ui_text(fb, stride, c - tw / 2, 16, t[g].label, 1, t[g].on ? C_BG : C_TEXT, c + 52);
        if (t[g].pages < 2) continue;
        int w = t[g].pages * 10 - 3 + (t[g].on ? 6 : 0), x = c - w / 2;  // a dash per page, the shown one long and white
        for (int i = 0; i < t[g].pages; i++) {
            int lit = t[g].on && i == t[g].page, dw = lit ? 13 : 7;
            ui_fill(fb, stride, x, 36, dw, 3, lit ? C_TEXT : C_LINE);
            x += dw + 3;
        }
    }
}

void ui_page_tabs(const struct app *a, struct tab t[3]) {
    static const char *const DIV[6] = {"1/8", "1/16", "1/32", "1/8T", "1/16T", "1/32T"};
    if (a->repeat) {  // Note Repeat: B1..B3 are its divisions, the triplets while SHIFT is held (the guide)
        for (int g = 0; g < 3; g++) t[g] = (struct tab){DIV[g + (a->shift ? 3 : 0)], a->repeat_div == g + (a->shift ? 3 : 0), 0, 0};
        return;
    }
    for (int g = 0; g < 3; g++) t[g] = (struct tab){page_name(a, g), g == a->group && a->mode->npages[g], a->mode->npages[g], a->page[g]};
}

// Name (or, right after a change, the value) over a bar: filled for a level, a marker for a centred value
// (pan, tune), segments for a choice. The encoder's knob has a bracket over it.
void ui_knobs(const struct app *a, uint32_t *fb, int stride, const struct cell c[3]) {
    int loaded = app_pad(a)->smp != NULL;
    char buf[32];
    for (int k = 0; k < 3; k++) {
        int cx = 8 + k * 104, bx = cx + 10, bw = 73, id = page_param(a, k);
        if (k == a->sel) ui_fill(fb, stride, cx, 200, 95, 1, C_TEXT), vline(fb, stride, cx, 200, 205, C_TEXT),
                         vline(fb, stride, cx + 94, 200, 205, C_TEXT);
        if (id == P_NONE) continue;
        const struct param *p = &PARAMS[id];
        int on = p->in == IN_PAD ? loaded  // a pad's settings mean nothing on an empty pad, a note's without a note
               : p->in == IN_EVENT ? seq_holds(a->sq, a->sq->cur, a->track, a->roll_sel) : 1;
        uint32_t col = on ? c[k].colour : C_DIM;
        if (on && (a->touched == k || c[k].value)) param_format(a, id, buf, sizeof buf);
        else snprintf(buf, sizeof buf, "%s", p->name);
        ui_text(fb, stride, cx + 12, 208, buf, 1, col, c[k].icon >= 0 ? cx + 78 : cx + 98);
        if (c[k].icon >= 0) ui_icon(fb, stride, cx + 80, 206, c[k].icon, 0, col);
        if (!on || c[k].value) continue;
        float n = param_norm(a, id);
        n = n < 0 ? 0 : n > 1 ? 1 : n;
        if (p->show == SHOW_LIST) {
            int segs = (int)(p->hi - p->lo) + 1, sw = (bw - (segs - 1) * 2) / segs, sel = (int)lroundf(n * (segs - 1));
            for (int i = 0; i < segs; i++) ui_fill(fb, stride, bx + i * (sw + 2), 227, sw, 3, i == sel ? col : C_LINE);
        } else if (p->lo < 0 && p->hi > 0 && p->show != SHOW_DB) {
            ui_fill(fb, stride, bx, 227, bw, 3, C_LINE);
            ui_fill(fb, stride, bx + (int)(n * (bw - 3)), 226, 3, 5, col);
        } else {
            ui_fill(fb, stride, bx, 227, bw, 3, C_LINE);
            ui_fill(fb, stride, bx, 227, 2 + (int)(n * (bw - 2)), 3, col);
        }
    }
}

void ui_shift_icon(uint32_t *fb, int stride, int held) {  // the guide's sign that SHIFT changes this screen
    static const char *const up[13] = {"...#...", "..###..", ".#####.", "#######", "..###..", "..###..", "", "#######"};
    glyph(fb, stride, 306, 227, up, held ? C_RED : C_TEXT);
}

// ---- waveform -----------------------------------------------------------------------------------------

#define WX 18   // first column inside the waveform boxes
#define WW 284
#define WY 91   // first row inside the left channel's box; the right one starts WH + 4 below
#define WH 45

// Amp envelope gain at sample frame pos of the play range [lo, hi), with the engine's linear stages; *stage:
// 0 attack, 1 full level, 2 decay (attack wins where both act). A Note On release depends on when the pad is
// lifted, so it is not drawn.
static float env_gain(const struct pad_params *p, double pos, double lo, double hi, int *stage) {
    double step = pow(2.0, (p->semi + p->fine / 100.0) / 12.0);  // sample frames per output frame
    double t = (p->reverse ? hi - pos : pos - lo) / step, left = (p->reverse ? pos - lo : hi - pos) / step;
    double a = env_seconds(p->attack) * RATE, d = env_seconds(p->decay) * RATE;
    double g = a < 1 || t >= a ? 1 : t / a;
    *stage = a >= 1 && t < a ? 0 : 1;
    if (p->note_on) return (float)g;
    if (p->decay_from == DECAY_FROM_START) {
        double td = t - (a < 1 ? 0 : a);
        if (td > 0) g = d < 1 || td >= d ? 0 : 1 - td / d, *stage = *stage ? 2 : 0;
    } else if (!p->loop_on && left < d) {
        g *= left / d, *stage = *stage ? 2 : 0;
    }
    return (float)g;
}

// Per-column peaks of each channel of the shown sample. A long sample takes ~1 ms to scan, so it is rescanned only
// when the pad shows another sample (samples never change in place); ui_free_retired drops it, since a freed
// address can return.
static struct { const struct sample *s; float lo[2][WW], hi[2][WW]; } peaks;

// env: the amp envelope over a dimmed waveform shaped by it, attack orange and decay teal as on the guide's Amp Env.
void ui_wave(uint32_t *fb, int stride, const struct pad *pd, const struct sample *s, int env) {
    const struct pad_params *p = &pd->p;
    int frames = s->frames;
    if (s != peaks.s) {
        peaks.s = s;
        for (int x = 0; x < WW; x++) {
            long a = (long)x * frames / WW, b = (long)(x + 1) * frames / WW;
            for (int ch = 0; ch < 2; ch++) {
                float lo = 0, hi = 0;
                for (long i = a; i < (b > a ? b : a + 1) && i < frames; i++) {
                    float v = s->lr[2 * i + ch];
                    lo = v < lo ? v : lo, hi = v > hi ? v : hi;
                }
                peaks.lo[ch][x] = lo, peaks.hi[ch][x] = hi;
            }
        }
    }
    int xs = (int)(p->start * WW), xe = (int)(p->end * WW), xl = (int)((p->loop_lock ? p->start : p->loop) * WW);
    int bottom = WY + 2 * WH + 3;  // last row inside the right channel's box
    uint32_t box = pd->muted ? C_DIM : C_BOX;
    for (int i = 0; i <= 16; i++) {  // rulers: start, middle and end marked white, dots between
        int x = WX + i * (WW - 1) / 16;
        if (i % 8 == 0) vline(fb, stride, x, 80, 82, C_TEXT), vline(fb, stride, x, 194, 196, C_TEXT);
        else fb[81 * stride + x] = fb[195 * stride + x] = C_DIM;
    }
    for (int ch = 0; ch < 2; ch++) {
        int top = WY + ch * (WH + 4), mid = top + WH / 2, half = WH / 2 - 1;
        if (p->loop_on && xl > 0) ui_fill(fb, stride, WX, top, xl, WH, 0x262626);  // before the loop point
        ui_frame(fb, stride, WX - 1, top - 1, WW + 2, WH + 2, box);
        ui_fill(fb, stride, WX, mid, WW, 1, box);
        for (int x = 0; x < WW; x++) {
            int in = x >= xs && x < xe, stage;
            float g = env && in ? env_gain(p, (x + 0.5) * frames / WW, p->start * frames, p->end * frames, &stage) : 1;
            int y0 = mid - (int)(peaks.hi[ch][x] * g * half), y1 = mid - (int)(peaks.lo[ch][x] * g * half);
            vline(fb, stride, WX + x, y0, y1, pd->muted ? C_MUTED : env || !in ? C_WAVE_DIM : C_WAVE);
        }
    }
    if (env) {  // the envelope over both channels, full level at the top; joined column to column
        for (int x = xs, prev = -1, stage; x < xe && x < WW; x++) {
            float g = env_gain(p, (x + 0.5) * frames / WW, p->start * frames, p->end * frames, &stage);
            int y = bottom - (int)(g * (bottom - WY));
            vline(fb, stride, WX + x, prev < 0 || y < prev ? y : prev, prev > y ? prev : y,
                  stage == 0 ? C_ORANGE : stage == 2 ? C_TEAL : C_TEXT);
            prev = y;
        }
    }
    if (p->loop_on) vline(fb, stride, WX + xl, WY - 1, bottom + 1, C_TEXT);
    int mh = bottom - WY + 3;  // side bars along the boxes: pad volume (left), pan (right)
    float vn = db_to_gain(p->vol_db) / db_to_gain(PARAMS[P_VOL].hi);  // in gain, as the guide's bar: 0 dB fills half
    ui_fill(fb, stride, 9, WY - 1, 4, mh, C_LINE);
    ui_fill(fb, stride, 9, WY - 1 + mh - (int)(vn * mh), 4, (int)(vn * mh), C_TEXT);
    ui_fill(fb, stride, 307, WY - 1, 4, mh, C_LINE);
    ui_fill(fb, stride, 307, WY - 1 + (int)((p->pan + 1) / 2 * (mh - 4)), 4, 4, C_TEXT);
}

// ---- screens ------------------------------------------------------------------------------------------

// MPC 3 Track Mute's grid: a cell per track where its pad is (track 1 bottom left). mute: the Track Mute screen,
// where pads switch tracks; else the SAMPLE + encoder track select, with K1 Type and K2 Length.
void ui_tracks(const struct app *a, uint32_t *fb, int stride, int mute) {
    char buf[16];
    ui_text(fb, stride, 8, 12, mute ? "Track Mute" : "Tracks", 1, C_TEXT, UI_W);
    const char *hint = mute ? "PADS: TRACKS" : "SAMPLE + ENCODER";
    ui_text(fb, stride, UI_W - 8 - (int)strlen(hint) * FONT_S_W, 15, hint, 0, C_DIM, UI_W);
    for (int t = 0; t < TRACKS; t++) {
        const struct track *tr = &a->e->tracks[t];
        int x = 4 + t % 4 * 79, y = 32 + (3 - t / 4) * 41, n = 0, type = tr->type <= TRACK_MIDI ? tr->type : TRACK_EMPTY;
        for (int b = 0; b < BANKS && type == TRACK_DRUM; b++)
            for (int i = 0; i < PADS; i++) n += tr->pads[b][i].smp != NULL;
        int lit = !mute && t == a->track;
        uint32_t fg = lit ? C_BG : type != TRACK_EMPTY ? C_TEXT : C_DIM;
        if (lit) ui_fill(fb, stride, x, y, 76, 38, C_TEXT);
        else ui_frame(fb, stride, x, y, 76, 38, type == TRACK_EMPTY ? C_LINE : tr->muted ? C_ORANGE : C_YELLOW);
        if (tr->muted) ui_fill(fb, stride, x + 56, y + 19, 15, 15, C_ORANGE), ui_text(fb, stride, x + 59, y + 20, "M", 0, C_BG, UI_W);
        snprintf(buf, sizeof buf, "T%d", t + 1);
        ui_text(fb, stride, x + 5, y + 3, buf, 0, fg, x + 76);
        if (type == TRACK_MIDI) snprintf(buf, sizeof buf, "Ch%d", tr->midi_ch);
        else if (n) snprintf(buf, sizeof buf, "%d", n);
        else buf[0] = 0;
        ui_text(fb, stride, x + 71 - (int)strlen(buf) * FONT_S_W, y + 3, buf, 0, fg, x + 76);
        ui_text(fb, stride, x + 5, y + 18, PARAMS[P_TTYPE].list[type], 1, fg, x + 54);
    }
    if (!mute) ui_knobs(a, fb, stride, (struct cell[3]){{C_TEXT, -1, 0}, {C_TEXT, -1, 0}, {C_TEXT, -1, 0}});
}

void ui_confirm(uint32_t *fb, int stride, const char *question) {  // drawn over the screen; the mode leaves the tabs empty
    static const struct tab t[3] = {{"Cancel", 0, 0, 0}, {"", 0, 0, 0}, {"Do It!", 1, 0, 0}};
    int w = (int)strlen(question) * FONT_L_W + 24, x = (UI_W - w) / 2;
    ui_tabs(fb, stride, t);
    ui_fill(fb, stride, x, 106, w, 34, C_TEXT), ui_fill(fb, stride, x + 1, 107, w - 2, 32, 0x2a2a2a);
    ui_text(fb, stride, x + 12, 115, question, 1, C_TEXT, x + w);
}

void ui_draw(const struct app *a, uint32_t *fb, int stride) {
    ui_fill(fb, stride, 0, 0, UI_W, UI_H, C_BG);
    ui_fill(fb, stride, 4, 3, 312, 3, C_LINE);  // output level along the top, as on every screen of the guide
    ui_fill(fb, stride, 4, 3, a->meter * 312 / 256, 3, C_TEAL);
    if (a->track_view) ui_tracks(a, fb, stride, 0);
    else a->mode->draw(a, fb, stride);
    char msg[sizeof a->popup];  // copied with a bound: the audio thread may be rewriting it right now
    memcpy(msg, a->popup, sizeof msg), msg[sizeof msg - 1] = 0;
    if (msg[0]) {
        int w = (int)strlen(msg) * FONT_L_W + 24;
        w = w > UI_W - 16 ? UI_W - 16 : w;
        int x = (UI_W - w) / 2;
        ui_fill(fb, stride, x, 96, w, 34, C_TEXT);
        ui_fill(fb, stride, x + 1, 97, w - 2, 32, 0x2a2a2a);
        ui_text(fb, stride, x + 12, 105, msg, 1, C_TEXT, x + w - 8);
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
    peaks.s = NULL;
    __atomic_store_n(&a->grave_freed, upto, __ATOMIC_RELEASE);
}
