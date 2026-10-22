// MPC Sample control surface: note/CC tables, MIDI parser and LED helpers shared by the demos.
// Protocol: docs/control-surface-protocol.md.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
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

static inline int pad_index(int note) {
    for (int i = 0; i < 16; i++)
        if (PAD_NOTE[i] == note) return i;
    return -1;
}

static inline const char *button_name(int note) {
    for (size_t i = 0; i < NBUTTONS; i++)
        if (BUTTONS[i].note == note) return BUTTONS[i].name;
    return NULL;
}

static inline const char *cc_name(int cc) {
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
static inline void pad_color_msg(unsigned char m[12], int pad, int r, int g, int b) {
    static const unsigned char head[7] = {0xF0, 0x47, 0x7F, 0x5C, 0x65, 0x00, 0x04};
    memcpy(m, head, sizeof head);
    m[7] = pad & 0x7F, m[8] = r & 0x7F, m[9] = g & 0x7F, m[10] = b & 0x7F, m[11] = 0xF7;
}

// Channel-message parser; returns 1 when msg[] holds a complete message. SysEx and realtime are skipped.
struct parser { unsigned char msg[3]; int n, sysex; };

static inline int feed(struct parser *p, unsigned char c) {
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

static inline void send_bytes(int fd, const unsigned char *b, size_t n) {
    if (write(fd, b, n) != (ssize_t)n) perror("write");
}

static inline void led(int fd, int note, int value) {  // value 8: full on for single and bicolour LEDs
    unsigned char m[3] = {0xB0, note & 0x7F, value & 0x7F};
    send_bytes(fd, m, 3);
}

static inline void pad_color(int fd, int pad, int r, int g, int b) {
    unsigned char m[12];
    pad_color_msg(m, pad, r, g, b);
    send_bytes(fd, m, 12);
}

static inline void all_off(int fd) {
    for (size_t i = 0; i < NBUTTONS; i++) led(fd, BUTTONS[i].note, 0);
    for (int i = 0; i < 16; i++) pad_color(fd, i, 0, 0, 0);
}

// ALSA card numbers change between boots; /dev/snd/by-path links ("../controlC2") do not.
static inline int card_by_path(const char *by_path) {
    char link[64];
    ssize_t n = readlink(by_path, link, sizeof link - 1);
    const char *c = n > 0 ? (link[n] = 0, strstr(link, "controlC")) : NULL;
    return c ? atoi(c + 8) : -1;
}

static inline int open_surface(int flags) {
    char path[32];
    int card = card_by_path("/dev/snd/by-path/platform-fe670000.serial");
    if (card < 0) { fputs("control surface not found\n", stderr); return -1; }
    snprintf(path, sizeof path, "/dev/snd/midiC%dD0", card);
    int fd = open(path, flags);
    if (fd < 0)
        fprintf(stderr, "%s: %s%s\n", path, strerror(errno), errno == EBUSY ? " (stop ac50.service first)" : "");
    return fd;
}
