[Русский](audio-display.md) | **English**

# MPC Sample (AC50): sound and display

Sources: DT from the `kernel.fit` of firmware 1.3.0.12 ([`tools/dtdump.py`](../tools/dtdump.py)), rootfs, the `MPC Sample` binary.
Live state was captured read-only while `ac50` was running: [`recon/audio-display-ro-20261022.txt`](../recon/audio-display-ro-20261022.txt).
Tags: **[DT]**, **[rootfs]**, **[live]** — confirmed; **[hypothesis]** — requires verification.

## 1. Sound

### Hardware signal path **[DT]**
```
I2S-TDM controller i2s@fe410000 (master BCLK/LRCK, mclk = 512·fs)
        │ I2S
AK4558 (i2c@fe5b0000, address 0x10; power analog-pwr, PDN-GPIO)
  DAC L/R → LOUT/ROUT ─┬→ Mute (GPIO, mute 50 ms / unmute 1000 ms) ─┬→ Headphones (detect GPIO3_B0)
                        │                                             └→ Line Out Jack (detect GPIO3_B1)
                        └→ Speaker amp (GPIO) → Builtin Speaker
  ADC L/R ← LIN/RIN ←─┬─ Line Input amp "TRS" (GPIO)
                       └─ Internal Mic amp (GPIO + vdd-mic)
```
- The ALSA card is `simple-audio-card` "I2S", format i2s, **the sample rate is fixed at 44100 Hz** (`convert-rate`).
- DAPM switches: `Builtin Speaker`, `Builtin Microphone`, `TRS`. The jack detectors appear as input devices "I2S Headphones" and "I2S Line Out Jack".
- There is no `asound.conf` and no UCM **[rootfs]**: the app opens the `hw:` devices directly. There is no `amixer` on the device.

### How the stock app uses it **[live]**
| Card | Direction | Format | Channels | Sample rate | period / buffer |
|---|---|---|---|---|---|
| I2S | playback | S32_LE | 2 | 44100 | 128 / 384 frames (2.9 / 8.7 ms) |
| I2S | capture | S32_LE | 2 | 44100 | 128 / 256 |
| UAC2Gadget (USB audio to the computer) | play / capture | S32_LE | 2 | 44100 | 24 / 384 |

The log line `Audio 44100Hz; 4-in; 4-out; 128sample buffer` is I2S 2 channels plus USB 2 channels. The binary contains `Failed to set S32_LE format for …`.
The card number changes between boots, so it must be looked up via `/dev/snd/by-path/platform-sound`: this string is present in the app binary.

### ALSA controls of the I2S card **[rootfs + app binary]**
The source of the names is kernel module strings and the app's `.rodata`. The logic comes from disassembling `MPC Sample`.

| Control | Source | Type | Who controls it |
|---|---|---|---|
| `Builtin Speaker Switch` | `pin-switches` in simple-card (`<pin> Switch`) | bool | app |
| `Builtin Microphone Switch` | same | bool | app |
| `TRS Switch` (line in) | same | bool | app |
| `Headphones Jack`, `Line Out Jack` | simple-card jack detectors (`hp-det` / `out-det` GPIO) | bool, read-only | kernel; the app reads |
| `Playback Volume` | AK4558 (inMusic driver) | int | **nobody**: the app does not use it, the volume is apparently software **[hypothesis]** |
| `Playback Deemphasis`, `Playback Digital Filter`, `Capture Digital Filter`, `Capture HPF`, `Playback Attentuation Transition Time` | AK4558 | enum/bool | nobody |

The amplifiers (`simple-audio-amplifier`: Speaker, Line Input, Internal Mic) and the mute circuit (`simple-audio-mute`) have no controls of their own. Their GPIO is switched by DAPM when the path is active. The mute circuit has a **1000 ms release delay**, so it is better to keep the PCM stream open continuously rather than open it for every note. There is no saved mixer state in rootfs: neither `alsactl` nor `asound.state`.

**App policy** (function `0x14EA4D0`; writing a value — the only call to `snd_ctl_elem_value_set_boolean` + `snd_ctl_elem_write` in `0x23E6030`):
- **Input source** (a setting, values 0–3):
  - 1 → `Builtin Microphone Switch` = 1, `TRS Switch` = 0;
  - 2 → `TRS Switch` = 1, `Builtin Microphone Switch` = 0;
  - 0 and 3 → both off.
- **`Builtin Speaker Switch` = 1** only if **neither headphones nor line out are plugged in**, and at the same time the condition "input = built-in microphone and the anti-feedback setting is on" (the string `Disable Internal Mic While Speaker Is On`) does not hold.
- **Headphones and line out** are not switched separately: their DAPM pins are switched on and off by the kernel itself according to the jack detectors.

**What this means for your own app:** after ac50 stops, the control values remain as the app left them, because the kernel stores them **[hypothesis]**. To control the speaker yourself, it is enough to write `Builtin Speaker Switch` (bool) through the ALSA control API (`/dev/snd/controlC<N>`).

## 2. Display

