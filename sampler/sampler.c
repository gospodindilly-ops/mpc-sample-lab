// MPC Sample mini sampler: the first 16 WAV files of a folder (name order) on pads 1-16, MAIN VOLUME as
// software volume, audio thread at real-time priority, pad grid through DRM. Run only while ac50 is stopped.
// Usage:     mpc-sampler ["/media/MPC SAMPLE/sampler"]   (44.1 kHz, 16/24/32-bit PCM or 32-bit float, mono/stereo)
// Build:     zig cc -target aarch64-linux-musl -static -O2 -I../panel-demo -o mpc-sampler sampler.c
// Self-test: cc -DSELFTEST -I../panel-demo -o sampler-host sampler.c -lm && ./sampler-host
// shortcut: PCM, DRM and screen-thread code is copied from sound-test/sound_test.c; move it into a shared
// header when a third program needs it.
#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include "surface.h"

#define RATE 44100
#define PERIOD 128                    // frames per write, same as the stock app
#define MAX_FRAMES (60 * RATE)        // longer samples are cut at 60 s
#define MAX_FILE (64u << 20)          // larger WAV files are skipped
#define FADE 256                      // 5.8 ms fade-out of a voice cut by a new hit on the same pad
#define TAIL 64                       // 1.5 ms fade at a sample's end (files may stop off zero)
#define KNEE 0.8f                     // the mix is untouched below this level (-1.9 dBFS)
#define CC_MAIN_VOLUME 19

struct sample { float *lr; int frames; };  // interleaved stereo, -1..1
static struct sample samples[16];

struct voice { const float *lr; int frames, pos; float gain; int fade; };  // fade > 0: frames left to silence
static struct voice voices[32];             // [pad]: playing, [16 + pad]: the previous hit fading out
static float volume = 0.25f;                // until MAIN VOLUME is moved; the knob position is unknown at start

static float volume_from_cc(int v) { return (v / 127.0f) * (v / 127.0f); }  // squared: closer to perceived loudness

