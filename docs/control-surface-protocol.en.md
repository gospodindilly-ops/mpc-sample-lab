[Русский](control-surface-protocol.md) | **English**

# MPC Sample (AC50): control surface protocol

Source: `/usr/share/Akai/AC50/Firmware/AC50/UpdateImage.rbin` from firmware 1.3.0.12
(77 784 bytes, SHA-256 `73cb081b24dfe85549ffcd92730c19762058b848c77cf45f3dfa0ec0ac01a9f4`).
Method: offline container parsing and disassembly (capstone, Thumb/M-class). The device was not used.
Tool: [`tools/rbin.py`](../tools/rbin.py) (checks the CRC32 of each region).

Confidence markers: **[code]** — read in the MCU code; **[hypothesis]** — a conclusion that needs verification on the device.

## 1. RBIN format

The file is a sequence of blocks. A block:

| Offset | Field |
|---|---|
| 0x00 | `!Rbn` |
| 0x04 | `Copyright 2025 inMusic Brands` (up to 0x24) |
| 0x24 | 8 × u32 LE: `peripheral_id, slot, version, base, span, crc, block_len, n_regions` |
| 0x44 | n × {`addr, size, crc32, offset`}, offset — from the start of the block |

The region CRCs are ordinary CRC32 (zlib), all match. **[code]**

| slot | version | address | size | contents |
|---|---|---|---|---|
| 0 | 1.0.0.022 | 0x08009000 | 0x5800 | bootloader (the reset vector points to 0x0800xxxx below 0x08009000) **[hypothesis: a BL image stored in the intermediate area]** |
| 1 | 1.0.0.051 | 0x0800F000 | 0x11000 | control-surface app (matches the MCU version in the device log) |

`peripheral_id = 0x09E800FF` for both blocks.

## 2. Microcontroller

- **ARMv6-M (Cortex-M0/M0+)**: among the actual instructions there are no 32-bit Thumb-2 ones, except `bl/dmb/msr/mrs`. **[code]**
- **STM32G0 family**: RCC 0x40021000, DMA1 0x40020000, **DMAMUX 0x40020800**, IOPORT 0x50000000–0x50000C00, TIM1/3/6/7/14/15/16/17, USART1/2/3, I2C2, ADC are used. SRAM up to 0x20009000 (36 KB), flash up to 0x08020000 (128 KB). This fits, for example, STM32G070/G071xB. **[hypothesis: the exact model]**
- Interrupts in use: DMA1_Ch1, DMA1_Ch4–7/DMAMUX, TIM6, TIM7, SysTick. UART exchange goes through DMA.
- 4 Mbit/s = 64 MHz / 16, which matches the maximum G0 clock. **[hypothesis]**
- Flash map: 0x08000000 bootloader · 0x08009000 slot 0 image · 0x0800F000 app. The page 0x0800E800–0x0800F000 is not used by the image **[hypothesis: configuration/serial number]**.
- Source modules (from the paths in assert): `ctrl_platform/adc.c`, `buttons.c`, `controllers.c`; functions `InitButtons`, `InitFaders`, `HandleFaders`.
- The firmware contains a **TI bq (fuel gauge)** programming routine over I2C: the strings `BQ GAUGE: …`, `;SET_CFGUPDATE`. Apparently the MCU also services the battery charge sensor. **[hypothesis]**

## 3. Transport

MCU ⇄ UART3 RK3566 (`serial@fe670000`, 4 000 000 baud) ⇄ `snd-serial-generic` ⇄ ALSA rawmidi "Control Surface" ⇄ `MPC Sample`.
All channel messages are three bytes long and are sent without running status: the MCU always writes the status byte. **[code]** This matches the counter test, where every increment was a multiple of 3.

## 4. MCU → app

Sending goes through four wrappers around `midiGetQueue(port, buf, 3)`:
`0x08014534` Note Off · `0x08014558` Note On · `0x0801457C` Poly AT · `0x080145A0` CC. The channel is sent counted from 1, the data is masked with `& 0x7F`. **[code]**

