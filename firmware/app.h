// Sample Mode controller: turns control-surface MIDI into engine actions, owns the UI state and the
// LED / pad-colour state sent back to the surface. No I/O: MIDI for the surface collects in out[].
#pragma once
#include "engine.h"

enum {
    P_NONE, P_START, P_END, P_LOOP, P_VOL, P_KITVOL, P_PAN, P_ATTACK, P_DECAY, P_VELSENS,
    P_DECAYFROM, P_SEMI, P_FINE, P_MONO, P_MUTEGRP,
};

#define GRAVE 64  // sample buffers retired by the audio thread, freed by the UI thread after its next frame

struct app {
    struct engine *e;
    int bank, pad;                                   // selected pad
    int shift, sample_held, bank_held, bank_used, erase_held;
    int mute_mode, mute_held, mute_used;
    int group, page[3], sel;                         // B1/B2/B3 page groups, encoder-selected knob 0..2
    int knob[3];                                     // last K1..K3 positions, -1 until the first move
    float shadow[3];                                 // knob-controlled value 0..1 between rounding steps
    int fader, fader_engaged;                        // Pickup takeover for Pad Volume
    int pressed[PADS];                               // velocity of held pads, 0 = up
    char popup[48];
    unsigned popup_until, now;                       // in audio frames
    unsigned char out[4096];
    int out_len;
    unsigned char led_sent[128], pad_sent[PADS][3];
    int leds_valid;
    float *grave[GRAVE];
    unsigned grave_n, grave_freed;                   // written by the audio / the UI thread
    int redraw;                                      // set by the audio thread, cleared by the UI thread
};

void app_init(struct app *a, struct engine *e);
void app_midi(struct app *a, const unsigned char *m);  // one complete channel message from the surface
void app_tick(struct app *a, int frames);              // clock, popups, LED refresh into out[]
int page_param(const struct app *a, int k);            // parameter on knob k (0..2) of the current page
const char *page_name(const struct app *a, int g);  // current page of group g (B1..B3 = 0..2)
const char *param_name(const struct app *a, int id);
void param_format(const struct app *a, int id, char *buf, int n);
