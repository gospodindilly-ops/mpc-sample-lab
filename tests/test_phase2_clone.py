"""Tests for the phase-2 clone patch set and zero-patch emit-device.

Phase 2 proves the activation + rollback loop with an UNMODIFIED clone: a patch
set with zero patches, whose result hash equals its target hash. These tests use
no hardware and no vendor binary - the real phase-2 JSON is inspected for shape,
and the zero-patch apply/emit behaviour is exercised on a synthetic blob.

Run: python3 -m pytest tests/ -q   (or: python3 tests/test_phase2_clone.py)
"""
import hashlib
import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import mpcpatch  # noqa: E402


def sha(b):
    return hashlib.sha256(b).hexdigest()


def _is_real_sha(value):
    return (
        isinstance(value, str)
        and len(value) == 64
        and not value.lower().startswith("todo")
        and all(c in "0123456789abcdef" for c in value.lower())
    )


# ---- the real phase-2 clone file ------------------------------------------

PHASE2 = REPO / "patches" / "1.3.0.12-phase2-clone.json"
MAIN = REPO / "patches" / "1.3.0.12.json"


def test_phase2_file_loads_and_has_zero_active_patches():
    spec = mpcpatch.load_patchset(PHASE2)
    assert spec["patches"] == []
    assert mpcpatch._active_patches(spec) == []


def test_phase2_result_equals_target_and_is_real():
    spec = json.loads(PHASE2.read_text())
    tgt = spec["target"]["sha256"]
    res = spec["result"]["sha256"]
    assert _is_real_sha(tgt), "phase-2 target.sha256 must be a real hash, not TODO"
    assert res == tgt, "a clone of stock must have result.sha256 == target.sha256"
    assert spec["target"]["size"] == 119243440


def test_main_patchset_has_real_target_sha():
    spec = json.loads(MAIN.read_text())
    assert _is_real_sha(spec["target"]["sha256"]), (
        "patches/1.3.0.12.json must carry the real target.sha256"
    )


def test_phase2_emit_device_is_headers_only(tmp_path):
    out = tmp_path / "patches.list"
    rc = mpcpatch.main(["emit-device", str(PHASE2), "-o", str(out)])
    assert rc == 0
    lines = out.read_text().splitlines()
    tgt = json.loads(PHASE2.read_text())["target"]["sha256"]
    assert f"#target-sha256 {tgt}" in lines
    assert f"#result-sha256 {tgt}" in lines
    # No patch (non-comment, non-blank) lines at all - clone means apply nothing.
    body = [ln for ln in lines if ln.strip() and not ln.startswith("#")]
    assert body == []


# ---- zero-patch semantics on a synthetic blob -----------------------------

STOCK = bytes(range(256)) * 4  # 1024 deterministic bytes


def _clone_spec(stock):
    return {
        "schema": 1,
        "target": {"name": "t", "size": len(stock), "sha256": sha(stock)},
        "result": {"sha256": sha(stock)},
        "patches": [],
    }


def test_zero_patch_apply_returns_identical_bytes():
    spec = _clone_spec(STOCK)
    mpcpatch.verify_target(spec, STOCK)             # no raise
    out = mpcpatch.apply_patches(spec, STOCK)
    assert bytes(out) == STOCK                      # clone == stock
    mpcpatch.verify_result(spec, bytes(out))        # result sha matches


def test_zero_patch_verify_cmd_ok(tmp_path, capsys):
    stock = tmp_path / "stock.bin"
    stock.write_bytes(STOCK)
    pj = tmp_path / "clone.json"
    pj.write_text(json.dumps(_clone_spec(STOCK)))
    rc = mpcpatch.main(["verify", str(pj), str(stock)])
    assert rc == 0


def test_zero_patch_apply_cmd_writes_identical_copy(tmp_path):
    stock = tmp_path / "stock.bin"
    stock.write_bytes(STOCK)
    out = tmp_path / "out.bin"
    pj = tmp_path / "clone.json"
    pj.write_text(json.dumps(_clone_spec(STOCK)))
    rc = mpcpatch.main(["apply", str(pj), str(stock), "-o", str(out)])
    assert rc == 0
    assert out.read_bytes() == STOCK


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