### Pads — channel 10
| Event | Message |
|---|---|
| hit | `99 <note> <velocity>` |
| pressure (only on change) | `A9 <note> <pressure>` |
| release | `A9 <note> 00` (if there was pressure), then `89 <note> 00` |

Notes by pad index 0…15 (the `.data` table at 0x20000240, the original data in flash at 0x0801C930). **[code]**

| index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| note | 37 | 36 | 42 | 82 | 40 | 38 | 46 | 44 | 48 | 47 | 45 | 43 | 49 | 55 | 51 | 53 |

This is the classic MPC layout: index i corresponds to pad i+1. **[Verified on the device, §8]**

### Buttons — channel 1
Press `90 <note> 7F`, release `80 <note> 00` **[code]**. Debouncing is done on timer thresholds.
24 inputs, the table in flash at 0x0801BEA7:

| index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| note | 6 | 75 | 73 | 7 | 43 | 80 | 1 | 74 | 81 | 0 | 40 | 82 |

| index | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| note | 49 | 36 | 12 | 35 | 53 | 13 | 9 | 55 | 14 | 11 | 54 | 111 |

The button names were established on the device, see §8. The numbers overlap with the pad notes (36, 40, 43, 49, 53, 55, 82), but go on a different channel (1 versus 10).

### Analog controls — channel 1, CC
| index | CC | value |
|---|---|---|
| 0…4 | 16, 17, 18, 19, 20 | `ADC14bit >> 7`, sent only on change |
| 5 | 100 | passed straight through, via a separate path |

**[code]** A separate function `0x080172C4` sends the current values of CC 16–20 in one go. It is called by the SysEx command handler, i.e. on request from the app. Index 4 (CC 20) is the fader, and index 5 (CC 100) is the large encoder (§8). The Knob Takeover modes in MPC Sample 1.3 confirm that the knobs are absolute.

## 5. App → MCU

### Button LEDs — CC on channel 1
The controller number matches the button note. The handler `0x080170BC` turns a CC into an LED index via a lookup table, and `0x08010650` sets the glow from a descriptor (flash 0x0801BA60, 12 bytes per LED). **[code]** The channel in the MCU is stored starting from 1 (`(status & 0xF) + 1`, `0x080140FC`), so the status is `0xB0`.
**Verified on the device** (the `mpc-panel-demo` demo, 2026-10-22): `B0 <note> 08` lights the button LED, `B0 <note> 00` turns it off.

| Type | Values |
|---|---|
| single | 0 — off, 1 — dim (~18%), ≥2 — full |
| bicolour | 0…8 = 3·A + B, where A and B ∈ {0 off, 1 dim, 2 full} for the two dies |

| CC | LED | type | | CC | LED | type |
|---|---|---|---|---|---|---|
| 0 | 13 | bicolour | | 53 | 22 | bicolour |
| 1 | 12 | bicolour | | 54 | 25 | single |
| 6 | 10 | bicolour | | 55 | 24 | single |
| 7 | 11 | bicolour | | 57–64 | 0–7 | single |
| 9 | 16 | bicolour | | 73 | 26 | single |
| 11 | 17 | single | | 74 | 19 | bicolour |
| 35 | 15 | single | | 75 | 18 | bicolour |
| 36 | 21 | bicolour | | 80 | 27 | single |
| 40 | 20 | bicolour | | 81 | 28 | single |
| 43 | 23 | bicolour | | 82 | 29 | single |
| 49 | 14 | single | | 83, 84 | 0, 1 | duplicates of CC 57, 58 |

- **Buttons without LEDs:** notes 12, 13, 14, 111.
- **LEDs outside the table:** LED 8 (bicolour) is controlled via CC 127, but only under a condition checked by `0x08014E94(9, 0)`. LED 9 has no CC of its own. CC 126 has a separate, unparsed path. **[code; purpose — hypothesis: power/charge]**
- **CC 57–64** light a row of 8 single LEDs that have no button of their own among the 24. **[hypothesis: level/bank indicator]**
- **CC on channel 2** with value 0 or 127 calls the global function `0x08010960`, purpose unknown.

