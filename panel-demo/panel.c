// MPC Sample control-surface demo: prints pads/buttons/knobs, lights the held button, colours the hit pad.
// Protocol: docs/control-surface-protocol.md. Run only while ac50.service is stopped.
// Build:     zig cc -target aarch64-linux-musl -static -O2 -o mpc-panel-demo panel.c
// Self-test: cc -DSELFTEST -o panel-test panel.c && ./panel-test
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const unsigned char PAD_NOTE[16] = {37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53};

static const struct { unsigned char note; const char *name; } BUTTONS[] = {
    {6, "SAMPLE"}, {7, "SEQ"}, {1, "PAD FX"}, {0, "KNOB FX"}, {49, "SHIFT"}, {35, "PAD BANK"},
    {9, "ERASE"}, {11, "NOTE REPEAT"}, {12, "B1"}, {13, "B2"}, {14, "B3"}, {75, "CHOP"},
    {43, "MUTE"}, {74, "LOOP"}, {40, "16 LEVELS"}, {36, "SAMPLE SELECT"}, {53, "TAP TEMPO"},
    {111, "ENCODER PUSH"}, {55, "MINUS"}, {54, "PLUS"}, {73, "SAMPLE RECORD"}, {80, "SEQ RECORD"},
    {81, "STOP"}, {82, "PLAY"},
};
#define NBUTTONS (sizeof BUTTONS / sizeof BUTTONS[0])

static int pad_index(int note) {
    for (int i = 0; i < 16; i++)
        if (PAD_NOTE[i] == note) return i;
    return -1;
}

static const char *button_name(int note) {
    for (size_t i = 0; i < NBUTTONS; i++)
        if (BUTTONS[i].note == note) return BUTTONS[i].name;
    return NULL;
}

static const char *cc_name(int cc) {
    switch (cc) {  // physical order as turned on the owner's unit
    case 19: return "knob 1";
    case 16: return "knob 2";
    case 17: return "knob 3";
    case 18: return "knob 4";
    case 100: return "fader";
    case 20: return "encoder";
    }
    return NULL;
}

// SysEx 0x65 for one pad: F0 47 7F 5C 65 00 04 <pad> <R> <G> <B> F7, components 7-bit.
static void pad_color_msg(unsigned char m[12], int pad, int r, int g, int b) {
    static const unsigned char head[7] = {0xF0, 0x47, 0x7F, 0x5C, 0x65, 0x00, 0x04};
    memcpy(m, head, sizeof head);
    m[7] = pad & 0x7F, m[8] = r & 0x7F, m[9] = g & 0x7F, m[10] = b & 0x7F, m[11] = 0xF7;
}

// Channel-message parser; returns 1 when msg[] holds a complete message. SysEx and realtime are skipped.
struct parser { unsigned char msg[3]; int n, sysex; };

static int feed(struct parser *p, unsigned char c) {
    if (c >= 0xF8) return 0;  // realtime may appear inside a message
    if (c == 0xF0) { p->sysex = 1, p->n = 0; return 0; }
    if (c >= 0xF1) { p->sysex = 0, p->n = 0; return 0; }  // F7 and system common: ignored
    if (c & 0x80) { p->sysex = 0, p->msg[0] = c, p->n = 1; return 0; }
    if (p->sysex || p->n == 0) return 0;  // the panel never uses running status
    p->msg[p->n++] = c;
    int len = (p->msg[0] & 0xE0) == 0xC0 ? 2 : 3;  // program change / channel pressure are 2 bytes
    if (p->n < len) return 0;
    p->n = 0;
    return 1;
}