static uint32_t le16(const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// Decodes a whole WAV file held in memory. Files come from the SD card, so every size is checked.
static float *wav_decode(const unsigned char *b, size_t n, int *frames, const char **err) {
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

static void trigger(int pad, int velocity) {
    if (!samples[pad].lr) return;
    struct voice *v = &voices[pad];
    if (v->pos < v->frames)  // an instant cut of a ringing voice clicks; hits closer than FADE cut the older fade
        voices[16 + pad] = *v, voices[16 + pad].fade = FADE;
    *v = (struct voice){samples[pad].lr, samples[pad].frames, 0, velocity / 127.0f, 0};
}

static int32_t to_s32(float s) { return (int32_t)((s > 1 ? 1 : s < -1 ? -1 : s) * 2147483520.0f); }  // largest float below 2^31

// Loud hits summing past full scale were hard-clipped, which clicks. Above KNEE the level bends smoothly
// towards full scale; the slope matches at KNEE, so there is no edge to hear.
static float softclip(float x) {
    float a = fabsf(x);
    if (a <= KNEE) return x;
    float y = KNEE + (1 - KNEE) * tanhf((a - KNEE) / (1 - KNEE));
    return x < 0 ? -y : y;
}

static void render(int32_t *out, int frames) {  // mixes all voices into interleaved stereo S32_LE
    for (int f = 0; f < frames; f++) {
        float l = 0, r = 0;
        for (int i = 0; i < 32; i++) {
            struct voice *v = &voices[i];
            if (v->pos >= v->frames) continue;
            float g = v->gain;
            int left = v->frames - v->pos;
            if (v->frames > 4 * TAIL && left < TAIL) g *= (float)left / TAIL;  // very short samples keep their edge
            if (v->fade) g *= (float)v->fade / FADE;
            l += v->lr[2 * v->pos] * g, r += v->lr[2 * v->pos + 1] * g;
            v->pos++;
            if (v->fade && --v->fade == 0) v->pos = v->frames;  // faded out: free the slot
        }
        out[2 * f] = to_s32(softclip(l * volume)), out[2 * f + 1] = to_s32(softclip(r * volume));
    }
}

// MPC layout: pad 1 bottom-left, pad 16 top-right; 320x240 screen in 80x60 cells.
static void pad_cell(int pad, int *x, int *y) { *x = pad % 4 * 80, *y = (3 - pad / 4) * 60; }

#ifdef SELFTEST
#include <assert.h>
static size_t wav_make(unsigned char *b, unsigned tag, unsigned ch, unsigned bits, uint32_t rate, const void *pcm, size_t len) {
    unsigned char *p = b;
#define PUT32(v) (p[0] = (v) & 255, p[1] = (v) >> 8 & 255, p[2] = (v) >> 16 & 255, p[3] = (uint32_t)(v) >> 24, p += 4)
#define PUT16(v) (p[0] = (v) & 255, p[1] = (v) >> 8 & 255, p += 2)
    memcpy(p, "RIFF", 4), p += 4, PUT32(0), memcpy(p, "WAVE", 4), p += 4;
    memcpy(p, "LIST", 4), p += 4, PUT32(3), memcpy(p, "abc", 3), p += 4;  // odd chunk + pad byte before fmt
    memcpy(p, "fmt ", 4), p += 4, PUT32(16), PUT16(tag), PUT16(ch), PUT32(rate);
    PUT32(rate * ch * bits / 8), PUT16(ch * bits / 8), PUT16(bits);
    memcpy(p, "data", 4), p += 4, PUT32(len), memcpy(p, pcm, len), p += len;
    return p - b;
}

int main(void) {
    unsigned char b[256];
    const char *err = NULL;
    int frames = 0;
    const int16_t s16[] = {16384, -16384, -32768, 32767};  // 2 stereo frames
    float *w = wav_decode(b, wav_make(b, 1, 2, 16, RATE, s16, sizeof s16), &frames, &err);
    assert(w && frames == 2 && w[0] == 0.5f && w[1] == -0.5f && w[2] == -1.0f);
    free(w);
    const unsigned char s24[] = {0, 0, 0x40, 0, 0, 0xC0};  // mono 24-bit: +0.5, -0.5
    w = wav_decode(b, wav_make(b, 1, 1, 24, RATE, s24, sizeof s24), &frames, &err);
    assert(w && frames == 2 && w[0] == 0.5f && w[1] == 0.5f && w[2] == -0.5f && w[3] == -0.5f);
    free(w);
    const float f32[] = {0.25f, NAN};
    w = wav_decode(b, wav_make(b, 3, 1, 32, RATE, f32, sizeof f32), &frames, &err);
    assert(w && frames == 2 && w[0] == 0.25f && w[2] == 0);  // NaN from a file becomes silence
    free(w);
    assert(!wav_decode(b, wav_make(b, 1, 2, 16, 48000, s16, sizeof s16), &frames, &err) && strstr(err, "44100"));
    assert(!wav_decode(b, wav_make(b, 1, 2, 8, RATE, s16, sizeof s16), &frames, &err));
    size_t n = wav_make(b, 1, 2, 16, RATE, s16, sizeof s16);
    w = wav_decode(b, n - 3, &frames, &err);  // truncated data chunk: only the complete frame is kept
    assert(w && frames == 1);
    samples[0] = (struct sample){w, frames};
    assert(!wav_decode(b, 11, &frames, &err));

    int32_t out[2 * 4];
    volume = 1;
    trigger(0, 127);
    trigger(1, 127);  // pad without a sample: ignored
    render(out, 4);
    assert(out[0] == to_s32(0.5f) && out[1] == to_s32(-0.5f) && out[2] == 0 && out[7] == 0);
    float loud[2] = {1, 1};
    samples[2] = (struct sample){loud, 1}, samples[3] = (struct sample){loud, 1};
    trigger(2, 127), trigger(3, 127);
    render(out, 1);
    assert(out[0] > to_s32(0.99f) && out[0] < to_s32(1));  // two full-scale voices: bent below full scale, no wrap
    assert(softclip(0.5f) == 0.5f && softclip(-KNEE) == -KNEE && softclip(-3) == -softclip(3));
    assert(softclip(10) <= 1 && softclip(KNEE + 0.01f) > KNEE && softclip(KNEE + 0.01f) - (KNEE + 0.01f) > -1e-4f);
    for (float x = 0; x < 4; x += 0.01f) assert(softclip(x + 0.01f) >= softclip(x));  // monotonic: no fold-back

    static float half[2 * 1000];  // constant 0.5, long enough for both fades
    static int32_t o2[2 * 1000];
    for (int i = 0; i < 2000; i++) half[i] = 0.5f;
    samples[4] = (struct sample){half, 1000};
    trigger(4, 127);
    render(o2, 10);               // the first hit is still ringing
    trigger(4, 127);              // retrigger: the old voice fades while the new one starts
    render(o2, FADE + 8);
    assert(o2[0] == to_s32(softclip(1.0f)));                             // old 0.5 at full + new 0.5
    assert(abs(o2[2 * (FADE / 2)] - to_s32(0.75f)) < 2147483);          // halfway: old at half gain
    assert(o2[2 * (FADE + 4)] == to_s32(0.5f));                          // fade done: new voice only
    assert(voices[16 + 4].pos == voices[16 + 4].frames);
    render(o2, 1000 - (FADE + 8) - TAIL);                                // up to the new voice's tail
    render(o2, TAIL);
    assert(o2[0] == to_s32(0.5f) && o2[2 * (TAIL - 1)] > 0 && o2[2 * (TAIL - 1)] < to_s32(0.5f / TAIL * 1.01f));
    assert(voices[4].pos == voices[4].frames);
    assert(volume_from_cc(0) == 0 && volume_from_cc(127) == 1);
    int x, y;
    pad_cell(0, &x, &y), assert(x == 0 && y == 180);
    free(w);
    puts("selftest ok");
    return 0;
}
#else
#include <pthread.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <sound/asound.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s, stop = 1; }

static int is_wav(const struct dirent *d) {
    size_t n = strlen(d->d_name);
    return d->d_name[0] != '.' && n > 4 && !strcasecmp(d->d_name + n - 4, ".wav");  // skips macOS "._" files
}

static int load_samples(const char *dir) {  // returns how many pads got a sample
    struct dirent **list;
    int n = scandir(dir, &list, is_wav, alphasort), loaded = 0;
    if (n < 0) { perror(dir); return 0; }
    for (int i = 0; i < n; i++) {
        if (i < 16) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dir, list[i]->d_name);
            FILE *f = fopen(path, "rb");
            long len = -1;
            unsigned char *buf = NULL;
            const char *err = "cannot read";
            if (f && !fseek(f, 0, SEEK_END) && (len = ftell(f)) > 0 && (unsigned long)len <= MAX_FILE && !fseek(f, 0, SEEK_SET)
                && (buf = malloc(len)) && fread(buf, 1, len, f) == (size_t)len)
                samples[i].lr = wav_decode(buf, len, &samples[i].frames, &err);
            else if (len > 0 && (unsigned long)len > MAX_FILE)
                err = "file larger than 64 MB";
            if (samples[i].lr) loaded++, printf("pad %2d: %s (%.2f s)\n", i + 1, list[i]->d_name, samples[i].frames / (double)RATE);
            else printf("pad %2d: %s skipped: %s\n", i + 1, list[i]->d_name, err);
            free(buf);
            if (f) fclose(f);
        }
        free(list[i]);
    }
    free(list);
    if (n > 16) printf("%d more WAV files ignored\n", n - 16);
    return loaded;
}

