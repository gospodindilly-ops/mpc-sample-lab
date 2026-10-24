// WAV decoding and loading (file thread only: it allocates and reads files).
#pragma once
#include <stddef.h>
#include "engine.h"

#define MAX_FRAMES (60 * RATE)  // longer samples are cut at 60 s
#define MAX_FILE (64u << 20)    // larger WAV files are skipped

// Decodes a whole WAV file held in memory (44.1 kHz, 16/24/32-bit PCM or 32-bit float, mono or stereo) into a
// new stereo sample named name. Returns NULL with *err set; every size is checked: files come from the SD card.
struct sample *wav_decode(const unsigned char *b, size_t n, const char *name, const char **err);

// Reads and decodes one WAV file; the sample is named after the file without ".wav". NULL with *err set.
struct sample *wav_load(const char *path, const char **err);