### SysEx (Akai)
Format: **`F0 47 7F 5C <cmd> <lenH> <lenL> <data…> F7`**, the length is 14-bit (`lenH<<7 | lenL`).
- **Akai** — `0x47`, **device ID** — `0x7F`, **AC50 product** — `0x5C`. **[code, app and MCU]**
- **App.** Two AC50-only functions (`0x159DAC0`, `0x159DB40`) have a ready-made 8-byte message `F0 47 7F 5C 42 00 00 F7` hardcoded (a 64-bit constant `0xF70000425C7F47F0`). The generic SysEx builder writes `F0 47` (`strh 0x47F0`), then `7F` and the product byte from the device description.
- **MCU.** The MCU parser points at the data right after the manufacturer byte. This is visible from the Universal Enquiry patterns `7F 06 01` / `00 06 01`. The internal SysEx ID `0x4701` means "manufacturer 0x47, ID length 1". The MCU does not check the `7F 5C` bytes.

| cmd | Data | Purpose |
|---|---|---|
| **0x65** | N × {`pad`, `R`, `G`, `B`} | **pad colour**. In the MCU each 7-bit component is converted to 8 bits as `v·2+1`, the pad index goes up to 16. The app sends **one pad per message**: `F0 47 7F 5C 65 00 04 <pad> <R> <G> <B> F7` (12 bytes), where `R = (ARGB>>17)&0x7F`, `G = (ARGB>>9)&0x7F`, `B = (ARGB>>1)&0x7F`, all multiplied by the brightness factor from the settings (function `0x2396520`). The R, G, B order is confirmed by the app code and **on the device**: the demo sent `R = velocity, G = 0, B = 127 − velocity`, and the pads lit up from blue (soft hit) to red (hard hit). The G position was verified only by exclusion. Index 16 is some 17th RGB LED **[hypothesis]** |
| 0x42 | empty (`00 00`) | the AC50 app code sends it before walking through the 16 pads (update/initialisation) **[purpose — hypothesis]** |
| 0x50 | 16 bytes | XTEA challenge-response: the surface proves its authenticity to the app. The key lies in the firmware and is **intentionally not given** here. Our own app does not need this command |
| 0x40/0x41, 0x52 (4 B), 0x5A (32 B), 0x62 (1 B), 0x64 (3 B), 0x6B, 0x6C, 0x79 (16 B) | — | not parsed. Builders for almost all of these commands exist in the app (common code for MPC/Force) |
| `F0 7E … 06 01 F7` | — | Universal Device Enquiry, the reply carries the version (used by `az0x-firmware-tool`) |

There is also a separate **service branch** (internal ID 0x17000003: commands 0x60, 0x70→0x71, 0x72/0x73, 0x79). It is apparently used by `midifirmup` (update, `WriteSerialNumber`, `WriteHardwareRevision`). **Do not send anything there.**

## 6. What to verify on the device

This requires briefly stopping `ac50.service` and `amidi -d` on "Control Surface". Your consent is required.
1. The exact first bytes of the SysEx header. For this, capture the app's Tx: for example, the pad colours when switching banks.
2. The mapping "index → physical button / knob / pad", and what CC 100 is.
3. The R/G/B order in command 0x65 and what index 16 is.
4. Whether the MCU requires a handshake to send events without the stock app.

## 8. Verification on the device (2026-10-22, `amidi -d`, app stopped, receive only)

Capture file: [`captures/surface-capture2-20261022.txt`](../captures/surface-capture2-20261022.txt). It is 1080 messages, **all 3 bytes each**, with no SysEx and no active sensing.