### Hardware **[DT, live]**
- **SPI display**, not RGB/MIPI. Its connection is described in `spi@fe640000/display@0`, `compatible = vitek,vt024k-zn01`. The kernel binds the **DRM driver `st7796s`** to it (Sitronix ST7796S controller, based on `drm_mipi_dbi`), device `spi3.0`.
- SPI up to **25 MHz**, the D/C line is GPIO3_A7, reset is GPIO3_A6, and the DT sets a rotation of 270°.
- **Mode 320×240**, connector `SPI-1`, plane in **XR24 (XRGB8888)** format, stride 1280.
- **fbdev emulation:** `/dev/fb0` "st7796sdrmfb", 320×240, 32 bits per pixel.
- **Backlight:** `pwm-backlight`, levels 0, 8, 16, 32, 64, 128, 255 (indices 0–6). Currently 6 of 6.
- **GPU and VOP are disabled in the DT.** So all drawing is software-only. The app draws on the CPU (the buffer is allocated by the "JUCE Message Thread") and holds `/dev/video0` = `rockchip-rga`, a 2D accelerator, apparently for copying and conversion **[hypothesis]**.
- **Speed limit:** a 320×240 frame at 16 bits per pixel over 25 MHz SPI gives about **20 fps** for a full screen **[hypothesis: panel in RGB565]**.

### Who holds the display **[live]**
`az0x-splash` (PID 111): `/dev/dri/card0` and `/dev/fb0`; `MPC Sample`: `/dev/dri/card0` and `/dev/video0`.

## 3. What follows from this for your own tool

| Task | Simplest path | Risk / to check |
|---|---|---|
| Sound out | Open I2S `hw:` with the same parameters: S32_LE, 2 channels, 44100 Hz, period 128. Do this when ac50 is stopped | Need to check whether the speaker and unmuting stay on after the app stops. The app sets the DAPM switches, and there is no `amixer` to check **[hypothesis]** |
| Sound via USB to the computer | Card `UAC2Gadget`, the same parameters | The gadget brings up `az0x-usb-gadget@ac50`, that is, without the app it apparently stays active **[hypothesis]** |
| Picture | `mmap` for `/dev/fb0`, write XRGB8888 320×240 | Whether fbdev is displayed when ac50 is stopped while `az0x-splash` still holds the devices **[hypothesis]**. If not — switch to DRM dumb-buffer |
| Build | Like `panel-demo`: static musl. For ALSA — either raw PCM ioctls or building against the device's glibc with its `libasound.so.2` | The choice has not been made yet |

## 4. First test on the device (2026-10-22, `sound-test`, ac50 stopped)

Log: [`captures/sound-test-run1-20261022.txt`](../captures/sound-test-run1-20261022.txt).
- **Switches after ac50 stopped:** `Builtin Speaker Switch` on, `Builtin Microphone Switch` off, `TRS Switch` off, no jacks. The kernel preserves the state left by the app.
- **Sound works:** I2S opens with the app's parameters (S32_LE, 2 channels, 44100, 128 × 3), the stream is `RUNNING`, no data loss. 83 hits across all 16 pads in 37 s. The owner confirmed that the tones and colors of the pads were as intended.
- **The display stayed black,** although `/dev/fb0` opened and the write went through without errors. The DRM client list (`/sys/kernel/debug/dri/0/clients`) shows: master — `MPC Sample`, `az0x-splash` — a client without master rights, which has its own framebuffers, and fbdev is fb 37 "[fbcon]". When ac50 closes, its framebuffers are removed and the plane goes dark. At the same time `az0x-splash` keeps the device open, so the last close never happens and the kernel does not bring fbdev back to the screen. Thus `/dev/fb0` is written to a buffer that is not visible **[inference from DRM state]**.
- **Solution:** draw through DRM directly. Create a dumb buffer, `ADDFB2` XR24, `SETCRTC` (connector 32, CRTC 35, mode 320×240), and call **`DIRTYFB`** after every draw: mipi-dbi SPI panels are updated only on a damage signal. If the master turns out to be busy, `SETCRTC` will return `EACCES` **[verify]**.

### Second run: output via DRM (log [`captures/sound-test-run2-drm-20261022.txt`](../captures/sound-test-run2-drm-20261022.txt))
- `drm 320x240 crtc 35 connector 32 fb 41 pitch 1280`, no errors. After ac50 stopped, the test became the DRM master (`az0x-splash` was left without master), and the plane shows the test's buffer.
- **The display works:** a 4×4 grid is visible, the cells are colored like the pads, the orientation is correct (the driver performs the 270° rotation). Confirmed by the owner and a photo.
- **Problem: 20 sound dropouts (underrun) for 20 hits.** A full-screen `DIRTYFB` performs the SPI transfer inside the ioctl. That is 320×240 at 25 MHz, on the order of 50 ms, which is more than the 8.7 ms sound buffer. So the single-threaded loop loses sound on every redraw. In the first run through fbdev, without `DIRTYFB`, there were no dropouts.
- **What to do:** the screen must not be redrawn in the audio loop. Update only the changed cell (a clip of 80×60, about 1/16 of the frame) and call `DIRTYFB` from a separate thread.

### Third run: display thread and cell clips (log [`captures/sound-test-run3-thread-20261022.txt`](../captures/sound-test-run3-thread-20261022.txt))
- The audio loop only draws the cell into the buffer and sets a bit in the mask. A separate thread sends `DIRTYFB` with a rectangle for each changed cell.
- **77 hits across all 16 pads in 26 s, including simultaneous ones: 0 sound dropouts, 0 DRM errors.** The problem of the second run is fixed.
- The rule for your own apps: **the audio thread never waits for the SPI display.**

## 5. What has not been checked
- The current **values** of the ALSA controls: there is no `amixer` on the device. The names and logic were established offline (§1).
- The pixel format on the SPI bus (RGB565 or RGB666) and the real frame rate.
- The behavior of `/dev/fb0` and sound with ac50 stopped.
