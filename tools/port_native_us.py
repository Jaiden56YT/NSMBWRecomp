#!/usr/bin/env python3
"""Copy NSMBWiicompiled's NSMBW-specific sources with PAL guest addresses
converted to USA Rev 2 (Kamek's offset table, see make_syms.py).

Rewrites PPC_NATIVE_OVERRIDE*(ADDR, ...) bindings and 0x80xxxxxx-0x81xxxxxx
constants. Bindings must land on a function start in the US files; anything
doubtful is listed in the report for review.

Usage: python3 -I tools/port_native_us.py SRC_DIR DST_DIR REPORT
"""
import os
import re
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from make_syms import fix_ntsc_v2
from pal_to_us_map import looks_like_start, word

BIND = re.compile(r'(PPC_NATIVE_OVERRIDE[A-Z_]*\(\s*)([0-9A-Fa-f]{8})(\s*,)')
CONST = re.compile(r'0x(8[01][0-9A-Fa-f]{6})(?=[uU]?(?:[lL]{1,2})?\b)')


def main():
    src, dst, report = sys.argv[1:4]
    os.makedirs(dst, exist_ok=True)
    rep = []
    nb = nc = 0
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(('.cpp', '.h', '.hpp')):
            continue
        text = open(os.path.join(src, fn), encoding='utf-8', errors='surrogateescape').read()
        def bind(m):
            nonlocal nb
            a = int(m.group(2), 16)
            u = fix_ntsc_v2(a)
            ok = looks_like_start(u)
            nb += 1
            rep.append(f'{fn}: bind {a:08X} -> {u:08X}' + ('' if ok else '   ** not a function start in the US files **'))
            return f'{m.group(1)}{u:08X}{m.group(3)}'
        text = BIND.sub(bind, text)
        def const(m):
            nonlocal nc
            a = int(m.group(1), 16)
            u = fix_ntsc_v2(a)
            if u != a:
                nc += 1
                kind = 'code' if word(u)[0] is not None else 'data'
                flag = '' if kind == 'data' or looks_like_start(u) else '   (code address, not a function start: check)'
                rep.append(f'{fn}: const {a:08X} -> {u:08X} [{kind}]{flag}')
            return f'0x{u:08X}' if m.group(1).isupper() or True else m.group(0)
        text = CONST.sub(const, text)
        open(os.path.join(dst, fn), 'w', encoding='utf-8', errors='surrogateescape').write(text)
    open(report, 'w').write('\n'.join(rep) + '\n')
    print(f'{nb} bindings, {nc} constants changed; report in {report}', file=sys.stderr)


if __name__ == '__main__':
    main()
