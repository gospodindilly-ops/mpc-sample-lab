[Русский](README.md) | **English**

# MPC Sample Lab

Personal research on the Akai MPC Sample (AC50, firmware 1.3.0.12). Everything already done is collected here: root access without changing the firmware, platform map, read-only eMMC backup, full control surface protocol, own audio and display output, and own Sample Mode firmware. Everything is verified on hardware. The firmware runs from RAM instead of the stock app and writes nothing to flash.

> The repo contains **no** Akai firmware, eMMC images, backups, or keys: `.gitignore` will not allow them to be committed. The control surface challenge-response key (XTEA) is intentionally omitted. `captures/`, `recon/`, and `backup-manifest/` describe a single device. Before publication (2026-10-23) they were checked: no serial numbers, MAC addresses, or keys are present, and partition hashes do not reveal their contents.

## What's inside

| Path | What it is |
|---|---|
| [docs/control-surface-protocol.md](docs/control-surface-protocol.md) | Control surface protocol: RBIN format, MCU, all pad, button, knob, and LED messages, SysEx. Marked what comes from code and what is verified on the device |
| [firmware/](firmware/) | **Own firmware, phase 1 — Sample Mode following the User Guide:**<br>• 8 banks × 16 pads, 32 voices;<br>• envelopes, tune, mono/poly, mute groups, loop, reverse, Note On;<br>• pages B1–B3, knobs K1–K3, encoder, Pad Volume fader;<br>• LEDs, screen with waveform and envelope.<br>`make test` — self-test on Mac/Linux, `make` — binary for the device |
| [docs/firmware-roadmap.md](docs/firmware-roadmap.md) | Every User Guide function by phase: what is done, what is next |
| [docs/audio-display.md](docs/audio-display.md) | Sound (I2S, AK4558, ALSA switches) and screen (SPI ST7796S via DRM) |
| [sampler/sampler.c](sampler/sampler.c) | A step towards the firmware: 16 WAV files from the SD card on the pads, MAIN VOLUME, real-time audio thread |
| [sound-test/sound_test.c](sound-test/sound_test.c) | Audio and display test: a tone per pad, pad grid via DRM |
| [panel-demo/panel.c](panel-demo/panel.c) | First own app: reads the surface, lights the LED of the pressed button, colors the hit pad |
| [tools/az0x.py](tools/az0x.py) | Parse and unpack `Update.img` (AZ0x container, read-only) |
| [tools/rbin.py](tools/rbin.py) | Parse `UpdateImage.rbin` of the panel firmware and helpers for disassembly (capstone) |
| [tools/emmc_backup.sh](tools/emmc_backup.sh) | Read-only backup of all eMMC partitions to SD with SHA-256 |
| [tools/surface_capture.sh](tools/surface_capture.sh) | 3-minute capture of surface messages (receive only); ac50 is always started again |
| [captures/](captures/) | Surface captures and run logs from the device |
| [recon/](recon/) | Read-only recon output on the device |
| [backup-manifest/](backup-manifest/) | Hashes and partition table of the eMMC backup, `Update.img` hash (without the images themselves) |
| [patcher/](patcher/) | Early patcher skeleton (history preserved). Outdated, see below |

## Device (confirmed)

| | |
|---|---|
| SoC | Rockchip **RK3566**, 4 × Cortex-A55, 2 GB RAM without swap |
| OS | Yocto "az0x" 5.0.14 (scarthgap), kernel 6.12.60-rt (PREEMPT_RT), BusyBox userland |
| eMMC 7.3 GB | 12 partitions: spl1/2, env1/2, uboot1/2, factory, boot, recovery, rootfs, content, data |
| Boot | U-Boot 2024.01. FIT of bootloader and kernel signed `sha256,rsa2048`, key `akai-1`; kernel, fdt, ramdisk, and cmdline are signed |
| Root FS | **dm-verity** (root hash in the signed cmdline). Writing to rootfs breaks boot |
| Writable | `/data` = `/var/lib` (p12). `/factory` (p7) — **do not touch** |
| SD | `/media/MPC SAMPLE`, mounted with `noexec` |
| Audio | I2S + AK4558; USB: UAC2 and f_midi (gadget) |
| Surface | separate STM32G0-class MCU on UART3, 4 Mbit/s, MIDI — see docs |
| Radio | no Wi-Fi; UART for Bluetooth disabled in DT |

Reference firmware: `Update.img` 1.3.0.12, SHA-256 in [backup-manifest/update-img.SHA256SUMS](backup-manifest/update-img.SHA256SUMS). The boot, recovery, and rootfs partitions on the device match it byte for byte.

## Device access

