"""RBIN container reader + Thumb disassembly helpers for the MPC Sample control-surface MCU image.
Usage: rbin.py [path/to/UpdateImage.rbin]   (needs: pip install capstone)

RBIN layout (derived from UpdateImage.rbin, see findings doc):
  block := "!Rbn" + copyright (to 0x24) + 8 x u32 header + n x region
  header: peripheral_id, slot, version, base, span, crc, block_len, n_regions
  region: addr, size, crc32(data), offset (from block start)
"""
import struct, sys, zlib
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS

RBIN = '/Users/sapfir/code/mpc-sample-analysis/work/rootfs/usr/share/Akai/AC50/Firmware/AC50/UpdateImage.rbin'


def blocks(path=RBIN):
    b = open(path, 'rb').read()
    out, o = [], 0
    while o < len(b):
        assert b[o:o+4] == b'!Rbn', hex(o)
        pid, slot, ver, base, span, crc, blen, n = struct.unpack_from('<8I', b, o + 0x24)
        img = bytearray(b'\xff' * span)
        for k in range(n):
            a, s, c, off = struct.unpack_from('<4I', b, o + 0x44 + 16 * k)
            data = b[o + off:o + off + s]
            assert zlib.crc32(data) == c, 'region crc'
            img[a - base:a - base + s] = data
        out.append(dict(pid=pid, slot=slot, ver=ver, base=base, span=span, crc=crc, img=bytes(img),
                        crc_ok=zlib.crc32(bytes(img)) == crc))
        o += blen
    return out


def ver_str(v):
    return f'{v >> 24}.{(v >> 16) & 0xff}.{(v >> 8) & 0xff}.{v & 0xff:03d}'


def cs():
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    md.detail = True
    md.skipdata = True
    return md


def disasm(blk, start=None, end=None):
    """Linear sweep (data pools decode as junk/.byte; fine for pattern scans)."""
    base, img = blk['base'], blk['img']
    s = (start or base + 0x100) - base
    e = (end or base + len(img)) - base
    return list(cs().disasm(img[s:e], base + s))


def u32(blk, addr):
    return struct.unpack_from('<I', blk['img'], addr - blk['base'])[0]


if __name__ == '__main__':
    for k in blocks(sys.argv[1] if len(sys.argv) > 1 else RBIN):
        print(f"slot={k['slot']} pid={k['pid']:#010x} ver={ver_str(k['ver'])} base={k['base']:#010x} "
              f"span={k['span']:#x} crc_ok={k['crc_ok']}")


def analyze(blk):
    """Return (instructions, helpers) for xref work on one block."""
    import re
    ins = [i for i in disasm(blk) if i.mnemonic != '.byte']
    base, img = blk['base'], blk['img']

    def pool_addr(i):
        m = re.match(r'r\d+, \[pc, #(0x[0-9a-f]+|\d+)\]', i.op_str)
        return ((i.address + 4) & ~3) + int(m.group(1), 0) if i.mnemonic == 'ldr' and m else None

    def func_start(k):
        for j in range(k, max(0, k - 600), -1):
            if ins[j].mnemonic == 'push' and 'lr' in ins[j].op_str:
                return ins[j].address
        return None

    def str_at(a):
        o = a - base
        e = img.index(b'\0', o)
        return img[o:e].decode(errors='replace')

    def xrefs_to_value(val):
        out = []
        for k, i in enumerate(ins):
            p = pool_addr(i)
            if p is not None and base <= p < base + len(img) - 3 and u32(blk, p) == val:
                out.append((i.address, func_start(k)))
        return out

    def callers(target):
        return [(i.address, func_start(k)) for k, i in enumerate(ins)
                if i.mnemonic == 'bl' and int(i.op_str.lstrip('#'), 0) == target]

    def listing(start, n=80):
        k = next(k for k, i in enumerate(ins) if i.address >= start)
        out = []
        for i in ins[k:k + n]:
            note = ''
            p = pool_addr(i)
            if p is not None and base <= p < base + len(img) - 3:
                v = u32(blk, p)
                note = f'   ; ={v:#x}'
                if base <= v < base + len(img):
                    s = str_at(v)
                    if s and all(32 <= ord(c) < 127 for c in s[:8]):
                        note += f' "{s[:60]}"'
            out.append(f'{i.address:#010x}: {i.mnemonic:6} {i.op_str}{note}')
        return out

    return ins, dict(pool_addr=pool_addr, func_start=func_start, str_at=str_at,
                     xrefs_to_value=xrefs_to_value, callers=callers, listing=listing)