static void print_switches(int card) {  // read-only: ELEM_READ never changes a control
    static const struct { int iface; const char *name; } ctl[] = {
        {SNDRV_CTL_ELEM_IFACE_MIXER, "Builtin Speaker Switch"}, {SNDRV_CTL_ELEM_IFACE_CARD, "Headphones Jack"},
        {SNDRV_CTL_ELEM_IFACE_CARD, "Line Out Jack"},
    };
    char path[32];
    snprintf(path, sizeof path, "/dev/snd/controlC%d", card);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return; }
    for (size_t i = 0; i < sizeof ctl / sizeof ctl[0]; i++) {
        struct snd_ctl_elem_value v;
        memset(&v, 0, sizeof v);
        v.id.iface = ctl[i].iface;
        strncpy((char *)v.id.name, ctl[i].name, sizeof v.id.name - 1);
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) < 0)
            printf("%-24s ? (%s)\n", ctl[i].name, strerror(errno));
        else
            printf("%-24s %s\n", ctl[i].name, v.value.integer.value[0] ? "on" : "off");
    }
    close(fd);
}

static void hw_any(struct snd_pcm_hw_params *p) {
    memset(p, 0, sizeof *p);
    for (int n = SNDRV_PCM_HW_PARAM_FIRST_MASK; n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++)
        memset(p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits, 0xff, sizeof p->masks[0].bits);
    for (int n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++)
        p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = ~0u;
    p->rmask = ~0u, p->info = ~0u;
}

