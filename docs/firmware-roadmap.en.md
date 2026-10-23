[Русский](firmware-roadmap.md) | **English**

# Own firmware for MPC Sample: function map and phases

Source of the function list: MPC Sample User Guide v1.3.0 (RevA), Features and Operation sections.
Implementation is independent: based on the description of behavior, without Akai code, sounds and graphics.
Runs on top of the stock OS, from RAM, with `ac50.service` stopped: firmware and flash are not changed.

Legend: ✅ done; 🟢 phase 1 (`firmware/`); 🟡 later phases; ⚪ research needed first.

## 1. What hardware is already there (verified)
Panel (pads with velocity and aftertouch, 24 buttons, 3 knobs, MAIN VOLUME, fader, encoder) ✅ ·
button and RGB pad lighting ✅ · I2S sound 44.1 kHz, latency like the stock app ✅ ·
320×240 screen via DRM without sound degradation ✅ · loading WAV from SD ✅ · real-time priority ✅.

## 2. Function map of the User Guide

### Sample mode and pad parameters
| Function | Phase | Comment |
|---|---|---|
| 8 banks × 16 pads, PAD BANK (forward, SHIFT — back, hold + pad 1–8) | 🟢 | bank indicators on CC 57–64 — hypothesis |
| Pad selection without triggering (SAMPLE + pad) | 🟢 | |
| Trim: Start / End / Loop, Loop Lock | 🟢 | no zoom |
| Mix: Volume (−∞, −74…+6 dB), Pan, Kit Volume | 🟢 | |
| Amp Env: Attack, Decay (from start/end), Release, Vel Sens | 🟢 | |
| Tune: Semi ±24, Fine ±90 cents | 🟢 | |
| Play: Mono/Poly, Mute Group 1–16 | 🟢 | |
| Play: Pad Link, Offset | 🟡 | |
| Filter (Classic, LPF/HPF/BPF 2/4) and Filter Env | 🟡 | own DSP filters needed |
| Warp (time stretch / pitch, # Beats, Seq) | 🟡 | time stretch — a separate large task |
| Normalize (SHIFT+B2), Trim Sample (SHIFT+pad 13) | 🟢 | destructive edits in memory |
| Note On (SHIFT+CHOP), Loop (LOOP), Reverse (SHIFT+LOOP) | 🟢 | |
| Mute (button, hold, SHIFT — Unmute All) | 🟢 | |
| Full Level (SHIFT+pad 1) | 🟢 | |
| Chop: Threshold / Regions 4·8·16 / Manual, Extract, Split, Merge | 🟡 | |
| 16 Levels: Velocity / Filter / Tune | 🟡 | Tune — simple, Filter — after filters |
| Erase + pad (unload sample) | 🟢 | |
| Copy (SHIFT+ERASE) | 🟡 | |
| Undo / Redo | 🟡 | |

### Samples and projects
| Function | Phase | Comment |
|---|---|---|
| Loading a folder from SD (first 128 WAV → banks A–H) | 🟢 | temporary replacement for the browser |
| Sample browser (SAMPLE SELECT, preview, Internal/SD) | 🟡 | need an on-screen list and encoder input |
| Saving a sample, name entry | 🟡 | |
| Projects: Load / Save / New, autosave | 🟡 | own format (JSON + WAV), store in `/data` or on SD |
| SD Card Access (USB drive) | ⚪ | USB gadget of the stock OS |

### Sample recording
| Function | Phase | Comment |
|---|---|---|
| Recording from Mic / Rear / Rear L/R, threshold, Free/Seq length, monitoring | 🟡 | I2S capture; source selected by `Builtin Microphone` / `TRS` switches |
| Resample (recording own output) | 🟡 | |
| USB as a source | 🟡 | `UAC2Gadget` card |
| Recall: last 25 from input | 🟡 | ring buffer |
| REC GAIN | ⚪ | hardware knob; unknown whether it is in the panel protocol |

### Sequencer
| Function | Phase | Comment |
|---|---|---|
| 16 × 8 sequences, selection/queue, PLAY / STOP / CONTINUE | 🟡 | |
| BPM (sequence/global), Tap Tempo, size, metronome, count-in | 🟡 | |
| Recording, overdub, Rec Quantize, Q, RT Swing, Recall | 🟡 | |
| Parameter automation | 🟡 | |
| Half/Double Seq, Half/Double Speed | 🟡 | |
| Step Edit (fader — event shift) | 🟡 | |
| Song Mode, export to audio and to a sequence | 🟡 | |
| Time Correct (Q, Shift, Swing) | 🟡 | |
| Note Repeat and triplets | 🟡 | after sequencer clocks |
| Sample/Seq mode | 🟡 | |

### Effects
| Function | Phase | Comment |
|---|---|---|
| Compressor (Attack, Release, Amount, In Boost, Color) | 🟡 | |
| Pad FX: 16 effects, press velocity — depth, up to 4 at once, Latch | 🟡 | own DSP implementations |
| Flex Beat: 16 time effects, One Shot/Loop, Quantize, Mix | 🟡 | |
| Knob FX: ~30 effects, pad selection, SHIFT parameters | 🟡 | own DSP implementations; “vintage” device models — only with own means |

### Menus, MIDI, system
| Function | Phase | Comment |
|---|---|---|
| Fader: assignment (Pad Volume/Pan/Tune/Attack/Decay/Cutoff, Kit Volume) | 🟢 partly | phase 1: Pad Volume with Pickup |
| Knob takeover: Pickup / Scaled / Instant | 🟢 | phase 1: Scaled for knobs, Pickup for the fader |
| MIDI In/Out (TRS and USB), channels, Pad MIDI In/Out, Thru, Program Change | 🟡 | ports `External MIDI` and `f_midi` are already visible in ALSA |
| MIDI Sync In/Out (Clock, MTC) | 🟡 | |
| CV/Sync Out | ⚪ | unknown who controls the SYNC OUT jack (SoC or MCU) |
| Battery and charging indicator | ⚪ | the charge sensor is handled by the MCU; how it reports the level is unknown |
| Level indicators (Meters) | ⚪ | unknown whether these are LEDs or graphics |
| Reset Factory Settings / Data | — | not needed: our firmware writes nothing to the system |

## 3. Phase 1 — what is in `firmware/`
- **Sound engine:** 32 stereo voices, linear pitch interpolation, envelopes, Mono/Poly, mute groups, Loop/Reverse/Note On, short fade when a voice is cut — retrigger, mute group, STOP (no clicks), balance panning (no loss at the centre), soft limiting on the output.
- **Project model:** 8 banks × 16 pads, parameters per the User Guide.
- **Controls:** all phase 1 buttons, SHIFT functions, PAD BANK, MUTE, ERASE, pages B1/B2/B3, K1–K3 with Scaled takeover, fader (Pad Volume, Pickup), encoder and −/+.
- **Screen:** B1–B3 tabs under the buttons, bank/pad, sample name, mode flags, waveform with Start/End/Loop markers (with the amp envelope on the AMP ENV page), three K1–K3 parameters.
- **Lighting:** bright — active functions, dim — available, off — unavailable (as in the User Guide).

## 4. What is missing for later phases (research)
1. How the MCU reports the battery level and charging state.
2. Who outputs CV/Sync Out.
3. Whether REC GAIN is in the panel protocol (likely an analog knob without an MCU).
4. Colors of the two-color LEDs (A/B).
