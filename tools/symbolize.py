#!/usr/bin/env python3
"""Annotate 8-digit hex addresses (0x8xxxxxxx) in a log with symbol+offset.

Usage: python3 -I tools/symbolize.py [file] < log   (default symbols: code/syms_smne01.txt)
Only names within 0x2000 bytes of the address are used.
"""
import bisect
import re
import sys

ROOT = __file__.rsplit('/tools/', 1)[0]
path = sys.argv[1] if len(sys.argv) > 1 else f'{ROOT}/code/syms_smne01.txt'
addrs, names = [], []
for line in open(path):
    a, n = line.split(None, 1)
    addrs.append(int(a, 16))
    names.append(n.strip())


def name(a):
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0 or a - addrs[i] > 0x2000:
        return None
    return f'{names[i]}+{a - addrs[i]:X}'


def repl(m):
    a = int(m.group(0), 16)
    n = name(a)
    return f'{m.group(0)}<{n}>' if n else m.group(0)


for line in sys.stdin:
    sys.stdout.write(re.sub(r'\b8[0-1][0-9A-F]{6}\b', repl, line))
