// WAV decoding and loading a folder of WAV files into the pad banks.
#pragma once
#include <stddef.h>
#include "engine.h"

#define MAX_FRAMES (60 * RATE)  // longer samples are cut at 60 s
#define MAX_FILE (64u << 20)    // larger WAV files are skipped

// Decodes a whole WAV file held in memory (44.1 kHz, 16/24/32-bit PCM or 32-bit float, mono or stereo) to
// interleaved stereo float. Returns NULL with *err set; every size is checked: files come from the SD card.
float *wav_decode(const unsigned char *b, size_t n, int *frames, const char **err);

// Loads the first BANKS*PADS WAV files of dir (name order) into banks A..H; returns how many pads got a sample.
int load_folder(struct engine *e, const char *dir);