static void hw_mask(struct snd_pcm_hw_params *p, int n, unsigned bit) {
    struct snd_mask *m = &p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK];
    memset(m->bits, 0, sizeof m->bits);
    m->bits[bit >> 5] = 1u << (bit & 31);
}

static void hw_int(struct snd_pcm_hw_params *p, int n, unsigned v) {
    struct snd_interval *i = &p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
    i->min = i->max = v, i->integer = 1;
}

static int open_pcm(int card) {  // same parameters as the stock app: S32_LE, 2 ch, 44.1 kHz, 3 x 128 frames
    char path[32];
    snprintf(path, sizeof path, "/dev/snd/pcmC%dD0p", card);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: %s%s\n", path, strerror(errno), errno == EBUSY ? " (stop ac50.service first)" : "");
        return -1;
    }
    struct snd_pcm_hw_params hw;
    hw_any(&hw);
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S32_LE);
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_RATE, RATE);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, PERIOD);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 3 * PERIOD);
    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) < 0 || ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) {
        perror("pcm setup");
        close(fd);
        return -1;
    }
    return fd;
}

static int pcm_write(int fd, int32_t *buf, int frames) {
    struct snd_xferi x = {.buf = buf, .frames = frames};
    while (ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
        if (errno == EINTR) return 0;  // a signal: let the main loop see stop
        if (errno != EPIPE || ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) { perror("pcm write"); return -1; }
        fputs("underrun\n", stderr);
    }
    return 0;
}

// /dev/fb0 is never shown while az0x-splash keeps DRM open, so draw into our own DRM dumb buffer.
static uint32_t *fb;  // XRGB8888
static int fb_stride, drm_fd = -1;
static uint32_t fb_id;
static size_t fb_bytes;

static int drm_call(unsigned long req, void *arg, const char *what) {
    int r;
    while ((r = ioctl(drm_fd, req, arg)) < 0 && errno == EINTR) {}
    if (r < 0) fprintf(stderr, "drm %s: %s\n", what, strerror(errno));
    return r;
}

