#!/usr/bin/env python3
"""Dump device-tree nodes from the kernel FIT of an MPC Sample firmware.

Usage: dtdump.py <kernel.fit> <regex> [regex...]
Prints enabled nodes whose path or compatible matches, with phandles resolved to node paths.
"""
import re, struct, sys


def fdt(blob):
    magic, total, off_struct, off_strings = struct.unpack_from('>4I', blob)
    assert magic == 0xd00dfeed, 'not an FDT'
    strs, p, path, out = blob[off_strings:], off_struct, [], []
    while True:
        tok, = struct.unpack_from('>I', blob, p); p += 4
        if tok == 1:
            e = blob.index(b'\0', p); path.append(blob[p:e].decode()); p = (e + 4) & ~3
        elif tok == 2:
            path.pop()
        elif tok == 3:
            ln, no = struct.unpack_from('>II', blob, p); p += 8
            out.append(('/' + '/'.join(path[1:]), strs[no:strs.index(b'\0', no)].decode(), blob[p:p + ln]))
            p = (p + ln + 3) & ~3
        elif tok == 9:
            return out, total


def dtb_from_fit(fit_bytes):
    props, total = fdt(fit_bytes)
    nodes = {}
    for path, n, v in props: nodes.setdefault(path, {})[n] = v
    name = next(p for p, pr in nodes.items() if p.startswith('/images/fdt-') and pr.get('type') == b'flat_dt\0'
                and 'cmdline' not in p)
    off, size = (struct.unpack('>I', nodes[name][k])[0] for k in ('data-offset', 'data-size'))
    base = (total + 3) & ~3
    return fit_bytes[base + off: base + off + size]


def main():
    fit, pats = open(sys.argv[1], 'rb').read(), [re.compile(x, re.I) for x in sys.argv[2:]]
    props, _ = fdt(dtb_from_fit(fit))
    nodes = {}
    for path, n, v in props: nodes.setdefault(path, {})[n] = v
    ph = {struct.unpack('>I', pr['phandle'])[0]: p for p, pr in nodes.items() if 'phandle' in pr}
    refs = re.compile(r'(-supply$|sound-dai$|remote-endpoint$|^backlight$|-dai$|aux-devs$|-master$|panel$)')
    first_only = re.compile(r'(-gpios?$|^gpios$|^pwms$|^clocks$|^io-channels$|-gpio$)')

    def show(n, v):
        if v and v[-1:] == b'\0' and all(32 <= c < 127 or c == 0 for c in v):
            return ' | '.join(x.decode() for x in v.split(b'\0') if x)
        if len(v) % 4 or len(v) > 96:
            return f'<{len(v)} bytes>'
        cells = struct.unpack(f'>{len(v) // 4}I', v)
        if first_only.search(n):  # phandle + specifier args
            return ' '.join([ph.get(cells[0], hex(cells[0]))] + [hex(c) for c in cells[1:]])
        if refs.search(n):
            return ' '.join(ph.get(c, hex(c)) for c in cells)
        return ' '.join(hex(c) for c in cells)

    for path, pr in nodes.items():
        compat = show('compatible', pr.get('compatible', b''))
        if pr.get('status') == b'disabled\0' or not any(p.search(path) or p.search(compat) for p in pats):
            continue
        print(f'\n# {path}')
        for n, v in pr.items():
            if n not in ('phandle', 'pinctrl-0', 'pinctrl-names', 'interrupts', 'reg', 'clock-names', 'reset-names', 'resets',
                         'assigned-clocks', 'assigned-clock-rates', 'assigned-clock-parents', 'power-domains', 'iommus'):
                print(f'   {n} = {show(n, v)}')


if __name__ == '__main__':
    main()
