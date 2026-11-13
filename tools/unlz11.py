#!/usr/bin/env python3
"""Decompress Nintendo LZ11 (type 0x11) files, e.g. NSMBW's rels/*.rel.LZ.

Usage: python3 -I unlz11.py IN OUT
"""
import sys


def unlz11(src: bytes) -> bytes:
    if src[0] != 0x11:
        raise ValueError(f"not LZ11 (type byte {src[0]:#x})")
    size = int.from_bytes(src[1:4], "little")
    pos = 4
    if size == 0:  # extended header
        size = int.from_bytes(src[4:8], "little")
        pos = 8
    out = bytearray()
    while len(out) < size:
        flags = src[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> bit):
                out.append(src[pos])
                pos += 1
                continue
            b0 = src[pos]
            ind = b0 >> 4
            if ind == 0:
                length = (((b0 & 0xF) << 4) | (src[pos + 1] >> 4)) + 0x11
                pos += 1
            elif ind == 1:
                length = (((b0 & 0xF) << 12) | (src[pos + 1] << 4) | (src[pos + 2] >> 4)) + 0x111
                pos += 2
            else:
                length = ind + 1
            disp = (((src[pos] & 0xF) << 8) | src[pos + 1]) + 1
            pos += 2
            start = len(out) - disp
            if start < 0:
                raise ValueError("back-reference before start of output")
            for i in range(length):
                out.append(out[start + i])
    return bytes(out[:size])


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        data = unlz11(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(data)
    print(f"{sys.argv[2]}: {len(data)} bytes")
