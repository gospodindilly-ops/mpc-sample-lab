// Own MPC Sample firmware, phase 1: Sample Mode (see docs/firmware-roadmap.md). Runs from RAM while
// ac50.service is stopped; changes nothing on the device.
// Usage: mpcfw ["/media/MPC SAMPLE/sampler"]   (the first 128 WAV files of the folder fill banks A..H)
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "app.h"
#include "files.h"
#include "hw.h"
#include "surface.h"
#include "ui.h"

#define PERIOD 128  // frames per write, same as the stock app: 2.9 ms

static struct engine engine;
static struct app app;
static struct sequencer seq;  // 1 MB of events: static, never allocated
static struct files files;
static volatile sig_atomic_t stop;
static int ui_quit;
static void on_signal(int s) { (void)s, stop = 1; }
static double secs(clockid_t c) { struct timespec t; clock_gettime(c, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double ui_cpu;

// Draws a frame when the audio thread asks for one and sends only the changed rectangles: a full frame takes
// ~50 ms over SPI, so the audio thread never waits for the screen. `front` mirrors the panel in cached RAM,
// because the DRM buffer is uncached and reading it back cost more than drawing.
static void *ui_thread(void *arg) {
    static uint32_t back[UI_W * UI_H], front[UI_W * UI_H];  // hw_screen_open clears the panel: front starts equal
    static int rect[UI_H][4];
    const struct timespec tick = {0, 20 * 1000 * 1000};
    (void)arg;
    while (!__atomic_load_n(&ui_quit, __ATOMIC_ACQUIRE)) {
        if (__atomic_exchange_n(&app.redraw, 0, __ATOMIC_ACQ_REL)) {
            unsigned upto = __atomic_load_n(&app.grave_n, __ATOMIC_ACQUIRE);
            ui_draw(&app, back, UI_W);
            for (int i = 0, n = hw_fb ? ui_changes(back, front, rect) : 0; i < n; i++) {
                int x0 = rect[i][0], y0 = rect[i][1], x1 = rect[i][2], y1 = rect[i][3];
                for (int y = y0; y < y1; y++) memcpy(hw_fb + y * hw_stride + x0, front + y * UI_W + x0, (x1 - x0) * 4);
                hw_screen_flush(x0, y0, x1, y1);
            }
            ui_free_retired(&app, upto);
        }
        nanosleep(&tick, NULL);
    }
    ui_cpu = secs(CLOCK_THREAD_CPUTIME_ID);
    return NULL;
}

// An external MIDI port (TRS jacks or the USB gadget), found by name: card numbers change between boots. The
// first line of /proc/asound/cardN/midi0 is the port's name. The panel's own port is never chosen.
static int midi_port(const char *name, const char *alt) {
    int panel = card_by_path("/dev/snd/by-path/platform-fe670000.serial");
    for (int c = 0; c < 16; c++) {
        char path[64], line[96] = "", id[32] = "";
        snprintf(path, sizeof path, "/proc/asound/card%d/midi0", c);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (!fgets(line, sizeof line, f)) line[0] = 0;
        fclose(f);
        snprintf(path, sizeof path, "/proc/asound/card%d/id", c);
        if ((f = fopen(path, "r"))) { if (!fgets(id, sizeof id, f)) id[0] = 0; fclose(f); }
        if (c == panel || !(strstr(line, name) || strstr(id, name) || (alt && (strstr(line, alt) || strstr(id, alt))))) continue;
        snprintf(path, sizeof path, "/dev/snd/midiC%dD0", c);
        int fd = open(path, O_WRONLY | O_NONBLOCK);
        printf("MIDI %s: card %d%s\n", name, c, fd < 0 ? ", cannot open" : "");
        return fd;
    }
    printf("MIDI %s: not found\n", name);
    return -1;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/media/MPC SAMPLE/sampler";
    engine_init(&engine);
    seq_init(&seq);
    app_init(&app, &engine, &seq);
    app.jobs = &files.req;
    int card = card_by_path("/dev/snd/by-path/platform-sound");
    if (card < 0) { fputs("I2S card not found\n", stderr); return 1; }
    hw_print_switches(card);
    int midi = open_surface(O_RDWR | O_NONBLOCK);
    int pcm = midi < 0 ? -1 : hw_pcm_open(card, RATE, PERIOD);
    if (pcm < 0) return 1;
    hw_screen_open();  // optional: the firmware still plays without a screen
    int ext[2] = {midi_port("External", NULL), midi_port("f_midi", "Gadget")};  // PORT_EXTERNAL, PORT_USB: MIDI tracks
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;  // no SA_RESTART: a blocked PCM write must return on a signal
    sigaction(SIGINT, &sa, NULL), sigaction(SIGTERM, &sa, NULL), sigaction(SIGHUP, &sa, NULL);
    pthread_t ui;
    if (pthread_create(&ui, NULL, ui_thread, NULL)) { fputs("ui thread failed\n", stderr); return 1; }
    if (files_start(&files, dir)) { fputs("file thread failed\n", stderr); return 1; }  // loads the folder in the background
    hw_realtime(40);  // after the other threads exist, so only this thread runs SCHED_FIFO; below IRQ threads (50)
    puts("ready: Sample Mode, SEQ for sequences");
    fflush(stdout);

    struct parser p = {0};
    unsigned char buf[256];
    int32_t audio[2 * PERIOD];
    double worst = 0, start = secs(CLOCK_MONOTONIC);
    while (!stop) {
        ssize_t n;
        while ((n = read(midi, buf, sizeof buf)) > 0)
            for (ssize_t k = 0; k < n; k++)
                if (feed(&p, buf[k])) app_midi(&app, p.msg);
        if (n < 0 && errno != EAGAIN && errno != EINTR) { perror("read"); break; }
        struct job j;
        while (q_pop(&files.res, &j)) app_result(&app, &j);
        app_tick(&app, PERIOD);
        if (app.out_len) send_bytes(midi, app.out, app.out_len), app.out_len = 0;
        for (int i = 0; i < 2; i++) {  // shortcut: a full port drops the notes (never wait here); fine for pads, recheck for sequences
            if (app.ext_len[i] && ext[i] >= 0) (void)!write(ext[i], app.ext[i], app.ext_len[i]);
            app.ext_len[i] = 0;
        }
        double t0 = secs(CLOCK_MONOTONIC);
        engine_render(&engine, audio, PERIOD);
        t0 = secs(CLOCK_MONOTONIC) - t0, worst = t0 > worst ? t0 : worst;
        if (hw_pcm_write(pcm, audio, PERIOD) < 0) break;
    }
    __atomic_store_n(&ui_quit, 1, __ATOMIC_RELEASE);
    pthread_join(ui, NULL);
    files_stop(&files);
    printf("cpu over %.0f s: audio thread %.1f s, screen thread %.1f s; slowest render %.0f of %.0f us\n",
           secs(CLOCK_MONOTONIC) - start, secs(CLOCK_THREAD_CPUTIME_ID), ui_cpu, worst * 1e6, PERIOD * 1e6 / RATE);
    all_off(midi);
    hw_screen_close();
    close(pcm), close(midi);
    for (int i = 0; i < 2; i++) if (ext[i] >= 0) close(ext[i]);
    return 0;
}