static void screen_open(void) {  // optional: the sampler still plays without a screen
    drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) { perror("/dev/dri/card0"); return; }
    if (ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0) < 0)  // the first opener without a master usually is one already
        fprintf(stderr, "drm set master: %s\n", strerror(errno));

    uint32_t crtcs[4], conns[4];
    struct drm_mode_card_res res = {.crtc_id_ptr = (uintptr_t)crtcs, .connector_id_ptr = (uintptr_t)conns,
                                    .count_crtcs = 4, .count_connectors = 4};  // kernel fills up to these counts
    if (drm_call(DRM_IOCTL_MODE_GETRESOURCES, &res, "getresources") < 0) return;
    if (!res.count_crtcs || !res.count_connectors) { fputs("drm: no crtc or connector\n", stderr); return; }

    struct drm_mode_modeinfo modes[8];
    struct drm_mode_get_connector con = {.connector_id = conns[0]};
    if (drm_call(DRM_IOCTL_MODE_GETCONNECTOR, &con, "getconnector") < 0) return;  // first call probes, counts modes
    if (!con.count_modes || con.count_modes > 8) { fprintf(stderr, "drm: %u modes\n", con.count_modes); return; }
    con = (struct drm_mode_get_connector){.connector_id = conns[0], .modes_ptr = (uintptr_t)modes,
                                          .count_modes = con.count_modes};
    if (drm_call(DRM_IOCTL_MODE_GETCONNECTOR, &con, "getconnector modes") < 0) return;
    struct drm_mode_modeinfo mode = modes[0];
    if (mode.hdisplay < 320 || mode.vdisplay < 240) { fprintf(stderr, "drm: mode %ux%u\n", mode.hdisplay, mode.vdisplay); return; }

    struct drm_mode_create_dumb dumb = {.width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32};
    if (drm_call(DRM_IOCTL_MODE_CREATE_DUMB, &dumb, "create dumb") < 0) return;
    struct drm_mode_fb_cmd2 add = {.width = dumb.width, .height = dumb.height, .pixel_format = DRM_FORMAT_XRGB8888,
                                   .handles = {dumb.handle}, .pitches = {dumb.pitch}};
    if (drm_call(DRM_IOCTL_MODE_ADDFB2, &add, "addfb2") < 0) return;
    struct drm_mode_map_dumb map = {.handle = dumb.handle};
    if (drm_call(DRM_IOCTL_MODE_MAP_DUMB, &map, "map dumb") < 0) return;
    void *p = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, map.offset);
    if (p == MAP_FAILED) { perror("mmap dumb"); return; }
    memset(p, 0, dumb.size);
    struct drm_mode_crtc set = {.crtc_id = crtcs[0], .fb_id = add.fb_id, .set_connectors_ptr = (uintptr_t)&conns[0],
                                .count_connectors = 1, .mode = mode, .mode_valid = 1};
    if (drm_call(DRM_IOCTL_MODE_SETCRTC, &set, "setcrtc") < 0) return;
    fb = p, fb_stride = dumb.pitch / 4, fb_bytes = dumb.size, fb_id = add.fb_id;
    printf("drm %ux%u crtc %u connector %u fb %u\n", mode.hdisplay, mode.vdisplay, crtcs[0], conns[0], fb_id);
}

// SPI panels (mipi-dbi) only update on DIRTYFB, which sends the pixels inside the ioctl (~50 ms for a full
// frame, longer than the 8.7 ms audio buffer). So the audio loop only marks cells; this thread sends them.
static pthread_mutex_t dirty_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dirty_cond = PTHREAD_COND_INITIALIZER;
static unsigned dirty_pads;  // bit i: cell of pad i changed
static int screen_quit;

static void mark_dirty(int pad) {
    pthread_mutex_lock(&dirty_lock);
    dirty_pads |= 1u << pad;
    pthread_cond_signal(&dirty_cond);
    pthread_mutex_unlock(&dirty_lock);
}

static void flush_cells(unsigned pads) {  // one clip rectangle per changed cell
    struct drm_clip_rect clips[16];
    unsigned n = 0;
    for (int i = 0; i < 16; i++)
        if (pads >> i & 1) {
            int x, y;
            pad_cell(i, &x, &y);
            clips[n++] = (struct drm_clip_rect){x, y, x + 80, y + 60};
        }
    struct drm_mode_fb_dirty_cmd dirty = {.fb_id = fb_id, .num_clips = n, .clips_ptr = (uintptr_t)clips};
    drm_call(DRM_IOCTL_MODE_DIRTYFB, &dirty, "dirtyfb");
}

static void *screen_thread(void *arg) {
    (void)arg;
    pthread_mutex_lock(&dirty_lock);
    while (!screen_quit) {
        if (!dirty_pads) { pthread_cond_wait(&dirty_cond, &dirty_lock); continue; }
        unsigned pads = dirty_pads;
        dirty_pads = 0;
        pthread_mutex_unlock(&dirty_lock);  // the slow SPI transfer runs without the lock
        flush_cells(pads);
        pthread_mutex_lock(&dirty_lock);
    }
    pthread_mutex_unlock(&dirty_lock);
    return NULL;
}

