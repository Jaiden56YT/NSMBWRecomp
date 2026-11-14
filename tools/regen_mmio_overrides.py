#!/usr/bin/env python3
"""Regenerate NSMBWiicompiled's "structural copy" native overrides from the US
translation (they were copies of the PAL translated bodies).

Upstream's edit in each was to send the accesses that hit hardware registers
through the checked Memory:: path instead of the flat fast path (which faults on
MMIO). Here every plain FlatReadN/FlatWriteN becomes Memory::ReadN/WriteN; the
checked path also serves RAM, and these are small boot-time functions.
FlatWriteRam* (stack) and the *Resolved* accesses (small-data globals) stay.

Usage: python3 -I tools/regen_mmio_overrides.py PLAIN_FUNCS_DIR NATIVE_DIR
"""
import re
import sys

OVERRIDES = [  # (US address, file, native function name, what it is)
    ('801B9BA0', 'nsmbw_exi_init.cpp', 'EXIInit_NSMBW_801B9CE0', 'EXIInit'),
    ('801B95C0', 'nsmbw_exi_select.cpp', 'EXISelect_NSMBW_801B9700', 'EXISelect'),
    ('801B96F0', 'nsmbw_exi_deselect.cpp', 'EXIDeselect_NSMBW_801B9830', 'EXIDeselect'),
    ('801B8B20', 'nsmbw_exi_imm_start.cpp', 'EXIImmStart_NSMBW_801B8C60', 'EXIImm (start)'),
    ('801B8F50', 'nsmbw_exi_imm_wait.cpp', 'EXIImmWait_NSMBW_801B9090', 'EXISync / imm wait'),
]


def main():
    plain, native = sys.argv[1], sys.argv[2]
    for addr, fn, name, what in OVERRIDES:
        body = open(f'{plain}/func_{addr}.cpp').read()
        body = body.replace(f'extern "C" void func_{addr}(', f'extern "C" void {name}(', 1)
        n = 0
        def sub(m):
            nonlocal n
            n += 1
            return f'Memory::{m.group(1)}{m.group(2)}'
        body = re.sub(r'MemoryInline::Flat(Read|Write)(8|16|32)\b', sub, body)
        head = (f'// {what}: native override regenerated for USA Rev 2 (tools/regen_mmio_overrides.py)\n'
                f'// from the US translated body of 0x{addr}. Upstream NSMBWiicompiled kept a structural\n'
                f'// copy of the PAL body with its hardware-register accesses on the checked Memory:: path\n'
                f'// (flat access faults on MMIO); here all {n} plain flat accesses are checked.\n'
                '#include "hle_stubs.h"\n#include "memory.h"\n')
        tail = f'\nPPC_NATIVE_OVERRIDE_VOID({addr}, {name}, (CpuContext* ctx), (ctx));\n'
        open(f'{native}/{fn}', 'w').write(head + body + tail)
        print(f'{fn}: {n} accesses switched to the checked path')


if __name__ == '__main__':
    main()