- **No handshake needed.** The MCU sends events even without the stock app, right after `systemctl stop ac50`.
- **Pads 1→16** (pressed in order) gave the notes 37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53. This is exactly the table from §4. Each hit is `99 nn vv`, then `A9 nn …`, then `89 nn 00`.
- **Analog controls:**

  | Control (as the owner turned it) | Message | Range |
  |---|---|---|
  | knob 1 (leftmost) | `B0 13 vv` (CC 19) | 0…127 |
  | knob 2 | `B0 10 vv` (CC 16) | 0…127 |
  | knob 3 | `B0 11 vv` (CC 17) | 0…127 |
  | knob 4 | `B0 12 vv` (CC 18) | 0…127 |
  | **fader** | `B0 14 vv` (CC 20) | 0…127, absolute value, ADC (index 4 in the MCU) |
  | **large encoder** | `B0 64 vv` (CC 100) | relative: `01` = +1, `7F` = −1 (which of these is clockwise has not yet been verified on the device); via a separate path (index 5 in the MCU) |

  **CC 19 = MAIN VOLUME, CC 16–18 = K1–K3.** In the photo of the surface (2026-10-22) MAIN VOLUME is the leftmost knob, at the top left; K1, K2, K3 go to its right. The owner turned them left to right. MAIN VOLUME is an ordinary surface knob (CC), so the volume is presumably software.
  **Corrected 2026-10-22.** Initially the fader and the encoder were mixed up: they were named in the order in which they were turned. Verified two ways:
  - **[code]** the stock parser `AC50MidiParser` (function `0x159f220` in `MPC Sample`) passes CC 16–20 on as the absolute value `v/127`. It reads CC 100 as a signed increment: `v > 64 ? v − 128 : v`;
  - **[capture]** in all captures CC 100 arrives only as `01` or `7F`. CC 20 ranges 0…127 and rests at the edges for 0.3–0.5 s, like a slider against the stop.
  This also explains the "2 positions" seen in the phase 1 firmware: the encoder steps `01`/`7F` went into the fader handling and set the pad volume to the minimum or the maximum.
- **Buttons** (24 of them, press `90 nn 7F`, release `80 nn 00`, names from the owner):

  | Button | Note | LED (CC = note) | | Button | Note | LED |
  |---|---|---|---|---|---|---|
  | SAMPLE | 6 | bicolour | | CHOP | 75 | bicolour |
  | SEQ | 7 | bicolour | | MUTE | 43 | bicolour |
  | PAD FX | 1 | bicolour | | LOOP | 74 | bicolour |
  | KNOB FX | 0 | bicolour | | 16 LEVELS | 40 | bicolour |
  | SHIFT | 49 | single | | SAMPLE SELECT | 36 | bicolour |
  | PAD BANK | 35 | single | | TAP TEMPO | 53 | bicolour |
  | ERASE | 9 | bicolour | | encoder press | 111 | none |
  | NOTE REPEAT | 11 | single | | MINUS | 55 | single |
  | B1 | 12 | none | | PLUS | 54 | single |
  | B2 | 13 | none | | SAMPLE RECORD | 73 | single |
  | B3 | 14 | none | | SEQ RECORD | 80 | single |
  | | | | | STOP | 81 | single |
  | | | | | PLAY | 82 | single |

  The buttons without an LED according to the firmware (notes 12, 13, 14, 111) turned out to be B1–B3 and the encoder press. This matches the CC→LED table.
  The row of 8 single LEDs on CC 57–64 does not belong to any button. **[hypothesis: pad bank indicators A–H]** In the photo, to the left of the screen, the letters `A B C D / E F G H` are printed with LEDs. This can be checked by sending CC 57–64.

The SysEx header and the R/G/B order in 0x65 were established offline from the app binary (§5). Still open are index 16 in 0x65, the purpose of 0x42 and the row of LEDs on CC 57–64.

## 7. Correction to a previous conclusion

Previously I wrote that in the test window "there was apparently no SysEx". This does not follow: SysEx 0x65 has a length of 8 + 4N bytes, and that number is a multiple of 3 for N = 1, 4, 7, 10, 13, 16.