#ifdef SELFTEST
#include <assert.h>
int main(void) {
    const unsigned char in[] = {0x99, 37, 100, 0xA9, 37, 0xF8, 5, 0xF0, 0x47, 0x01, 0xF7, 0x90, 6, 127, 0xC0, 5};
    const unsigned char want[][3] = {{0x99, 37, 100}, {0xA9, 37, 5}, {0x90, 6, 127}, {0xC0, 5, 0}};
    struct parser p = {0};
    int got = 0;
    for (size_t i = 0; i < sizeof in; i++)
        if (feed(&p, in[i])) {
            assert(got < 4 && p.msg[0] == want[got][0] && p.msg[1] == want[got][1]);
            assert(p.msg[0] == 0xC0 || p.msg[2] == want[got][2]);
            got++;
        }
    assert(got == 4);
    unsigned char m[12];
    pad_color_msg(m, 15, 127, 0, 64);
    const unsigned char exp[12] = {0xF0, 0x47, 0x7F, 0x5C, 0x65, 0x00, 0x04, 15, 127, 0, 64, 0xF7};
    assert(memcmp(m, exp, 12) == 0);
    assert(pad_index(37) == 0 && pad_index(53) == 15 && pad_index(60) == -1);
    assert(strcmp(button_name(111), "ENCODER PUSH") == 0 && button_name(2) == NULL);
    assert(strcmp(cc_name(100), "fader") == 0 && cc_name(5) == NULL);
    puts("selftest ok");
    return 0;
}
#else
static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s, stop = 1; }

static void send_bytes(int fd, const unsigned char *b, size_t n) {
    if (write(fd, b, n) != (ssize_t)n) perror("write");
}

static void led(int fd, int note, int value) {  // value 8: full on for single and bicolour LEDs
    unsigned char m[3] = {0xB0, note & 0x7F, value & 0x7F};
    send_bytes(fd, m, 3);
}

static void pad_color(int fd, int pad, int r, int g, int b) {
    unsigned char m[12];
    pad_color_msg(m, pad, r, g, b);
    send_bytes(fd, m, 12);
}

static void all_off(int fd) {
    for (size_t i = 0; i < NBUTTONS; i++) led(fd, BUTTONS[i].note, 0);
    for (int i = 0; i < 16; i++) pad_color(fd, i, 0, 0, 0);
}

static void handle(int fd, const unsigned char *m) {
    int st = m[0] & 0xF0, ch = (m[0] & 0x0F) + 1, d1 = m[1], d2 = m[2], i;
    const char *name;
    if (ch == 10 && st == 0x90 && d2 > 0 && (i = pad_index(d1)) >= 0) {
        printf("pad %-9d velocity %3d\n", i + 1, d2);
        pad_color(fd, i, d2, 0, 127 - d2);  // soft hits blue, hard hits red
    } else if (ch == 1 && (st == 0x90 || st == 0x80) && (name = button_name(d1))) {
        int down = st == 0x90 && d2 > 0;
        printf("%-13s %s\n", name, down ? "down" : "up");
        led(fd, d1, down ? 8 : 0);
    } else if (ch == 1 && st == 0xB0 && (name = cc_name(d1))) {
        printf("%-13s %3d\n", name, d2);
    }
    fflush(stdout);
}

int main(void) {
    // The surface's card number changes between boots; the UART3 by-path link does not.
    char link[64], path[32];
    ssize_t n = readlink("/dev/snd/by-path/platform-fe670000.serial", link, sizeof link - 1);
    const char *c = n > 0 ? (link[n] = 0, strstr(link, "controlC")) : NULL;
    if (!c) { fputs("control surface not found\n", stderr); return 1; }
    snprintf(path, sizeof path, "/dev/snd/midiC%sD0", c + 8);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: %s%s\n", path, strerror(errno), errno == EBUSY ? " (stop ac50.service first)" : "");
        return 1;
    }
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;  // no SA_RESTART: read() must return on a signal
    sigaction(SIGINT, &sa, NULL), sigaction(SIGTERM, &sa, NULL), sigaction(SIGHUP, &sa, NULL);

    all_off(fd);
    printf("listening on %s, Ctrl-C to quit\n", path);
    fflush(stdout);
    struct parser p = {0};
    unsigned char buf[256];
    while (!stop) {
        n = read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        for (ssize_t k = 0; k < n; k++)
            if (feed(&p, buf[k])) handle(fd, p.msg);
    }
    all_off(fd);
    close(fd);
    return 0;
}
#endif
