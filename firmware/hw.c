// Linux side, see hw.h. PCM and DRM code proven in sound-test/ and sampler/.
#include "hw.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <sound/asound.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

void hw_print_switches(int card) {  // ELEM_READ never changes a control
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
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) < 0) printf("%-24s ? (%s)\n", ctl[i].name, strerror(errno));
        else printf("%-24s %s\n", ctl[i].name, v.value.integer.value[0] ? "on" : "off");
    }
    close(fd);
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

int hw_pcm_open(int card, int rate, int period) {
    char path[32];
    snprintf(path, sizeof path, "/dev/snd/pcmC%dD0p", card);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: %s%s\n", path, strerror(errno), errno == EBUSY ? " (stop ac50.service first)" : "");
        return -1;
    }
    struct snd_pcm_hw_params hw;
    memset(&hw, 0, sizeof hw);
    for (int n = SNDRV_PCM_HW_PARAM_FIRST_MASK; n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++)
        memset(hw.masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits, 0xff, sizeof hw.masks[0].bits);
    for (int n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++)
        hw.intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = ~0u;
    hw.rmask = ~0u, hw.info = ~0u;
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S32_LE);
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_RATE, rate);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, period);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 3 * period);
    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) < 0 || ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) {
        perror("pcm setup");
        close(fd);
        return -1;
    }
    return fd;
}

int hw_pcm_write(int fd, int32_t *buf, int frames) {
    struct snd_xferi x = {.buf = buf, .frames = frames};
    while (ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
        if (errno == EINTR) return 0;  // a signal: let the main loop see it
        if (errno != EPIPE || ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) { perror("pcm write"); return -1; }
        fputs("underrun\n", stderr);
    }
    return 0;
}

// /dev/fb0 is never shown while az0x-splash keeps DRM open, so draw into our own DRM dumb buffer.
uint32_t *hw_fb;
int hw_stride;
static int drm_fd = -1;
static uint32_t fb_id;
static size_t fb_bytes;

static int drm_call(unsigned long req, void *arg, const char *what) {
    int r;
    while ((r = ioctl(drm_fd, req, arg)) < 0 && errno == EINTR) {}
    if (r < 0) fprintf(stderr, "drm %s: %s\n", what, strerror(errno));
    return r;
}

void hw_screen_open(void) {
    drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) { perror("/dev/dri/card0"); return; }
    if (ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0) < 0) fprintf(stderr, "drm set master: %s\n", strerror(errno));
    uint32_t crtcs[4], conns[4];
    struct drm_mode_card_res res = {.crtc_id_ptr = (uintptr_t)crtcs, .connector_id_ptr = (uintptr_t)conns,
                                    .count_crtcs = 4, .count_connectors = 4};  // kernel fills up to these counts
    if (drm_call(DRM_IOCTL_MODE_GETRESOURCES, &res, "getresources") < 0) return;
    if (!res.count_crtcs || !res.count_connectors) { fputs("drm: no crtc or connector\n", stderr); return; }
    struct drm_mode_modeinfo modes[8];
    struct drm_mode_get_connector con = {.connector_id = conns[0]};
    if (drm_call(DRM_IOCTL_MODE_GETCONNECTOR, &con, "getconnector") < 0) return;  // first call probes, counts modes
    if (!con.count_modes || con.count_modes > 8) { fprintf(stderr, "drm: %u modes\n", con.count_modes); return; }
    con = (struct drm_mode_get_connector){.connector_id = conns[0], .modes_ptr = (uintptr_t)modes, .count_modes = con.count_modes};
    if (drm_call(DRM_IOCTL_MODE_GETCONNECTOR, &con, "getconnector modes") < 0) return;
    struct drm_mode_modeinfo mode = modes[0];
    if (mode.hdisplay != 320 || mode.vdisplay != 240) { fprintf(stderr, "drm: mode %ux%u\n", mode.hdisplay, mode.vdisplay); return; }
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
    hw_fb = p, hw_stride = dumb.pitch / 4, fb_bytes = dumb.size, fb_id = add.fb_id;
    printf("drm %ux%u crtc %u connector %u fb %u\n", mode.hdisplay, mode.vdisplay, crtcs[0], conns[0], fb_id);
}

void hw_screen_flush(int x0, int y0, int x1, int y1) {  // mipi-dbi panels only update on DIRTYFB
    struct drm_clip_rect clip = {(unsigned short)x0, (unsigned short)y0, (unsigned short)x1, (unsigned short)y1};
    struct drm_mode_fb_dirty_cmd dirty = {.fb_id = fb_id, .num_clips = 1, .clips_ptr = (uintptr_t)&clip};
    if (hw_fb && x1 > x0 && y1 > y0) drm_call(DRM_IOCTL_MODE_DIRTYFB, &dirty, "dirtyfb");
}

void hw_screen_close(void) {
    if (hw_fb) memset(hw_fb, 0, fb_bytes), hw_screen_flush(0, 0, 320, 240);
    if (drm_fd >= 0) close(drm_fd);  // drops our framebuffer; ac50 sets its own mode on restart
    hw_fb = NULL, drm_fd = -1;
}

void hw_realtime(int priority) {
    struct sched_param sp = {.sched_priority = priority};
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) perror("mlockall");
    int e = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (e) fprintf(stderr, "SCHED_FIFO: %s\n", strerror(e));
    else printf("audio thread: SCHED_FIFO %d, memory locked\n", priority);
}
