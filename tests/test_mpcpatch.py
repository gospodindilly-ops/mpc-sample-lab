"""Tests for mpcpatch - the safety gate of the patcher, so cover the guards.

Run: python3 -m pytest tests/ -q      (or: python3 tests/test_mpcpatch.py)
No hardware or vendor binary needed - everything uses synthetic blobs.
"""
import base64
import hashlib
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import mpcpatch  # noqa: E402


def sha(b):
    return hashlib.sha256(b).hexdigest()


def make_spec(stock, patches, result_sha=None):
    return {
        "schema": 1,
        "target": {"name": "t", "size": len(stock), "sha256": sha(stock)},
        "result": {"sha256": result_sha},
        "patches": patches,
    }


STOCK = bytes(range(256)) * 4  # 1024 deterministic bytes


# ---- helpers ---------------------------------------------------------------

def test_parse_int_accepts_hex_and_decimal():
    assert mpcpatch._parse_int("0x10") == 16
    assert mpcpatch._parse_int("16") == 16
    assert mpcpatch._parse_int(16) == 16


def test_hexbytes_rejects_bad_and_empty():
    with pytest.raises(mpcpatch.PatchError):
        mpcpatch._hexbytes("zz", "f")
    with pytest.raises(mpcpatch.PatchError):
        mpcpatch._hexbytes("", "f")


# ---- target verification ---------------------------------------------------

def test_verify_target_ok():
    spec = make_spec(STOCK, [])
    mpcpatch.verify_target(spec, STOCK)  # no raise


def test_verify_target_wrong_hash():
    spec = make_spec(STOCK, [])
    tampered = bytearray(STOCK)
    tampered[-1] ^= 0xFF  # same length, different content -> hash gate, not size gate
    with pytest.raises(mpcpatch.PatchError, match="sha256 mismatch"):
        mpcpatch.verify_target(spec, bytes(tampered))


def test_verify_target_wrong_size():
    spec = make_spec(STOCK, [])
    spec["target"]["size"] = len(STOCK) + 1
    with pytest.raises(mpcpatch.PatchError, match="size"):
        mpcpatch.verify_target(spec, STOCK)


def test_verify_target_todo_sha_refused():
    spec = make_spec(STOCK, [])
    spec["target"]["sha256"] = "TODO-fill-me"
    with pytest.raises(mpcpatch.PatchError, match="not set"):
        mpcpatch.verify_target(spec, STOCK)


# ---- applying --------------------------------------------------------------

def test_apply_single_patch():
    spec = make_spec(STOCK, [{"id": "a", "offset": "0x4",
                              "original": STOCK[4:8].hex(), "patched": "deadbeef"}])
    out = mpcpatch.apply_patches(spec, STOCK)
    assert out[4:8] == bytes.fromhex("deadbeef")
    assert out[:4] == STOCK[:4] and out[8:] == STOCK[8:]


def test_apply_original_mismatch_aborts():
    spec = make_spec(STOCK, [{"id": "a", "offset": 4,
                              "original": "ffffffff", "patched": "deadbeef"}])
    with pytest.raises(mpcpatch.PatchError, match="do not match expected"):
        mpcpatch.apply_patches(spec, STOCK)


def test_apply_length_mismatch_aborts():
    spec = make_spec(STOCK, [{"id": "a", "offset": 4,
                              "original": STOCK[4:8].hex(), "patched": "dead"}])
    with pytest.raises(mpcpatch.PatchError, match="same length"):
        mpcpatch.apply_patches(spec, STOCK)


def test_apply_out_of_range_aborts():
    spec = make_spec(STOCK, [{"id": "a", "offset": len(STOCK) - 1,
                              "original": "0000", "patched": "0101"}])
    with pytest.raises(mpcpatch.PatchError, match="outside the file"):
        mpcpatch.apply_patches(spec, STOCK)


def test_apply_overlap_aborts():
    spec = make_spec(STOCK, [
        {"id": "a", "offset": 4, "original": STOCK[4:8].hex(), "patched": "deadbeef"},
        {"id": "b", "offset": 6, "original": STOCK[6:10].hex(), "patched": "cafebabe"},
    ])
    with pytest.raises(mpcpatch.PatchError, match="overlap"):
        mpcpatch.apply_patches(spec, STOCK)


def test_disabled_patch_is_skipped():
    spec = make_spec(STOCK, [{"id": "a", "offset": 4, "enabled": False,
                              "original": "ffffffff", "patched": "deadbeef"}])
    out = mpcpatch.apply_patches(spec, STOCK)
    assert out == STOCK


def test_result_hash_checked():
    spec = make_spec(STOCK, [{"id": "a", "offset": 4,
                              "original": STOCK[4:8].hex(), "patched": "deadbeef"}])
    patched = mpcpatch.apply_patches(spec, STOCK)
    spec["result"]["sha256"] = sha(patched)
    mpcpatch.verify_result(spec, patched)  # ok
    spec["result"]["sha256"] = sha(b"nope")
    with pytest.raises(mpcpatch.PatchError, match="result sha256 mismatch"):
        mpcpatch.verify_result(spec, patched)


# ---- end to end via files --------------------------------------------------

def test_apply_cmd_writes_output(tmp_path):
    stock = tmp_path / "stock.bin"
    stock.write_bytes(STOCK)
    out = tmp_path / "out.bin"
    spec = make_spec(STOCK, [{"id": "a", "offset": 4,
                              "original": STOCK[4:8].hex(), "patched": "deadbeef"}])
    pj = tmp_path / "p.json"
    pj.write_text(json.dumps(spec))
    rc = mpcpatch.main(["apply", str(pj), str(stock), "-o", str(out)])
    assert rc == 0
    assert out.read_bytes()[4:8] == bytes.fromhex("deadbeef")
    assert stock.read_bytes() == STOCK  # input untouched


def test_apply_refuses_in_place(tmp_path):
    stock = tmp_path / "stock.bin"
    stock.write_bytes(STOCK)
    spec = make_spec(STOCK, [])
    pj = tmp_path / "p.json"
    pj.write_text(json.dumps(spec))
    rc = mpcpatch.main(["apply", str(pj), str(stock), "-o", str(stock)])
    assert rc == 1  # refused


def test_emit_device_roundtrip(tmp_path):
    spec = make_spec(STOCK, [{"id": "a", "offset": 4,
                              "original": STOCK[4:8].hex(), "patched": "deadbeef"}])
    pj = tmp_path / "p.json"
    pj.write_text(json.dumps(spec))
    out = tmp_path / "patches.list"
    rc = mpcpatch.main(["emit-device", str(pj), "-o", str(out)])
    assert rc == 0
    body = out.read_text()
    assert f"#target-sha256 {sha(STOCK)}" in body
    line = [ln for ln in body.splitlines() if not ln.startswith("#") and ln.strip()][0]
    off, b64o, b64p = line.split()
    assert int(off) == 4
    assert base64.b64decode(b64o) == STOCK[4:8]
    assert base64.b64decode(b64p) == bytes.fromhex("deadbeef")


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
