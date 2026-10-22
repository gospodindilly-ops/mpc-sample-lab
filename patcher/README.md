# mpc-sample-patcher

A **scaffold** for a reversible, shareable mod of the Akai MPC Sample (AC50).

It distributes **only your own changes** — a list of byte offsets and replacement
bytes — never any Akai binary. Each user supplies the stock binary from their own
device. The tooling verifies it is exactly the build the patch set targets, applies
the changes to a copy in RAM, and runs that copy; **internal flash is never
written, and a reboot always restores stock.**

This is a skeleton with working reference code and clearly marked `TODO`s. The
device-specific values (hashes, offsets, service names) need a unit and a root
shell to fill in — it is not a finished, flashable mod, and it has not been run on
hardware.

## Why this shape

The stock app on firmware `1.3.0.12` is `/usr/bin/MPC Sample` (ARM64 ELF, ~119 MB),
launched by `/usr/bin/launch-ac50` from `ac50.service`. The stock app already has a
feature that scans an inserted SD card for a script and asks on-screen
*"Script detected — Do you want to run it?"*. That is the activation path here: the
user confirms, and the script swaps in a patched copy for the current boot only.

```
patches/1.3.0.12.json              <- offsets + bytes YOU change, + stock/result SHA-256
patches/1.3.0.12-phase2-clone.json <- zero patches: clone + bind-mount only (phase 2)
        |  mpcpatch.py emit-device
        v
sdcard/mpc-mod/patches.list   <- flat list the on-device applier reads
sdcard/scripts/autorun        <- what the stock app offers to run (you confirm)
sdcard/mpc-mod/apply.sh        <- verify stock, patch a copy in /tmp, activate
sdcard/mpc-mod/rollback.sh     <- drop the override (reboot does this too)
```

## Safety model

- **No vendor binary is ever shipped.** The repo contains only your offsets/bytes.
  `.gitignore` blocks `*.bin`/`*.img`/`MPC-Sample*` so a stock or patched binary
  can't be committed by accident.
- **Exact-build gate.** Patching is refused unless the stock binary's size and
  SHA-256 match the patch set. Each individual patch also checks the `original`
  bytes before overwriting, so a near-miss never corrupts the file.
- **In-place only.** `patched` must be the same length as `original`; nothing
  shifts. Need more room? Use a code cave or an `LD_PRELOAD` `.so` (future work).
- **Flash is never touched.** The patched binary lives in `/tmp` (tmpfs) and is
  activated with a bind-mount over the stock path. A reboot drops it → stock.
- **Crash guard.** `apply.sh` counts failed attempts in `.pending`; after two it
  backs off and leaves stock running, so a bad patch can't cause a boot loop.
- **Out of scope on purpose:** this does not remove code signing and does not
  touch license/activation checks for paid plugins or expansions.

## Phase 2: prove the loop with a clone first

Before any real patch, validate the whole activation + rollback loop with an
**unmodified** copy. `patches/1.3.0.12-phase2-clone.json` has **zero patches**, so
`apply.sh` just clones the stock binary into RAM and bind-mounts the identical copy
over it, then restarts. The running app is byte-for-byte stock, so this proves code
execution (the "Do you want to run it?" path), the bind-mount, and that
`rollback.sh`/reboot restore stock — without changing a single byte of the app.

```bash
tools/mpcpatch.py emit-device patches/1.3.0.12-phase2-clone.json -o sdcard/mpc-mod/patches.list
```

`apply.sh` clears its `.pending` crash-guard marker and logs success **before** the
service restart, so even if the restart were to kill the script it can never strand
a marker and make the next run read a success as a failure. For dry-running or
testing off-device, `MPC_STOCK` and `MPC_WORK` override the stock path and the RAM
work dir (the device sets neither, so the real paths apply there).

## Workflow (host)

```bash
# 1. Pull the stock binary off your device and record its hash:
tools/mpcpatch.py hash "./MPC Sample"
#    -> paste the hash into patches/1.3.0.12.json  target.sha256

# 2. Add your real patches to patches/1.3.0.12.json (delete the example),
#    then dry-run against the stock binary (writes nothing):
tools/mpcpatch.py verify patches/1.3.0.12.json "./MPC Sample"

# 3. Produce a patched copy and record result.sha256 once it's known-good:
tools/mpcpatch.py apply patches/1.3.0.12.json "./MPC Sample" -o ./MPC-Sample-patched

# 4. Generate the on-device list and copy the sdcard/ tree to an SD card:
tools/mpcpatch.py emit-device patches/1.3.0.12.json -o sdcard/mpc-mod/patches.list
```

## Workflow (device)

Insert the SD card; the stock app offers to run the script; confirm on-screen.
`apply.sh` verifies the stock binary, patches a copy in RAM, and activates it.
To go back: `rollback.sh`, or just reboot.

## Uninstall

Reboot (removes the RAM-only override), then delete the `mpc-mod/` and
`scripts/` folders from the SD card. Nothing was written to the device.

## `patches/*.json` format

```jsonc
{
  "schema": 1,
  "target": {
    "name": "MPC Sample",
    "firmware_version": "1.3.0.12",
    "path_on_device": "/usr/bin/MPC Sample",
    "size": 119243440,
    "sha256": "<stock binary sha256>"
  },
  "result": { "sha256": null },     // optional: expected hash after patching
  "patches": [
    {
      "id": "short-name",
      "description": "what this changes and why",
      "enabled": true,              // optional, default true
      "offset": "0x1234",           // file offset, hex string or decimal
      "original": "aabbccdd",       // bytes expected there now (guard)
      "patched":  "1f2003d5"        // replacement, SAME length as original
    }
  ]
}
```

## Tests

```bash
python3 -m pytest tests/ -q
```

Tests cover the applier's guards (hash/size gate, original-byte check, length and
overlap rules, in-place refusal, device-list round-trip), the phase-2 clone set
(zero active patches, result == target, header-only device list), and `apply.sh`
itself run under the host shell with command shims — including that it clears
`.pending` before the restart. They use synthetic blobs — no hardware or vendor
binary required. (Default Python has no `pytest`; use a throwaway venv.)

## Status

Scaffold only. Needs, from someone with the device: the stock/result hashes, the
real patch offsets, and confirmation of the bind-mount + `ac50.service` activation
path. Credit for the hardware/boot findings: the `mpc-forums.com` MPC Sample thread
(briwil78, vinylstyle, puckcv).