static void fill(int x0, int y0, int w, int h, uint32_t c) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) fb[y * fb_stride + x] = c;
}

static void draw_pad(int pad, uint32_t color) {
    int x, y;
    if (!fb) return;
    pad_cell(pad, &x, &y);
    fill(x, y, 80, 60, 0x404040);  // cell border
    fill(x + 2, y + 2, 76, 56, color);
    mark_dirty(pad);
}

static void realtime(void) {  // called after the screen thread exists, so only the audio thread gets SCHED_FIFO
    struct sched_param sp = {.sched_priority = 40};  // below the RT kernel's IRQ threads (50)
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) perror("mlockall");
    int e = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (e) fprintf(stderr, "SCHED_FIFO: %s\n", strerror(e));
    else puts("audio thread: SCHED_FIFO 40, memory locked");
}

static void handle(int midi, const unsigned char *m) {
    int i;
    if (m[0] == 0x99 && m[2] > 0 && (i = pad_index(m[1])) >= 0) {  // note on, channel 10
        if (!samples[i].lr) return;
        int r = m[2], b = 127 - m[2];                                  // soft hits blue, hard hits red
        trigger(i, m[2]);
        pad_color(midi, i, r, 0, b);
        draw_pad(i, (uint32_t)r << 17 | (uint32_t)b << 1);
    } else if (m[0] == 0xB0 && m[1] == CC_MAIN_VOLUME) {
        volume = volume_from_cc(m[2]);
        printf("volume %3d%%\n", m[2] * 100 / 127);
        fflush(stdout);
    }
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/media/MPC SAMPLE/sampler";
    if (!load_samples(dir)) { fprintf(stderr, "no playable WAV files in %s\n", dir); return 1; }
    int card = card_by_path("/dev/snd/by-path/platform-sound");
    if (card < 0) { fputs("I2S card not found\n", stderr); return 1; }
    print_switches(card);
    int midi = open_surface(O_RDWR | O_NONBLOCK);
    int pcm = midi < 0 ? -1 : open_pcm(card);
    if (pcm < 0) return 1;
    screen_open();
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;  // no SA_RESTART: a blocked PCM write must return on a signal
    sigaction(SIGINT, &sa, NULL), sigaction(SIGTERM, &sa, NULL), sigaction(SIGHUP, &sa, NULL);

    all_off(midi);
    pthread_t screen;
    if (fb && pthread_create(&screen, NULL, screen_thread, NULL) != 0) fputs("screen thread failed\n", stderr), fb = NULL;
    for (int i = 0; i < 16; i++) {  // pads with a sample: dim white; empty pads: dark
        if (samples[i].lr) pad_color(midi, i, 12, 12, 12);
        draw_pad(i, samples[i].lr ? 0x303030 : 0);
    }
    realtime();
    printf("volume %d%% until MAIN VOLUME is turned; hit pads, Ctrl-C to quit\n", (int)(volume * 100));
    fflush(stdout);
    struct parser p = {0};
    unsigned char buf[256];
    int32_t audio[2 * PERIOD];
    while (!stop) {  // the PCM write paces the loop: one 2.9 ms period per pass
        ssize_t n;
        while ((n = read(midi, buf, sizeof buf)) > 0)
            for (ssize_t k = 0; k < n; k++)
                if (feed(&p, buf[k])) handle(midi, p.msg);
        if (n < 0 && errno != EAGAIN && errno != EINTR) { perror("read"); break; }
        render(audio, PERIOD);
        if (pcm_write(pcm, audio, PERIOD) < 0) break;
    }
    all_off(midi);
    if (fb) {  // audio has stopped, so a blocking full-screen flush is fine here
        pthread_mutex_lock(&dirty_lock);
        screen_quit = 1;
        pthread_cond_signal(&dirty_cond);
        pthread_mutex_unlock(&dirty_lock);
        pthread_join(screen, NULL);
        memset(fb, 0, fb_bytes);
        flush_cells(0xFFFF);
    }
    close(pcm), close(midi);
    if (drm_fd >= 0) close(drm_fd);  // drops our framebuffer; ac50 sets its own mode on restart
    return 0;
}
#endif
