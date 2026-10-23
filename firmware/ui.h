// Screen: draws the Sample Mode page into a 320x240 XRGB8888 buffer. Runs in the UI thread; reads the app
// and engine state without locks (a stale value only shows for one frame).
#pragma once
#include <stdint.h>
#include "app.h"

#define UI_W 320
#define UI_H 240

void ui_draw(const struct app *a, uint32_t *fb, int stride);
// Copies what changed from back into front (both UI_W x UI_H) and returns how many rectangles changed: one per
// band of changed rows, cut to the changed columns; rect = x0, y0, x1, y1 with x1, y1 exclusive.
int ui_changes(const uint32_t *back, uint32_t *front, int rect[UI_H][4]);
void ui_free_retired(struct app *a, unsigned upto);  // free buffers retired before the frame that just ended
