#!/usr/bin/env python3
"""Parse an inMusic AZ0x update image (MPC Sample Update.img).

Layout reconstructed from a hex dump of 1.3.0 (see README):
  0x00  magic "AZ0x", u32 version
  0x10  u32 strtab_start, u32 strtab_end, u32 records_start, u32 records_end
  0x20  u32 strtab_size, u16 ?, u16 record_count, u32 ?, u32 signed_size
  0x30  u32 snapshot_str, u32 description_str
  0x88  u32 ?, u32 product_str
  records (64 bytes each): tag[4], u32 compressed, u64 offset, u64 size,
                           u64 name_str, sha256[32]
  signed_size: u64 key_name_str, then a 256-byte RSA-2048 signature

Read-only: never writes to the input image.
Usage: az0x.py IMAGE [--extract OUTDIR]
"""
import hashlib
import struct
import sys
from pathlib import Path


def cstr(buf, off):
    end = buf.index(b"\0", off)
    return buf[off:end].decode("ascii", "replace")


def parse(img):
    hdr = img[:0x400]
    magic, version = struct.unpack_from("<4sI", hdr, 0)
    if magic != b"AZ0x":
        raise SystemExit(f"bad magic {magic!r}")
    st_start, st_end, rec_start, rec_end = struct.unpack_from("<4I", hdr, 0x10)
    st_size, unk24, rec_count, unk28, signed_size = struct.unpack_from("<IHHII", hdr, 0x20)
    snap_s, desc_s = struct.unpack_from("<II", hdr, 0x30)
    unk88, prod_s = struct.unpack_from("<II", hdr, 0x88)
    strtab = hdr[st_start:st_end]
    info = {
        "version": version,
        "strtab": (st_start, st_end, st_size),
        "records": (rec_start, rec_end, rec_count),
        "signed_size": signed_size,
        "unknown": {"0x24": unk24, "0x28": unk28, "0x88": hex(unk88)},
        "snapshot": cstr(strtab, snap_s),
        "description": cstr(strtab, desc_s),
        "product": cstr(strtab, prod_s),
    }
    recs = []
    for i in range(rec_count):
        tag, comp, off, size, name_s, digest = struct.unpack_from(
            "<4sIQQQ32s", hdr, rec_start + i * 64)
        recs.append({
            "tag": tag.decode(), "compressed": comp, "offset": off,
            "size": size, "name": cstr(strtab, name_s) or "bootloader",
            "sha256": digest.hex(),
        })
    (key_s,) = struct.unpack_from("<Q", hdr, signed_size)
    info["key_name"] = cstr(strtab, key_s)
    info["sig_range"] = (signed_size + 8, signed_size + 8 + 256)
    return info, recs


def main():
    path = Path(sys.argv[1])
    out = Path(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[2] == "--extract" else None
    img = path.read_bytes()
    info, recs = parse(img)
    for k, v in info.items():
        print(f"{k:12} {v}")
    end = 0
    ok = True
    for r in recs:
        data = img[r["offset"]:r["offset"] + r["size"]]
        match = hashlib.sha256(data).hexdigest() == r["sha256"]
        ok &= match
        end = max(end, r["offset"] + r["size"])
        print(f"{r['tag']} {r['name']:10} off=0x{r['offset']:08x} size={r['size']:>11,} "
              f"xz={r['compressed']} head={data[:6].hex()} sha256={'OK' if match else 'MISMATCH'}")
        if out:
            ext = ".xz" if r["compressed"] else ".bin"
            (out / f"{r['name']}{ext}").write_bytes(data)
    print(f"payloads end 0x{end:x}, file size 0x{len(img):x}, trailing: {img[end:].hex() or 'none'}")
    if out:
        sig0, sig1 = info["sig_range"]
        (out / "header_signed.bin").write_bytes(img[:info["signed_size"]])
        (out / "signature.bin").write_bytes(img[sig0:sig1])
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
