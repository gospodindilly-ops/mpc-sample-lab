// Linux side of the MPC Sample: ALSA PCM and controls through raw ioctls, the SPI display through DRM, and
// real-time scheduling. Only built for the device (not part of the host self-test).
#pragma once
#include <stdint.h>

void hw_print_switches(int card);                   // read-only
int hw_pcm_open(int card, int rate, int period);    // playback, S32_LE stereo, 3 periods; fd or -1
int hw_pcm_write(int fd, int32_t *buf, int frames); // 0, or -1 on a fatal error (underruns are recovered)

extern uint32_t *hw_fb;                             // mapped DRM dumb buffer, XRGB8888, uncached: write only;
                                                    // NULL without screen
extern int hw_stride;                               // in pixels
void hw_screen_open(void);                          // leaves hw_fb NULL on failure, printing why
void hw_screen_flush(int x0, int y0, int x1, int y1); // sends [x0,x1) x [y0,y1) to the panel (SPI inside the ioctl)
void hw_screen_close(void);

void hw_realtime(int priority);                     // SCHED_FIFO for the calling thread + mlockall
