#!/usr/bin/env python3
"""Build a USA Rev 2 (SMNE01 v2) symbol table from PAL v1 symbol lists.

Sources (PAL v1 addresses):
  third_party/NSMBW-Decomp/syms.txt                    name=0xADDR
  third_party/NSMASR-Code/Kamek/tools/kamek_pal.x      name = 0xADDR;
Conversion: fix_offs_ntsc_v2 from NSMASR-Code/Kamek/tools/mapfile_tool.py.
REL addresses (PAL assumes the modules at the game's own load addresses)
are rebased onto where the host links the recompiled RELs.

Usage: python3 -I tools/make_syms.py > code/syms_smne01.txt
"""
import re
import sys

ROOT = __file__.rsplit('/tools/', 1)[0]


def fix_ntsc_v1(o):
    if 0x800B4604 <= o <= 0x800C8E4C: return o - 0x50
    if 0x800C8E50 <= o <= 0x800E4D70: return o - 0xF0
    if 0x800E4EC0 <= o <= 0x8010F200: return o - 0x110
    if 0x8010F430 <= o <= 0x802BB6BC: return o - 0x140
    if 0x802BB6C0 <= o <= 0x802BB74C: return o - 0x150
    if 0x802BB860 <= o <= 0x802BBBFC: return o - 0x260
    if 0x802BBC90 <= o <= 0x802EDCC0: return o - 0x2F0
    if 0x802EDCE0 <= o <= 0x80317734: return o - 0x300
    if 0x80317750 <= o <= 0x80322FE0: return o - 0x318
    if 0x80323118 <= o <= 0x8032E77C: return o - 0x348
    if 0x8032E780 <= o <= 0x8035197C: return o - 0x340
    if 0x80351980 <= o <= 0x80427E87: return o - 0x300
    if 0x80427E88 <= o <= 0x80429563: return o - 0x310
    if 0x80429564 <= o <= 0x80429D7F: return o - 0x2F8
    if 0x80429D80 <= o <= 0x807684BF: return o - 0x2E0
    if o >= 0x8098A43C: return o + 0x20
    return o


def fix_ntsc_v2(o):
    o = fix_ntsc_v1(o)
    if 0x800CF5F8 <= o <= 0x800CF81F: return o + 8
    if 0x807685A0 <= o <= 0x807AAA70: return o + 0x40
    if 0x807AAA74 <= o <= 0x8099081C: return o + 0x10
    if o >= 0x80990820: return o + 0x20
    return o


# Where the game loads each REL (module header address, observed in the
# host log) and where the host links the recompiled copy.
RELS = [  # (game load address, size, host base)
    (0x80768500, 0x5190, 0x81DC0000),     # d_profileNP (1)
    (0x8076D6C0, 0x235620, 0x81800000),   # d_basesNP (2)
    (0x809A2CE0, 0x179C80, 0x81BA0000),   # d_enemiesNP (3)
    (0x80B1C960, 0x71A00, 0x81B00000),    # d_en_bossNP (4)
]


def to_host(o):
    for game, size, host in RELS:
        if game <= o < game + size:
            return o - game + host
    return o


def main():
    syms = {}
    for line in open(f'{ROOT}/third_party/NSMBW-Decomp/syms.txt'):
        m = re.match(r'\s*([^=\s]+)\s*=\s*(0x[0-9A-Fa-f]+)', line)
        if m: syms.setdefault(m.group(1), int(m.group(2), 16))
    for line in open(f'{ROOT}/third_party/NSMASR-Code/Kamek/tools/kamek_pal.x'):
        m = re.match(r'\s*([^=\s]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;', line)
        if m: syms.setdefault(m.group(1), int(m.group(2), 16))
    out = sorted((to_host(fix_ntsc_v2(a)), n) for n, a in syms.items() if a >= 0x80000000)
    for a, n in out:
        print(f'{a:08X} {n}')


if __name__ == '__main__':
    main()
