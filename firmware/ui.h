// Screen: 320x240 XRGB8888, drawn in the UI thread from the app and engine state without locks (a stale value
// only shows for one frame). Widgets follow the MPC Sample User Guide screens: B1..B3 tabs on top, the knob row
// at the bottom, the mode's own view between them, the output level along the top; the 16-track grid and the MIDI
// track's pad view, which the MPC Sample guide does not have, are modelled on MPC 3.
#pragma once
#include <stdint.h>
#include "app.h"

#define UI_W 320
#define UI_H 240

enum { C_BG = 0x000000, C_TEXT = 0xe6e6e6, C_DIM = 0x5a5a5a, C_LINE = 0x3a3a3a, C_YELLOW = 0xf0a828,
       C_WAVE = 0xffb000, C_WAVE_DIM = 0x6e5212, C_BOX = 0x7a5600, C_BLUE = 0x3a9fe8, C_ORANGE = 0xff7a1a,
       C_TEAL = 0x1fd1a0, C_RED = 0xe0245e, C_MUTED = 0x8a8a8a };
enum { ICON_NOTE, ICON_REVERSE, ICON_LOOP, ICON_16, ICON_LOCK, ICON_METRO };

struct tab { const char *label; int on, pages, page; };  // on: white box; pages > 1: a dash per page under it
struct cell { uint32_t colour; int icon, value; };      // a knob's colour, icon after the label (-1: none); value: shows
                                                         // its value instead of name and bar (Sequence Mode)

void ui_draw(const struct app *a, uint32_t *fb, int stride);  // the current screen: mode or tracks, plus the popup
// Copies what changed from back into front (both UI_W x UI_H) and returns how many rectangles changed: one per
// band of changed rows, cut to the changed columns; rect = x0, y0, x1, y1 with x1, y1 exclusive.
int ui_changes(const uint32_t *back, uint32_t *front, int rect[UI_H][4]);
void ui_free_retired(struct app *a, unsigned upto);  // free buffers retired before the frame that just ended

// Widgets for modes.
void ui_fill(uint32_t *fb, int stride, int x0, int y0, int w, int h, uint32_t c);
int ui_text(uint32_t *fb, int stride, int x, int y, const char *s, int big, uint32_t c, int x_max);  // big: 0 6x12, 1 8x16, 2 16x32
void ui_text_fit(uint32_t *fb, int stride, int x, int y, const char *s, int big, uint32_t c, int x_max);  // "..." when cut
void ui_frame(uint32_t *fb, int stride, int x, int y, int w, int h, uint32_t c);
void ui_tiny(uint32_t *fb, int stride, int x, int y, const char *s, uint32_t c);  // 3x5: digits, A-H, #, -
void ui_icon(uint32_t *fb, int stride, int x, int y, int icon, int on, uint32_t on_colour);  // 23x20 box; LOCK, METRO bare
void ui_tabs(uint32_t *fb, int stride, const struct tab t[3]);
void ui_page_tabs(const struct app *a, struct tab t[3]);  // the mode's current B1..B3 pages
void ui_knobs(const struct app *a, uint32_t *fb, int stride, const struct cell c[3]);  // K1..K3 of the current page
void ui_wave(uint32_t *fb, int stride, const struct pad *pd, const struct sample *s, int env);  // stereo, with meters
void ui_shift_icon(uint32_t *fb, int stride, int held);
void ui_tracks(const struct app *a, uint32_t *fb, int stride, int mute);
void ui_confirm(uint32_t *fb, int stride, const char *question);  // the guide's question: B1 Cancel, B3 Do It!  // the 16 tracks as the pads; mute: Track Mute
