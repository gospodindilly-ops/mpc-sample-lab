// WAV decoding (taken over from sampler/sampler.c) and folder loading, see wav.h.
#include "wav.h"
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint32_t le16(const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

float *wav_decode(const unsigned char *b, size_t n, int *frames, const char **err) {
    if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return *err = "not a RIFF/WAVE file", NULL;
    const unsigned char *fmt = NULL, *data = NULL;
    size_t fmt_len = 0, data_len = 0;
    for (size_t o = 12; o + 8 <= n;) {
        size_t len = le32(b + o + 4);
        if (len > n - o - 8) len = n - o - 8;  // truncated last chunk: use what is there
        if (!memcmp(b + o, "fmt ", 4)) fmt = b + o + 8, fmt_len = len;
        else if (!memcmp(b + o, "data", 4)) data = b + o + 8, data_len = len;
        o += 8 + len + (len & 1);              // chunks are padded to even sizes
    }
    if (!fmt || fmt_len < 16 || !data) return *err = "missing fmt or data chunk", NULL;
    unsigned tag = le16(fmt), ch = le16(fmt + 2), bits = le16(fmt + 14);
    if (tag == 0xFFFE && fmt_len >= 26) tag = le16(fmt + 24);  // WAVE_FORMAT_EXTENSIBLE: real tag starts the subformat GUID
    if (le32(fmt + 4) != RATE) return *err = "sample rate is not 44100 Hz", NULL;
    if (ch != 1 && ch != 2) return *err = "only mono or stereo", NULL;
    if (!((tag == 1 && (bits == 16 || bits == 24 || bits == 32)) || (tag == 3 && bits == 32)))
        return *err = "only 16/24/32-bit PCM or 32-bit float", NULL;
    size_t bps = bits / 8, stride = bps * ch, count = data_len / stride;
    if (count > MAX_FRAMES) count = MAX_FRAMES;
    if (!count) return *err = "no audio frames", NULL;
    float *out = malloc(count * 2 * sizeof *out);
    if (!out) return *err = "out of memory", NULL;
    for (size_t f = 0; f < count; f++)
        for (size_t c = 0; c < 2; c++) {
            const unsigned char *s = data + f * stride + (ch == 2 ? c : 0) * bps;  // mono feeds both sides
            float v;
            if (tag == 3) { uint32_t u = le32(s); memcpy(&v, &u, 4); if (!isfinite(v)) v = 0; }
            else if (bits == 16) v = (int16_t)le16(s) / 32768.0f;
            else if (bits == 24) v = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) / 2147483648.0f;
            else v = (int32_t)le32(s) / 2147483648.0f;
            out[2 * f + c] = v;
        }
    *frames = (int)count;
    return out;
}

static int is_wav(const struct dirent *d) {
    size_t n = strlen(d->d_name);
    return d->d_name[0] != '.' && n > 4 && !strcasecmp(d->d_name + n - 4, ".wav");  // skips macOS "._" files
}

int load_folder(struct engine *e, const char *dir) {
    struct dirent **list;
    int n = scandir(dir, &list, is_wav, alphasort), loaded = 0;
    if (n < 0) { perror(dir); return 0; }
    for (int i = 0; i < n; i++) {
        if (i < BANKS * PADS) {
            struct sample *s = &e->pads[i / PADS][i % PADS].smp;
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dir, list[i]->d_name);
            FILE *f = fopen(path, "rb");
            long len = -1;
            unsigned char *buf = NULL;
            const char *err = "cannot read";
            if (f && !fseek(f, 0, SEEK_END) && (len = ftell(f)) > 0 && (unsigned long)len <= MAX_FILE && !fseek(f, 0, SEEK_SET)
                && (buf = malloc(len)) && fread(buf, 1, len, f) == (size_t)len)
                s->lr = wav_decode(buf, len, &s->frames, &err);
            else if (len > 0 && (unsigned long)len > MAX_FILE)
                err = "file larger than 64 MB";
            if (s->lr) {
                loaded++;
                snprintf(s->name, sizeof s->name, "%.*s", (int)strlen(list[i]->d_name) - 4, list[i]->d_name);
                printf("%c%02d: %s (%.2f s)\n", 'A' + i / PADS, i % PADS + 1, list[i]->d_name, s->frames / (double)RATE);
            } else {
                printf("%c%02d: %s skipped: %s\n", 'A' + i / PADS, i % PADS + 1, list[i]->d_name, err);
            }
            free(buf);
            if (f) fclose(f);
        }
        free(list[i]);
    }
    free(list);
    if (n > BANKS * PADS) printf("%d more WAV files ignored\n", n - BANKS * PADS);
    return loaded;
}
