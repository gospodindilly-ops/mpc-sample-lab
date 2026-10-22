// MPC Sample sound + screen test: prints the I2S switches, then plays a short tone per pad hit (I2S)
// and draws a 4x4 pad grid through DRM. Docs: docs/audio-display.md. Run only while ac50.service is stopped.
// Build:     zig cc -target aarch64-linux-musl -static -O2 -I../panel-demo -o mpc-sound-test sound_test.c
// Self-test: cc -DSELFTEST -I../panel-demo -o sound-test-host sound_test.c -lm && ./sound-test-host
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include "surface.h"

#define RATE 44100
#define PERIOD 128                      // frames per write, same as the stock app
#define GAIN 0.15f                      // first test stays quiet
#define TONE_FRAMES (RATE * 15 / 100)   // 150 ms per hit

struct voice { float phase, step, amp; int left; };
static struct voice voices[16];

static void trigger(int pad, int velocity) {  // pads 1-16 are a chromatic scale from A3
    struct voice *v = &voices[pad];
    v->phase = 0;
    v->step = 2.0f * (float)M_PI * 220.0f * powf(2.0f, pad / 12.0f) / RATE;
    v->amp = GAIN * velocity / 127.0f;
    v->left = TONE_FRAMES;
}

// Mixes all voices into interleaved stereo S32_LE.
static void render(int32_t *out, int frames) {
    for (int f = 0; f < frames; f++) {
        float s = 0;
        for (int i = 0; i < 16; i++) {
            struct voice *v = &voices[i];
            if (v->left <= 0) continue;
            s += sinf(v->phase) * v->amp * v->left / TONE_FRAMES;  // linear fade-out: no click at the end
            v->phase += v->step;
            if (v->phase > 2 * (float)M_PI) v->phase -= 2 * (float)M_PI;
            v->left--;
        }
        s = s > 1 ? 1 : s < -1 ? -1 : s;
        out[2 * f] = out[2 * f + 1] = (int32_t)(s * 2147483520.0f);  // largest float below 2^31
    }
}

// MPC layout: pad 1 bottom-left, pad 16 top-right; 320x240 screen in 80x60 cells.
static void pad_cell(int pad, int *x, int *y) { *x = pad % 4 * 80, *y = (3 - pad / 4) * 60; }

#ifdef SELFTEST
#include <assert.h>
int main(void) {
    int32_t buf[2 * PERIOD];
    render(buf, PERIOD);
    for (int i = 0; i < 2 * PERIOD; i++) assert(buf[i] == 0);
    trigger(0, 127);
    assert(fabsf(voices[0].step - 2 * (float)M_PI * 220 / RATE) < 1e-6f);
    render(buf, PERIOD);
    int peak = 0;
    for (int f = 0; f < PERIOD; f++) {
        assert(buf[2 * f] == buf[2 * f + 1]);
        peak = abs(buf[2 * f]) > peak ? abs(buf[2 * f]) : peak;
    }
    assert(peak > 0 && peak <= (int)(GAIN * 2147483520.0f));
    for (int k = 0; k < TONE_FRAMES / PERIOD + 1; k++) render(buf, PERIOD);
    assert(voices[0].left == 0 && buf[0] == 0);
    for (int i = 0; i < 16; i++) trigger(i, 127);  // all pads at once must clip, not wrap
    render(buf, PERIOD);
    int x, y;
    pad_cell(0, &x, &y), assert(x == 0 && y == 180);
    pad_cell(15, &x, &y), assert(x == 240 && y == 0);
    puts("selftest ok");
    return 0;
}
#else
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <sound/asound.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s, stop = 1; }

static void print_switches(int card) {  // read-only: ELEM_READ never changes a control
    static const struct { int iface; const char *name; } ctl[] = {
        {SNDRV_CTL_ELEM_IFACE_MIXER, "Builtin Speaker Switch"}, {SNDRV_CTL_ELEM_IFACE_MIXER, "Builtin Microphone Switch"},
        {SNDRV_CTL_ELEM_IFACE_MIXER, "TRS Switch"}, {SNDRV_CTL_ELEM_IFACE_CARD, "Headphones Jack"},
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
            printf("%-26s ? (%s)\n", ctl[i].name, strerror(errno));
        else
            printf("%-26s %s\n", ctl[i].name, v.value.integer.value[0] ? "on" : "off");
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

static void screen_open(void) {  // optional: the test still plays sound without a screen
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
    printf("drm %ux%u crtc %u connector %u fb %u pitch %u\n", mode.hdisplay, mode.vdisplay, crtcs[0], conns[0], fb_id, dumb.pitch);
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

static void handle(int midi, const unsigned char *m) {
    int i;
    if (m[0] == 0x99 && m[2] > 0 && (i = pad_index(m[1])) >= 0) {  // note on, channel 10
        int r = m[2], b = 127 - m[2];                                  // soft hits blue, hard hits red
        trigger(i, m[2]);
        pad_color(midi, i, r, 0, b);
        draw_pad(i, (uint32_t)r << 17 | (uint32_t)b << 1);
        printf("pad %-2d velocity %3d\n", i + 1, m[2]);
        fflush(stdout);
    }
}

int main(void) {
    int card = card_by_path("/dev/snd/by-path/platform-sound");
    if (card < 0) { fputs("I2S card not found\n", stderr); return 1; }
    printf("I2S card %d\n", card);
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
    for (int i = 0; i < 16; i++) draw_pad(i, 0);
    puts("hit pads, Ctrl-C to quit");
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