- **Root over SSH.** Temporary USB access is provided by the third-party kit [orrpunk-art/mpc-sample-root-ssh](https://github.com/orrpunk-art/mpc-sample-root-ssh) at commit `8334ca08` (read in full before use). Its `setup_unix.sh` creates the keys, and the `autorun/` folder is placed in the root of the SD (exFAT).
  ```bash
  ssh -p 2222 -i ~/.ssh/mpc_sample_client_ed25519 root@192.168.155.1
  ```
- **File copy** without sftp (the kit has none): `ssh … 'cat > /run/name' < name`.
- **Own binaries** run only from `/run` or `/tmp`: SD is `noexec`.
- **Running own app instead of the stock one.** systemd stops ac50 itself and always starts it again: after exit, crash or time limit.
  ```bash
  ssh … 'systemd-run --unit=NAME --collect -p RuntimeMaxSec=600 -p ExecStartPre="/usr/bin/systemctl stop ac50.service" -p ExecStopPost="/usr/bin/systemctl start ac50.service" /run/BINARY'
  ```

## eMMC backup

`ssh … 'sh -s' < tools/emmc_backup.sh` only reads eMMC and writes to `emmc-backup-DATE/` on SD. The 2026-10-22 backup is stored on SD and in `~/code/mpc-sample-analysis/emmc-backup-2026-10-22/`, verified with `shasum -c`.
- The `p12-data.img` partition was captured while it was changing. For `/data` use `p12-data-files.tar`.
- **Recovery method not verified.**

## Control surface: cheat sheet

Details in [docs/control-surface-protocol.md](docs/control-surface-protocol.md).
- **Pads:** channel 10. `99 note velocity`, then `A9 note pressure` and `89 note 00`. Pad notes 1–16: 37 36 42 82 40 38 46 44 48 47 45 43 49 55 51 53.
- **Buttons:** channel 1, `90 note 7F` and `80 note 00`. 24 buttons, table in docs, §8.
- **Knobs:** 1–4 → CC 19, 16, 17, 18. Fader → CC 20 (absolute 0–127). Big encoder → CC 100 (relative: `01` = +1, `7F` = −1).
- **Button LED:** `B0 <button note> <value>`. Single-color: 0 / 1 / ≥2. Two-color: 0–8 = 3·A + B.
- **Pad color:** `F0 47 7F 5C 65 00 04 <pad 0–15> <R> <G> <B> F7`, components are 7-bit.
- **No handshake needed:** the surface sends events even without the stock app.

## Demo app

```bash
cd panel-demo && zig cc -target aarch64-linux-musl -static -O2 -o mpc-panel-demo panel.c
```
```bash
cc -DSELFTEST -o panel-test panel.c && ./panel-test
```
Deploy: copy to `/run/mpc-panel-demo` and run per the template in the "Device access" section. Exit: `systemctl stop mpc-panel-demo` or Ctrl-C. Verified 2026-10-22: the surface is read, buttons light while held, pads color from blue to red by hit velocity.

## Own firmware

```bash
cd firmware && make test   # self-test of engine, controller, screen, and WAV (ASan/UBSan)
cd firmware && make        # static aarch64 binary (zig cc), ~100 KB
```
Deploy: copy to `/run/mpcfw` and run per the template in the "Device access" section, passing a folder with 44.1 kHz WAVs:
```bash
ssh … 'systemd-run --unit=mpcfw --collect -p RuntimeMaxSec=900 -p ExecStartPre="/usr/bin/systemctl stop ac50.service" -p ExecStopPost="/usr/bin/systemctl start ac50.service" /run/mpcfw "/media/MPC SAMPLE/folder"'
```
The first 128 files of the folder go to banks A–H. On exit the firmware prints CPU time per thread and the slowest render. Measured 2026-10-22: 3.3% of one core, slowest render 124 of 2902 µs, 0 underruns, 37 MB.

## patcher/

Skeleton from 2026-10-19: patches the app binary in RAM with bind-mount. **Not run on hardware yet.** Its activation method (`sdcard/scripts/autorun`) does not match the verified stock autorun mechanism (`autorun/autorun.json` + `autorun.check` in the SD root), so it must be reworked before use.

## Plan status

| # | Step | Status |
|---|---|---|
| 1 | Initial state | ✅ 1.3.0.12, project saved |
| 2 | Offline firmware analysis | ✅ |
| 3 | Read-only recon on the device | ✅ |
| 4 | Partition backup | ✅ two copies, verified |
| 5 | Own programs without changing the firmware | ✅ surface protocol, sound, screen, stage 1 firmware |
| 6 | Patches / alternative firmware | ⏳ depends on secure boot |

**Open:**
- hardware secure boot (eFuse) and recovery method (updater, recovery, maskrom);
- LED row CC 57–64 (presumably bank letters A–H), index 16 in 0x65, what 0x42 does;
- next firmware stages: filter, Chop, 16 Levels, browser, recording, sequencer, effects (see roadmap).
