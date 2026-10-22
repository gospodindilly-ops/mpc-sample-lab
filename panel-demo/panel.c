// MPC Sample control-surface demo: prints pads/buttons/knobs, lights the held button, colours the hit pad.
// Protocol: docs/control-surface-protocol.md. Run only while ac50.service is stopped.
// Build:     zig cc -target aarch64-linux-musl -static -O2 -o mpc-panel-demo panel.c
// Self-test: cc -DSELFTEST -o panel-test panel.c && ./panel-test
#include <signal.h>
#include "surface.h"

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
    int fd = open_surface(O_RDWR);
    if (fd < 0) return 1;
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;  // no SA_RESTART: read() must return on a signal
    sigaction(SIGINT, &sa, NULL), sigaction(SIGTERM, &sa, NULL), sigaction(SIGHUP, &sa, NULL);

    all_off(fd);
    puts("listening on the control surface, Ctrl-C to quit");
    fflush(stdout);
    struct parser p = {0};
    unsigned char buf[256];
    while (!stop) {
        ssize_t n = read(fd, buf, sizeof buf);
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
