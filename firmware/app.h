// Controller: turns control-surface MIDI into engine actions, owns the UI state and the LED / pad-colour state
// sent back to the surface. No I/O: MIDI for the surface collects in out[]. What every mode shares (SHIFT, knobs,
// encoder, fader, PAD BANK, MUTE, track select) is here; the rest of a mode lives in its own file (mode_*.c).
#pragma once
#include <stdint.h>
#include "engine.h"
#include "files.h"
#include "params.h"
#include "seq.h"

#define GRAVE 64  // sample buffers retired by the audio thread, freed by the UI thread after its next frame

// Control-surface buttons: note numbers on MIDI channel 1 (docs/control-surface-protocol.md).
enum { N_SAMPLE = 6, N_SEQ = 7, N_SHIFT = 49, N_PADBANK = 35, N_ERASE = 9, N_CHOP = 75, N_MUTE = 43, N_LOOP = 74,
       N_B1 = 12, N_B2 = 13, N_B3 = 14, N_ENCPUSH = 111, N_MINUS = 55, N_PLUS = 54, N_STOP = 81, N_PLAY = 82,
       N_SEQREC = 80, N_REPEAT = 11, N_TAP = 53 };

enum { CC_K1 = 16, CC_K2 = 17, CC_K3 = 18, CC_MAIN_VOLUME = 19, CC_FADER = 20, CC_ENCODER = 100, CC_BANK_LED = 57 };

static inline int midi_note(int key) { return (36 + key) & 127; }  // a MIDI track's pad key (bank * 16 + pad): A01 = C1

struct app;
struct page { const char *name; int k[3], shift_k[3]; };  // a B1..B3 page: parameters on K1..K3, shift_k if set while SHIFT is held

// A mode: one screen with its B1..B3 pages, what its pads and buttons do, its LEDs and how it draws.
struct mode {
    struct page pages[3][3];
    int npages[3];
    void (*pad)(struct app *a, int i, int vel);                      // vel 0: the pad is lifted
    int (*button)(struct app *a, int note, int down);                // 1 if the mode used the button
    int (*led)(const struct app *a, int note);                       // 0 off, 1 dim, 2 lit; -1: not the mode's
    void (*colour)(const struct app *a, int i, unsigned char c[3]);  // pad colour when nothing else shows
    void (*draw)(const struct app *a, uint32_t *fb, int stride);     // the whole screen, in the UI thread
    int (*control)(struct app *a, int cc, int v);                    // optional: the encoder or fader, 1 if used
};
extern const struct mode SAMPLE_MODE, MIDI_MODE, AUDIO_MODE, SEQ_MODE, ROLL_MODE;  // Drum (and empty), MIDI, Audio
                                                                            // tracks; sequences; the piano roll

struct off { int track, key, port, ch, left; };  // a note-off due in `left` frames; port -1: a drum pad's release

struct app {
    struct engine *e;
    struct sequencer *sq;
    const struct mode *mode;
    int seq_pads;                                    // Sequence screen with pads playing the track: 1 SEQ + SAMPLE, 2 armed
    int seq_held, bpm_edit, bpm_fine, tap_held;      // B1 (Sequence Mode) or TAP TEMPO held: the encoder sets the tempo
    unsigned tap_at, stop_at;                        // last TAP TEMPO and STOP presses, for tempo and double-press
    float tap_gap;
    int repeat, repeat_tap, repeat_used, repeat_div; // Note Repeat: on; held since pressed; div 0..5 (1/8..1/32, triplets)
    unsigned noting;                                 // pads held as notes on the selected track: Note Repeat repeats them
    int32_t rec_ev[PADS];                            // event recorded for a held pad, for its length; -1: none
    double rec_at[PADS];
    struct off offs[64];
    int noffs;
    const struct mode *back;                         // the mode SAMPLE was pressed in: a track picked there returns to it
    uint32_t roll_tick;                              // piano roll: cursor step, in the track's own ticks
    int32_t roll_sel;                                // its selected event, -1: none
    int roll_zoom;                                   // 1, 2, 4 bars or the whole track
    int confirm;                                     // pad or note whose events ERASE + pad asks to erase; -1: none
    int track, bank, pad;                            // selected track and pad
    int shift, sample_held, bank_held, bank_used, erase_held;
    int mute_mode, mute_held, mute_used;
    int track_view;                                  // SAMPLE held and the encoder turned: the 16 tracks show
    int group, page[3], sel;                         // B1/B2/B3 page groups, encoder-selected knob 0..2
    int knob[3];                                     // last K1..K3 positions, -1 until the first move
    float shadow[3];                                 // knob-controlled value 0..1 between rounding steps
    int fader, fader_engaged;                        // Pickup takeover for Pad Volume
    int pressed[PADS];                               // velocity of held pads, 0 = up
    int touched;                                     // knob whose value shows in place of its name, -1: none
    unsigned touched_until;
    int note_out[PADS];                              // MIDI note a pad holds on: note | ch << 8 | port << 12, -1: none
    unsigned char ext[2][256];                       // MIDI for the External (TRS) and USB ports, sent by the audio loop
    int ext_len[2];
    int meter;                                       // output level for the strip along the top, 0..256
    float meter_peak;                                // loudest output since the meter last moved
    unsigned meter_at;
    char popup[48];
    unsigned popup_until, now;                       // in audio frames
    unsigned char out[4096];
    int out_len;
    unsigned char led_sent[128], pad_sent[PADS][3];
    int leds_valid;
    struct sample *grave[GRAVE];
    unsigned grave_n, grave_freed;                   // written by the audio / the UI thread
    struct queue *jobs;                              // requests to the file thread (NULL: edits are refused)
    const struct sample *pinned;                     // source of the edit in flight: not freed until it is back
    int pinned_dropped;                              // the pad let go of the pinned sample meanwhile
    int redraw;                                      // set by the audio thread, cleared by the UI thread
};

void app_init(struct app *a, struct engine *e, struct sequencer *s);
void app_midi(struct app *a, const unsigned char *m);  // one complete channel message from the surface
void app_tick(struct app *a, int frames);              // clock, popups, LED refresh into out[]
void app_result(struct app *a, const struct job *j);   // a sample loaded or edited by the file thread

// For modes and the screen.
struct pad *app_pad(const struct app *a);              // the selected pad of the selected track
void app_popup(struct app *a, const char *fmt, ...);
void app_swap_sample(struct app *a, int t, int bank, int i, struct sample *s);  // NULL empties the pad
void app_edit_sample(struct app *a, int type);         // JOB_TRIM / JOB_NORMALIZE on the selected pad
void app_midi_out(struct app *a, int port, int status, int d1, int d2);  // a channel message to an external port
void app_played(struct app *a, int i, int vel);        // a mode played pad i on the selected track: record it, repeat it
void app_released(struct app *a, int i);
const struct mode *app_track_mode(const struct app *a); // the selected track's own mode (Sample, MIDI or Audio)
int page_param(const struct app *a, int k);            // parameter on knob k (0..2) of the current page
const char *page_name(const struct app *a, int g);     // current page of group g (B1..B3 = 0..2)
float param_norm(const struct app *a, int id);         // 0..1 along its range, for bars
void param_format(const struct app *a, int id, char *buf, int n);
