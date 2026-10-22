#!/usr/bin/env python3
"""mpcpatch - apply a byte-patch set to an MPC Sample application binary.

This operates on a binary you read from your OWN device. It:
  - verifies the stock binary's size + SHA-256 against the patch set,
  - checks the expected 'original' bytes at every offset before touching them,
  - applies each patch in place (patched bytes must match original length),
  - optionally verifies the result's SHA-256,
  - and NEVER writes to the input (output goes to a separate file).

It never contains, ships, or reproduces any vendor binary - only your own
offsets and replacement bytes live in the patch set.

Usage:
  mpcpatch.py apply   PATCHES.json STOCK_BINARY -o OUT_BINARY
  mpcpatch.py verify  PATCHES.json STOCK_BINARY          # check hashes/guards only
  mpcpatch.py hash    FILE                               # print sha256 + size
  mpcpatch.py emit-device PATCHES.json -o patches.list   # on-device apply list

Exit status is non-zero on any mismatch, so it is safe to gate a script on it.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import sys
from pathlib import Path


class PatchError(Exception):
    """Any condition that must stop the patch run."""


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _parse_int(value) -> int:
    """Accept 12345 or "0x3039" (string, hex or decimal)."""
    if isinstance(value, int):
        return value
    text = str(value).strip()
    return int(text, 16) if text.lower().startswith("0x") else int(text, 10)


def _hexbytes(value, field: str) -> bytes:
    text = "".join(str(value).split())
    try:
        out = bytes.fromhex(text)
    except ValueError as exc:
        raise PatchError(f"{field}: not valid hex: {value!r}") from exc
    if not out:
        raise PatchError(f"{field}: empty byte string")
    return out


def load_patchset(path: Path) -> dict:
    try:
        spec = json.loads(path.read_text())
    except json.JSONDecodeError as exc:
        raise PatchError(f"{path}: invalid JSON: {exc}") from exc
    if spec.get("schema") != 1:
        raise PatchError(f"{path}: unsupported schema {spec.get('schema')!r}, expected 1")
    if "target" not in spec or "patches" not in spec:
        raise PatchError(f"{path}: missing 'target' or 'patches'")
    return spec


def _active_patches(spec: dict) -> list[dict]:
    return [p for p in spec["patches"] if p.get("enabled", True)]


def verify_target(spec: dict, data: bytes) -> None:
    """Check the stock binary is exactly the build this patch set targets."""
    target = spec["target"]
    want_size = target.get("size")
    if want_size is not None and len(data) != want_size:
        raise PatchError(
            f"stock size {len(data)} != expected {want_size} "
            "(wrong firmware version or truncated read)"
        )
    want_sha = target.get("sha256")
    if not want_sha or str(want_sha).lower().startswith("todo"):
        raise PatchError(
            "target.sha256 is not set - fill it in with the hash of your stock "
            "binary (mpcpatch.py hash STOCK_BINARY) before applying"
        )
    got = _sha256(data)
    if got != want_sha:
        raise PatchError(
            f"stock sha256 mismatch\n  expected {want_sha}\n  got      {got}\n"
            "This is NOT the build this patch set targets - refusing to patch."
        )


def apply_patches(spec: dict, data: bytes) -> bytes:
    """Return a new bytearray with every enabled patch applied in place."""
    out = bytearray(data)
    size = len(out)
    seen: dict[int, str] = {}
    for patch in _active_patches(spec):
        pid = patch.get("id", "<no id>")
        off = _parse_int(patch["offset"])
        original = _hexbytes(patch["original"], f"{pid}.original")
        patched = _hexbytes(patch["patched"], f"{pid}.patched")
        if len(original) != len(patched):
            raise PatchError(
                f"{pid}: original is {len(original)} bytes but patched is "
                f"{len(patched)} - in-place patches must keep the same length"
            )
        end = off + len(original)
        if off < 0 or end > size:
            raise PatchError(f"{pid}: offset range {off}..{end} is outside the file ({size} bytes)")
        for i in range(off, end):
            if i in seen:
                raise PatchError(f"{pid}: byte {i} already written by patch {seen[i]!r} (overlap)")
            seen[i] = pid
        actual = bytes(out[off:end])
        if actual != original:
            raise PatchError(
                f"{pid}: bytes at {hex(off)} do not match expected 'original'\n"
                f"  expected {original.hex()}\n  got      {actual.hex()}\n"
                "The binary differs from what this patch assumes - refusing to patch."
            )
        out[off:end] = patched
    return out


def verify_result(spec: dict, patched: bytes) -> None:
    want = (spec.get("result") or {}).get("sha256")
    if not want:
        return
    got = _sha256(patched)
    if got != want:
        raise PatchError(
            f"result sha256 mismatch\n  expected {want}\n  got      {got}\n"
            "The patched output is not the known-good build - not writing it."
        )


def _read(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        raise PatchError(f"cannot read {path}: {exc}") from exc


def cmd_hash(args) -> int:
    data = _read(Path(args.file))
    print(f"{_sha256(data)}  {len(data)}  {args.file}")
    return 0


def cmd_verify(args) -> int:
    spec = load_patchset(Path(args.patches))
    data = _read(Path(args.stock))
    verify_target(spec, data)
    patched = apply_patches(spec, data)   # runs all the per-patch guards
    verify_result(spec, patched)
    n = len(_active_patches(spec))
    print(f"OK: stock matches target, {n} patch(es) apply cleanly.")
    if (spec.get("result") or {}).get("sha256"):
        print("OK: patched result matches result.sha256.")
    else:
        print(f"note: result.sha256 not set; patched build would be {_sha256(patched)}")
    return 0


def cmd_apply(args) -> int:
    spec = load_patchset(Path(args.patches))
    stock = Path(args.stock)
    out = Path(args.out)
    if out.resolve() == stock.resolve():
        raise PatchError("output path must differ from the stock input (never patch in place)")
    data = _read(stock)
    verify_target(spec, data)
    patched = apply_patches(spec, data)
    verify_result(spec, patched)
    out.write_bytes(patched)
    print(f"wrote {out} ({len(patched)} bytes)")
    print(f"  stock   {_sha256(data)}")
    print(f"  patched {_sha256(patched)}")
    return 0


def cmd_emit_device(args) -> int:
    """Emit a flat list the on-device ash applier reads: one patch per line,
       'offset_decimal  base64(original)  base64(patched)', plus header lines.
       Contains only your own bytes - safe to ship."""
    spec = load_patchset(Path(args.patches))
    target = spec["target"]
    lines = [
        "# generated by mpcpatch.py emit-device - do not edit by hand",
        f"#target-size {target.get('size', '')}",
        f"#target-sha256 {target.get('sha256', '')}",
        f"#result-sha256 {(spec.get('result') or {}).get('sha256') or ''}",
    ]
    for patch in _active_patches(spec):
        off = _parse_int(patch["offset"])
        original = _hexbytes(patch["original"], f"{patch.get('id')}.original")
        patched = _hexbytes(patch["patched"], f"{patch.get('id')}.patched")
        if len(original) != len(patched):
            raise PatchError(f"{patch.get('id')}: original/patched length mismatch")
        b64o = base64.b64encode(original).decode()
        b64p = base64.b64encode(patched).decode()
        lines.append(f"{off} {b64o} {b64p}")
    text = "\n".join(lines) + "\n"
    if args.out and args.out != "-":
        Path(args.out).write_text(text)
        print(f"wrote {args.out} ({len(_active_patches(spec))} patch(es))")
    else:
        sys.stdout.write(text)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="mpcpatch", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("apply", help="verify then write a patched copy")
    p.add_argument("patches")
    p.add_argument("stock")
    p.add_argument("-o", "--out", required=True)
    p.set_defaults(func=cmd_apply)

    p = sub.add_parser("verify", help="check hashes and guards, write nothing")
    p.add_argument("patches")
    p.add_argument("stock")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("hash", help="print sha256 and size of a file")
    p.add_argument("file")
    p.set_defaults(func=cmd_hash)

    p = sub.add_parser("emit-device", help="write the on-device apply list")
    p.add_argument("patches")
    p.add_argument("-o", "--out", default="-")
    p.set_defaults(func=cmd_emit_device)
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except PatchError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
